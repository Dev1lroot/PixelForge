#include "ora.h"

#include <QBuffer>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <zlib.h>

namespace {

struct ZipEntry {
    QByteArray name;
    quint32 crc = 0, csize = 0, usize = 0, offset = 0;
    quint16 method = 0;
};

void put16(QByteArray &b, quint16 v) { b.append(char(v & 0xff)); b.append(char(v >> 8)); }
void put32(QByteArray &b, quint32 v) { put16(b, v & 0xffff); put16(b, v >> 16); }
quint16 get16(const QByteArray &b, qsizetype o) { return quint8(b[o]) | (quint8(b[o + 1]) << 8); }
quint32 get32(const QByteArray &b, qsizetype o) { return get16(b, o) | (quint32(get16(b, o + 2)) << 16); }

class ZipWriter
{
public:
    void add(const QByteArray &name, const QByteArray &data)
    {
        ZipEntry e;
        e.name = name;
        e.crc = crc32(0, reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size()));
        e.csize = e.usize = quint32(data.size());
        e.offset = quint32(m_out.size());
        put32(m_out, 0x04034b50);
        put16(m_out, 20);
        put16(m_out, 0);
        put16(m_out, 0);                     // stored
        put16(m_out, 0);
        put16(m_out, 0x21);                  // 1980-01-01
        put32(m_out, e.crc);
        put32(m_out, e.csize);
        put32(m_out, e.usize);
        put16(m_out, quint16(name.size()));
        put16(m_out, 0);
        m_out.append(name);
        m_out.append(data);
        m_entries.append(e);
    }

    QByteArray finish()
    {
        quint32 cdStart = quint32(m_out.size());
        for (const ZipEntry &e : m_entries) {
            put32(m_out, 0x02014b50);
            put16(m_out, 20);
            put16(m_out, 20);
            put16(m_out, 0);
            put16(m_out, 0);
            put16(m_out, 0);
            put16(m_out, 0x21);
            put32(m_out, e.crc);
            put32(m_out, e.csize);
            put32(m_out, e.usize);
            put16(m_out, quint16(e.name.size()));
            put16(m_out, 0);
            put16(m_out, 0);
            put16(m_out, 0);
            put16(m_out, 0);
            put32(m_out, 0);
            put32(m_out, e.offset);
            m_out.append(e.name);
        }
        quint32 cdSize = quint32(m_out.size()) - cdStart;
        put32(m_out, 0x06054b50);
        put16(m_out, 0);
        put16(m_out, 0);
        put16(m_out, quint16(m_entries.size()));
        put16(m_out, quint16(m_entries.size()));
        put32(m_out, cdSize);
        put32(m_out, cdStart);
        put16(m_out, 0);
        return m_out;
    }

private:
    QByteArray m_out;
    QVector<ZipEntry> m_entries;
};

bool readZip(const QByteArray &z, QHash<QString, QByteArray> &files)
{
    qsizetype eocd = -1;
    for (qsizetype i = z.size() - 22; i >= 0 && i >= z.size() - 22 - 65535; --i)
        if (get32(z, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0)
        return false;
    quint16 count = get16(z, eocd + 10);
    qsizetype p = get32(z, eocd + 16);
    for (int i = 0; i < count; ++i) {
        if (p + 46 > z.size() || get32(z, p) != 0x02014b50)
            return false;
        quint16 method = get16(z, p + 10);
        quint32 csize = get32(z, p + 20), usize = get32(z, p + 24);
        quint16 nlen = get16(z, p + 28), elen = get16(z, p + 30), clen = get16(z, p + 32);
        quint32 off = get32(z, p + 42);
        QString name = QString::fromUtf8(z.mid(p + 46, nlen));
        p += 46 + nlen + elen + clen;
        if (off + 30 > quint32(z.size()))
            return false;
        qsizetype data = off + 30 + get16(z, off + 26) + get16(z, off + 28);
        QByteArray comp = z.mid(data, csize);
        if (method == 0) {
            files.insert(name, comp);
        } else if (method == 8) {
            QByteArray out(usize, Qt::Uninitialized);
            z_stream s{};
            if (inflateInit2(&s, -MAX_WBITS) != Z_OK)
                return false;
            s.next_in = reinterpret_cast<Bytef *>(comp.data());
            s.avail_in = uInt(comp.size());
            s.next_out = reinterpret_cast<Bytef *>(out.data());
            s.avail_out = uInt(out.size());
            int rc = inflate(&s, Z_FINISH);
            inflateEnd(&s);
            if (rc != Z_STREAM_END && rc != Z_OK)
                return false;
            files.insert(name, out);
        }
    }
    return true;
}

QByteArray png(const QImage &img)
{
    QByteArray b;
    QBuffer buf(&b);
    buf.open(QIODevice::WriteOnly);
    img.convertToFormat(QImage::Format_ARGB32).save(&buf, "PNG");
    return b;
}

} // namespace

namespace Ora {

bool save(const QString &path, const QVector<Layer> &layers, const QSize &size, double dpi, const QImage &merged,
          QString *error)
{
    ZipWriter zip;
    zip.add("mimetype", "image/openraster");
    QByteArray xml;
    {
        QXmlStreamWriter w(&xml);
        w.setAutoFormatting(true);
        w.writeStartDocument();
        w.writeStartElement("image");
        w.writeAttribute("version", "0.0.5");
        w.writeAttribute("w", QString::number(size.width()));
        w.writeAttribute("h", QString::number(size.height()));
        w.writeAttribute("xres", QString::number(dpi));
        w.writeAttribute("yres", QString::number(dpi));
        w.writeStartElement("stack");
        for (int i = layers.size() - 1; i >= 0; --i) {
            const Layer &l = layers[i];
            w.writeStartElement("layer");
            w.writeAttribute("name", l.name);
            w.writeAttribute("src", QString("data/layer%1.png").arg(i));
            w.writeAttribute("x", "0");
            w.writeAttribute("y", "0");
            w.writeAttribute("opacity", QString::number(l.opacity / 255.0, 'f', 4));
            w.writeAttribute("visibility", l.visible ? "visible" : "hidden");
            w.writeAttribute("composite-op", blendModeSvgOp(l.blend));
            w.writeEndElement();
        }
        w.writeEndElement();
        w.writeEndElement();
        w.writeEndDocument();
    }
    zip.add("stack.xml", xml);
    for (int i = 0; i < layers.size(); ++i)
        zip.add(QString("data/layer%1.png").arg(i).toUtf8(), png(layers[i].image));
    zip.add("Thumbnails/thumbnail.png", png(merged.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    zip.add("mergedimage.png", png(merged));

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(zip.finish());
    return true;
}

bool load(const QString &path, QVector<Layer> &layers, QSize &size, double &dpi, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QHash<QString, QByteArray> files;
    if (!readZip(f.readAll(), files) || !files.contains("stack.xml")) {
        if (error)
            *error = QObject::tr("Not a valid OpenRaster file.");
        return false;
    }
    QXmlStreamReader r(files.value("stack.xml"));
    QVector<Layer> topDown;
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement())
            continue;
        auto a = r.attributes();
        if (r.name() == QLatin1String("image")) {
            size = QSize(a.value("w").toInt(), a.value("h").toInt());
            if (a.hasAttribute("xres"))
                dpi = a.value("xres").toDouble();
        } else if (r.name() == QLatin1String("layer")) {
            Layer l;
            l.name = a.value("name").toString();
            l.visible = a.value("visibility").toString() != "hidden";
            l.opacity = a.hasAttribute("opacity") ? int(a.value("opacity").toDouble() * 255 + 0.5) : 255;
            l.blend = blendModeFromSvgOp(a.value("composite-op").toString());
            QImage src = QImage::fromData(files.value(a.value("src").toString()));
            QImage img(size, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            if (!src.isNull()) {
                QPainter p(&img);
                p.setCompositionMode(QPainter::CompositionMode_Source);
                p.drawImage(a.value("x").toInt(), a.value("y").toInt(), src);
            }
            l.image = img;
            topDown.append(l);
        }
    }
    if (r.hasError() || size.isEmpty() || topDown.isEmpty()) {
        if (error)
            *error = QObject::tr("Could not parse the OpenRaster layer stack.");
        return false;
    }
    layers.clear();
    for (int i = topDown.size() - 1; i >= 0; --i)
        layers.append(topDown[i]);
    return true;
}

} // namespace Ora
