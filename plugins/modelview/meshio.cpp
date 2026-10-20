#include "meshio.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QRegularExpression>
#include <QTextStream>
#include <QUrl>
#include <QtEndian>
#include <cmath>
#include <zlib.h>
#include <cstring>
#include <unordered_map>

namespace ModelView {

namespace {

// Triangles collected per material before they are packed into a Model.
struct Builder {
    QVector<Vertex> vertices;
    QVector<QVector<quint32>> tris;           // per material
    QVector<Material> materials;
    QVector<int> weld;                        // per vertex: id of the vertices sharing its position, for normals
    QVector<bool> needsNormal;
    bool hasUV = false;
    QStringList uvNames;                      // channel 0 lives in Vertex::u/v, the others in extraUV
    QVector<QVector<QVector2D>> extraUV;

    // UV map by name, created on first use
    int uvChannel(const QString &name)
    {
        int i = int(uvNames.indexOf(name));
        if (i < 0) {
            uvNames << name;
            i = int(uvNames.size() - 1);
        }
        return i;
    }
    void setUV(int channel, quint32 vertex, float u, float v)
    {
        hasUV = true;
        if (channel == 0) {
            vertices[vertex].u = u;
            vertices[vertex].v = v;
            return;
        }
        while (extraUV.size() < channel)
            extraUV.append(QVector<QVector2D>());
        QVector<QVector2D> &c = extraUV[channel - 1];
        if (c.size() <= qsizetype(vertex))
            c.resize(vertices.size());
        c[vertex] = QVector2D(u, v);
    }

    int material(const QString &name)
    {
        for (int i = 0; i < materials.size(); ++i)
            if (materials[i].name == name)
                return i;
        Material m;
        m.name = name;
        materials.append(m);
        tris.append(QVector<quint32>());
        return int(materials.size() - 1);
    }
    quint32 add(const Vertex &v, int weldId, bool missingNormal)
    {
        vertices.append(v);
        weld.append(weldId < 0 ? int(vertices.size() - 1) : weldId);
        needsNormal.append(missingNormal);
        return quint32(vertices.size() - 1);
    }
    void triangle(int mat, quint32 a, quint32 b, quint32 c)
    {
        tris[mat] << a << b << c;
    }

    Model finish(const QString &file)
    {
        Model m;
        m.file = file;
        m.hasUV = hasUV;
        // smooth normals for vertices that came without one, area-weighted over shared positions
        if (needsNormal.contains(true)) {
            QHash<int, QVector3D> acc;
            for (const QVector<quint32> &t : tris)
                for (int i = 0; i + 2 < t.size(); i += 3) {
                    const QVector3D n = QVector3D::crossProduct(vertices[t[i + 1]].pos - vertices[t[i]].pos,
                                                                vertices[t[i + 2]].pos - vertices[t[i]].pos);
                    for (int k = 0; k < 3; ++k)
                        acc[weld[t[i + k]]] += n;
                }
            for (int i = 0; i < vertices.size(); ++i)
                if (needsNormal[i])
                    vertices[i].normal = acc.value(weld[i]).normalized();
        }
        m.vertices = vertices;
        if (hasUV) {
            m.uvSets = uvNames;
            while (m.uvSets.size() < 1 + extraUV.size())
                m.uvSets << QStringLiteral("UV %1").arg(m.uvSets.size() + 1);
            if (m.uvSets.first().isEmpty())
                m.uvSets.first() = QStringLiteral("UVMap");
            QVector<QVector2D> first(vertices.size());
            for (int i = 0; i < vertices.size(); ++i)
                first[i] = QVector2D(vertices[i].u, vertices[i].v);
            m.uvData << first;
            for (QVector<QVector2D> c : extraUV) {
                c.resize(vertices.size());
                m.uvData << c;
            }
        }
        for (int i = 0; i < tris.size(); ++i) {
            if (tris[i].isEmpty())
                continue;
            Part p;
            p.material = i;
            p.first = int(m.indices.size());
            p.count = int(tris[i].size());
            m.indices += tris[i];
            m.parts.append(p);
        }
        m.materials = materials;
        if (!m.vertices.isEmpty()) {
            QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
            for (quint32 i : m.indices) {
                const QVector3D &p = m.vertices[i].pos;
                lo = QVector3D(std::min(lo.x(), p.x()), std::min(lo.y(), p.y()), std::min(lo.z(), p.z()));
                hi = QVector3D(std::max(hi.x(), p.x()), std::max(hi.y(), p.y()), std::max(hi.z(), p.z()));
            }
            m.center = (lo + hi) / 2;
            float r = 0;
            for (quint32 i : m.indices)
                r = std::max(r, (m.vertices[i].pos - m.center).length());
            m.radius = r > 0 ? r : 1;
        }
        return m;
    }
};

QImage loadTexture(const QDir &dir, const QString &name)
{
    if (name.isEmpty())
        return QImage();
    QString path = QDir::cleanPath(dir.filePath(QString(name).replace('\\', '/')));
    QImage img(path);
    if (img.isNull())                         // exported files often keep only the base name
        img = QImage(dir.filePath(QFileInfo(path).fileName()));
    return img.isNull() ? img : img.convertToFormat(QImage::Format_ARGB32);
}

// ---------------------------------------------------------------- OBJ

// map_Kd [-options args] file name with spaces
QString mtlMapFile(const QStringList &tokens)
{
    int i = 1;
    static const QHash<QString, int> optionArgs = {{"-bm", 1}, {"-blendu", 1}, {"-blendv", 1}, {"-boost", 1},
                                                   {"-cc", 1}, {"-clamp", 1}, {"-imfchan", 1}, {"-texres", 1},
                                                   {"-mm", 2}, {"-o", 3}, {"-s", 3}, {"-t", 3}};
    while (i < tokens.size() && tokens[i].startsWith('-') && optionArgs.contains(tokens[i])) {
        // -o/-s/-t take 1 to 3 numbers
        int n = optionArgs.value(tokens[i]);
        ++i;
        for (int k = 0; k < n && i < tokens.size(); ++k) {
            bool ok;
            tokens[i].toDouble(&ok);
            if (!ok && k > 0)
                break;
            ++i;
        }
    }
    return tokens.mid(i).join(' ');
}

void loadMtl(const QString &path, Builder &b)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QDir dir = QFileInfo(path).dir();
    QTextStream in(&f);
    int cur = -1;
    while (!in.atEnd()) {
        const QStringList t = in.readLine().trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (t.isEmpty() || t[0].startsWith('#'))
            continue;
        const QString key = t[0].toLower();
        if (key == "newmtl" && t.size() > 1) {
            cur = b.material(t.mid(1).join(' '));
        } else if (cur < 0) {
            continue;
        } else if (key == "kd" && t.size() >= 4) {
            b.materials[cur].color = QColor::fromRgbF(std::clamp(t[1].toFloat(), 0.f, 1.f), std::clamp(t[2].toFloat(), 0.f, 1.f),
                                                      std::clamp(t[3].toFloat(), 0.f, 1.f));
        } else if (key == "map_kd") {
            b.materials[cur].texture = loadTexture(dir, mtlMapFile(t));
        } else if (key == "map_bump" || key == "bump" || key == "norm" || key == "map_kn") {
            b.materials[cur].normalTexture = loadTexture(dir, mtlMapFile(t));
        }
    }
}

struct ObjKey {
    int v, t, n;
    bool operator==(const ObjKey &o) const { return v == o.v && t == o.t && n == o.n; }
};
struct ObjKeyHash {
    size_t operator()(const ObjKey &k) const { return size_t(k.v) * 73856093u ^ size_t(k.t) * 19349663u ^ size_t(k.n) * 83492791u; }
};

bool loadObj(const QString &path, Builder &b, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QDir dir = QFileInfo(path).dir();
    QVector<QVector3D> pos, nrm;
    QVector<QPointF> uv;
    std::unordered_map<ObjKey, quint32, ObjKeyHash> cache;
    int mat = -1;
    QVector<quint32> face;
    while (!f.atEnd()) {
        QByteArray line = f.readLine().trimmed();
        while (line.endsWith('\\') && !f.atEnd())                    // line continuation
            line = line.chopped(1) + ' ' + f.readLine().trimmed();
        if (line.isEmpty() || line[0] == '#')
            continue;
        const QList<QByteArray> t = line.simplified().split(' ');
        const QByteArray &key = t[0];
        if (key == "v" && t.size() >= 4) {
            pos.append(QVector3D(t[1].toFloat(), t[2].toFloat(), t[3].toFloat()));
        } else if (key == "vt" && t.size() >= 2) {
            uv.append(QPointF(t[1].toDouble(), t.size() >= 3 ? t[2].toDouble() : 0.0));
        } else if (key == "vn" && t.size() >= 4) {
            nrm.append(QVector3D(t[1].toFloat(), t[2].toFloat(), t[3].toFloat()));
        } else if (key == "usemtl") {
            mat = b.material(t.size() > 1 ? QString::fromUtf8(line.mid(7).trimmed()) : QStringLiteral("Default"));
        } else if (key == "mtllib" && t.size() > 1) {
            loadMtl(dir.filePath(QString::fromUtf8(line.mid(7).trimmed())), b);
        } else if (key == "f" && t.size() >= 4) {
            if (mat < 0)
                mat = b.material("Default");
            face.clear();
            for (int i = 1; i < t.size(); ++i) {
                const QList<QByteArray> parts = t[i].split('/');
                auto idx = [](const QList<QByteArray> &p, int k, int count) {
                    if (k >= p.size() || p[k].isEmpty())
                        return -1;
                    int i = p[k].toInt();
                    i = i < 0 ? count + i : i - 1;                     // negative = relative to the end
                    return i >= 0 && i < count ? i : -1;
                };
                ObjKey k{idx(parts, 0, int(pos.size())), idx(parts, 1, int(uv.size())), idx(parts, 2, int(nrm.size()))};
                if (k.v < 0)
                    continue;
                auto it = cache.find(k);
                if (it == cache.end()) {
                    Vertex v;
                    v.pos = pos[k.v];
                    if (k.t >= 0) {
                        v.u = float(uv[k.t].x());
                        v.v = float(uv[k.t].y());
                        b.hasUV = true;
                    }
                    if (k.n >= 0)
                        v.normal = nrm[k.n].normalized();
                    it = cache.emplace(k, b.add(v, k.v, k.n < 0)).first;
                }
                face.append(it->second);
            }
            for (int i = 1; i + 1 < face.size(); ++i)                  // fan triangulation
                b.triangle(mat, face[0], face[i], face[i + 1]);
        }
    }
    return true;
}

// ---------------------------------------------------------------- glTF 2.0

struct Gltf {
    QJsonObject json;
    QVector<QByteArray> buffers;
    QDir dir;
    QString error;

    QByteArray bufferView(int index, int *stride = nullptr) const
    {
        const QJsonObject bv = json["bufferViews"].toArray().at(index).toObject();
        const int buf = bv["buffer"].toInt();
        if (buf < 0 || buf >= buffers.size())
            return {};
        if (stride)
            *stride = bv["byteStride"].toInt(0);
        return buffers[buf].mid(bv["byteOffset"].toInt(0), bv["byteLength"].toInt());
    }

    // accessor values as floats, `comps` per element (normalized integers are mapped to 0..1 / -1..1)
    QVector<float> accessor(int index, int *comps = nullptr) const
    {
        QVector<float> out;
        const QJsonObject a = json["accessors"].toArray().at(index).toObject();
        static const QHash<QString, int> typeComps = {{"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4}, {"MAT4", 16}};
        const int n = typeComps.value(a["type"].toString(), 1);
        if (comps)
            *comps = n;
        const int count = a["count"].toInt();
        const int ct = a["componentType"].toInt();
        const bool normalized = a["normalized"].toBool();
        const int csize = (ct == 5120 || ct == 5121) ? 1 : (ct == 5122 || ct == 5123) ? 2 : 4;
        out.resize(qsizetype(count) * n);
        if (!a.contains("bufferView"))
            return out;                                            // all zeros (sparse-only accessors)
        int stride = 0;
        const QByteArray data = bufferView(a["bufferView"].toInt(), &stride);
        const int offset = a["byteOffset"].toInt(0);
        if (stride == 0)
            stride = csize * n;
        const uchar *base = reinterpret_cast<const uchar *>(data.constData());
        for (int i = 0; i < count; ++i)
            for (int c = 0; c < n; ++c) {
                const qsizetype at = offset + qsizetype(i) * stride + c * csize;
                if (at + csize > data.size())
                    return out;
                const uchar *p = base + at;
                float v = 0;
                switch (ct) {
                case 5120: v = normalized ? std::max(qint8(*p) / 127.f, -1.f) : qint8(*p); break;
                case 5121: v = normalized ? *p / 255.f : *p; break;
                case 5122: v = normalized ? std::max(qFromLittleEndian<qint16>(p) / 32767.f, -1.f) : qFromLittleEndian<qint16>(p); break;
                case 5123: v = normalized ? qFromLittleEndian<quint16>(p) / 65535.f : qFromLittleEndian<quint16>(p); break;
                case 5125: v = float(qFromLittleEndian<quint32>(p)); break;
                default: {
                    quint32 bits = qFromLittleEndian<quint32>(p);
                    std::memcpy(&v, &bits, 4);
                }
                }
                out[qsizetype(i) * n + c] = v;
            }
        return out;
    }

    QImage image(int textureIndex) const
    {
        if (textureIndex < 0)
            return QImage();
        const int src = json["textures"].toArray().at(textureIndex).toObject()["source"].toInt(-1);
        if (src < 0)
            return QImage();
        const QJsonObject img = json["images"].toArray().at(src).toObject();
        QImage out;
        if (img.contains("bufferView")) {
            out = QImage::fromData(bufferView(img["bufferView"].toInt()));
        } else {
            const QString uri = img["uri"].toString();
            if (uri.startsWith("data:"))
                out = QImage::fromData(QByteArray::fromBase64(uri.section(',', 1).toLatin1()));
            else
                return loadTexture(dir, QUrl::fromPercentEncoding(uri.toUtf8()));
        }
        return out.isNull() ? out : out.convertToFormat(QImage::Format_ARGB32);
    }
};

QMatrix4x4 nodeMatrix(const QJsonObject &node)
{
    QMatrix4x4 m;
    const QJsonArray mat = node["matrix"].toArray();
    if (mat.size() == 16) {
        float v[16];
        for (int i = 0; i < 16; ++i)
            v[i] = float(mat[i].toDouble());
        return QMatrix4x4(v).transposed();                       // glTF is column-major
    }
    const QJsonArray t = node["translation"].toArray(), r = node["rotation"].toArray(), s = node["scale"].toArray();
    if (t.size() == 3)
        m.translate(float(t[0].toDouble()), float(t[1].toDouble()), float(t[2].toDouble()));
    if (r.size() == 4)
        m.rotate(QQuaternion(float(r[3].toDouble()), float(r[0].toDouble()), float(r[1].toDouble()), float(r[2].toDouble())));
    if (s.size() == 3)
        m.scale(float(s[0].toDouble()), float(s[1].toDouble()), float(s[2].toDouble()));
    return m;
}

void gltfMesh(const Gltf &g, int meshIndex, const QMatrix4x4 &xf, Builder &b, QHash<int, int> &materialMap)
{
    const QJsonObject mesh = g.json["meshes"].toArray().at(meshIndex).toObject();
    const QMatrix3x3 nxf = xf.normalMatrix();
    const bool mirrored = xf.determinant() < 0;
    for (const QJsonValue &pv : mesh["primitives"].toArray()) {
        const QJsonObject prim = pv.toObject();
        const int mode = prim["mode"].toInt(4);
        if (mode != 4 && mode != 5 && mode != 6)
            continue;                                              // points / lines
        const QJsonObject attr = prim["attributes"].toObject();
        if (!attr.contains("POSITION"))
            continue;
        int pc = 0, nc = 0, tc = 0;
        const QVector<float> pos = g.accessor(attr["POSITION"].toInt(), &pc);
        const QVector<float> nrm = attr.contains("NORMAL") ? g.accessor(attr["NORMAL"].toInt(), &nc) : QVector<float>();
        const QVector<float> uv = attr.contains("TEXCOORD_0") ? g.accessor(attr["TEXCOORD_0"].toInt(), &tc) : QVector<float>();
        QVector<QPair<int, QVector<float>>> extraUv;               // TEXCOORD_1.. : (channel, data)
        if (!uv.isEmpty())
            b.uvChannel(QStringLiteral("UV 1"));
        for (int set = 1; attr.contains(QStringLiteral("TEXCOORD_%1").arg(set)); ++set) {
            int c = 0;
            const QVector<float> data = g.accessor(attr[QStringLiteral("TEXCOORD_%1").arg(set)].toInt(), &c);
            if (c >= 2)
                extraUv.append({b.uvChannel(QStringLiteral("UV %1").arg(set + 1)), data});
        }
        if (pc < 3)
            continue;
        const int count = int(pos.size() / pc);

        const int gm = prim["material"].toInt(-1);
        if (!materialMap.contains(gm)) {
            const QJsonObject mo = g.json["materials"].toArray().at(gm).toObject();
            const int idx = b.material(gm < 0 ? QStringLiteral("Default")
                                              : mo["name"].toString(QStringLiteral("Material %1").arg(gm)));
            const QJsonObject pbr = mo["pbrMetallicRoughness"].toObject();
            const QJsonArray f = pbr["baseColorFactor"].toArray();
            if (f.size() >= 3)
                b.materials[idx].color = QColor::fromRgbF(std::clamp(f[0].toDouble(), 0.0, 1.0), std::clamp(f[1].toDouble(), 0.0, 1.0),
                                                          std::clamp(f[2].toDouble(), 0.0, 1.0));
            b.materials[idx].texture = g.image(pbr["baseColorTexture"].toObject()["index"].toInt(-1));
            b.materials[idx].normalTexture = g.image(mo["normalTexture"].toObject()["index"].toInt(-1));
            materialMap.insert(gm, idx);
        }
        const int mat = materialMap.value(gm);

        const quint32 base = quint32(b.vertices.size());
        const bool hasNormals = nc >= 3 && nrm.size() / nc == count;
        const bool hasUv = tc >= 2 && uv.size() / tc == count;
        b.hasUV |= hasUv;
        for (int i = 0; i < count; ++i) {
            Vertex v;
            v.pos = xf.map(QVector3D(pos[i * pc], pos[i * pc + 1], pos[i * pc + 2]));
            if (hasNormals) {
                const QVector3D n(nrm[i * nc], nrm[i * nc + 1], nrm[i * nc + 2]);
                const float *m = nxf.constData();
                v.normal = QVector3D(m[0] * n.x() + m[3] * n.y() + m[6] * n.z(), m[1] * n.x() + m[4] * n.y() + m[7] * n.z(),
                                     m[2] * n.x() + m[5] * n.y() + m[8] * n.z()).normalized();
            }
            if (hasUv) {
                v.u = uv[i * tc];
                v.v = 1.0f - uv[i * tc + 1];                       // glTF UVs start at the top-left
            }
            const quint32 vi = b.add(v, -1, !hasNormals);
            for (const auto &[channel, data] : extraUv)
                if (qsizetype(i) * 2 + 1 < data.size())
                    b.setUV(channel, vi, data[i * 2], 1.0f - data[i * 2 + 1]);
        }

        QVector<quint32> idx;
        if (prim.contains("indices")) {
            for (float f : g.accessor(prim["indices"].toInt()))
                idx.append(quint32(f));
        } else {
            for (int i = 0; i < count; ++i)
                idx.append(quint32(i));
        }
        auto tri = [&](quint32 a, quint32 c, quint32 d) {
            if (a >= quint32(count) || c >= quint32(count) || d >= quint32(count))
                return;
            if (mirrored)
                std::swap(c, d);
            b.triangle(mat, base + a, base + c, base + d);
        };
        if (mode == 4) {
            for (int i = 0; i + 2 < idx.size(); i += 3)
                tri(idx[i], idx[i + 1], idx[i + 2]);
        } else if (mode == 5) {
            for (int i = 0; i + 2 < idx.size(); ++i)
                (i & 1) ? tri(idx[i + 1], idx[i], idx[i + 2]) : tri(idx[i], idx[i + 1], idx[i + 2]);
        } else {
            for (int i = 1; i + 1 < idx.size(); ++i)
                tri(idx[0], idx[i], idx[i + 1]);
        }
    }
}

void gltfNode(const Gltf &g, int nodeIndex, const QMatrix4x4 &parent, Builder &b, QHash<int, int> &materialMap, int depth)
{
    const QJsonObject node = g.json["nodes"].toArray().at(nodeIndex).toObject();
    if (depth > 64)
        return;                                                    // malformed cyclic hierarchy
    const QMatrix4x4 xf = parent * nodeMatrix(node);
    if (node.contains("mesh"))
        gltfMesh(g, node["mesh"].toInt(), xf, b, materialMap);
    for (const QJsonValue &c : node["children"].toArray())
        gltfNode(g, c.toInt(), xf, b, materialMap, depth + 1);
}

bool loadGltf(const QString &path, Builder &b, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QByteArray data = f.readAll();
    Gltf g;
    g.dir = QFileInfo(path).dir();
    QByteArray jsonChunk, binChunk;
    if (data.startsWith("glTF")) {
        // GLB: 12-byte header, then chunks (length, type, payload)
        qsizetype at = 12;
        while (at + 8 <= data.size()) {
            const quint32 len = qFromLittleEndian<quint32>(data.constData() + at);
            const quint32 type = qFromLittleEndian<quint32>(data.constData() + at + 4);
            const QByteArray chunk = data.mid(at + 8, len);
            if (type == 0x4E4F534A)
                jsonChunk = chunk;
            else if (type == 0x004E4942)
                binChunk = chunk;
            at += 8 + qsizetype(len);
        }
    } else {
        jsonChunk = data;
    }
    QJsonParseError pe;
    g.json = QJsonDocument::fromJson(jsonChunk, &pe).object();
    if (pe.error != QJsonParseError::NoError || g.json.isEmpty()) {
        if (error)
            *error = QObject::tr("Invalid glTF: %1").arg(pe.errorString());
        return false;
    }
    for (const QJsonValue &bv : g.json["buffers"].toArray()) {
        const QString uri = bv.toObject()["uri"].toString();
        if (uri.isEmpty()) {
            g.buffers.append(binChunk);
        } else if (uri.startsWith("data:")) {
            g.buffers.append(QByteArray::fromBase64(uri.section(',', 1).toLatin1()));
        } else {
            QFile bf(g.dir.filePath(QUrl::fromPercentEncoding(uri.toUtf8())));
            if (!bf.open(QIODevice::ReadOnly)) {
                if (error)
                    *error = QObject::tr("Missing buffer file %1").arg(uri);
                return false;
            }
            g.buffers.append(bf.readAll());
        }
    }

    QHash<int, int> materialMap;
    const QJsonArray scenes = g.json["scenes"].toArray();
    QJsonArray roots = scenes.at(g.json["scene"].toInt(0)).toObject()["nodes"].toArray();
    if (roots.isEmpty()) {
        // no scene: every node that isn't somebody's child
        const QJsonArray nodes = g.json["nodes"].toArray();
        QVector<bool> child(nodes.size());
        for (const QJsonValue &n : nodes)
            for (const QJsonValue &c : n.toObject()["children"].toArray())
                if (c.toInt() >= 0 && c.toInt() < child.size())
                    child[c.toInt()] = true;
        for (int i = 0; i < nodes.size(); ++i)
            if (!child[i])
                roots.append(i);
        if (nodes.isEmpty())
            for (int i = 0; i < g.json["meshes"].toArray().size(); ++i)
                gltfMesh(g, i, QMatrix4x4(), b, materialMap);
    }
    for (const QJsonValue &r : roots)
        gltfNode(g, r.toInt(), QMatrix4x4(), b, materialMap, 0);
    return true;
}

// ---------------------------------------------------------------- STL

bool loadStl(const QString &path, Builder &b, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QByteArray data = f.readAll();
    const int mat = b.material("Default");
    auto facet = [&](const QVector3D &a, const QVector3D &c, const QVector3D &d) {
        // STL normals are often missing or wrong: always use the geometric one (flat shading)
        Vertex v[3];
        v[0].pos = a;
        v[1].pos = c;
        v[2].pos = d;
        const QVector3D n = QVector3D::crossProduct(c - a, d - a).normalized();
        quint32 i[3];
        for (int k = 0; k < 3; ++k) {
            v[k].normal = n;
            i[k] = b.add(v[k], -1, false);
        }
        b.triangle(mat, i[0], i[1], i[2]);
    };
    const quint32 binCount = data.size() >= 84 ? qFromLittleEndian<quint32>(data.constData() + 80) : 0;
    if (data.size() >= 84 && qsizetype(84) + qsizetype(binCount) * 50 == data.size()) {
        auto vec = [&](qsizetype at) {
            float v[3];
            for (int k = 0; k < 3; ++k) {
                quint32 bits = qFromLittleEndian<quint32>(data.constData() + at + 4 * k);
                std::memcpy(&v[k], &bits, 4);
            }
            return QVector3D(v[0], v[1], v[2]);
        };
        for (quint32 i = 0; i < binCount; ++i) {
            const qsizetype at = 84 + qsizetype(i) * 50;
            facet(vec(at + 12), vec(at + 24), vec(at + 36));
        }
        return true;
    }
    QVector<QVector3D> corners;
    for (const QByteArray &line : data.split('\n')) {
        const QList<QByteArray> t = line.simplified().split(' ');
        if (t.size() >= 4 && t[0] == "vertex") {
            corners.append(QVector3D(t[1].toFloat(), t[2].toFloat(), t[3].toFloat()));
            if (corners.size() == 3) {
                facet(corners[0], corners[1], corners[2]);
                corners.clear();
            }
        } else if (t[0] == "endloop") {
            corners.clear();
        }
    }
    return true;
}

// ---------------------------------------------------------------- PLY

bool loadPly(const QString &path, Builder &b, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (f.readLine().trimmed() != "ply")
        return fail(QObject::tr("Not a PLY file"));
    struct Prop { QByteArray name, type, countType; bool list = false; };
    struct Element { QByteArray name; qint64 count = 0; QVector<Prop> props; };
    QVector<Element> elements;
    QByteArray format;
    while (!f.atEnd()) {
        const QList<QByteArray> t = f.readLine().simplified().split(' ');
        if (t[0] == "end_header")
            break;
        if (t[0] == "format" && t.size() > 1)
            format = t[1];
        else if (t[0] == "element" && t.size() > 2)
            elements.append({t[1], t[2].toLongLong(), {}});
        else if (t[0] == "property" && !elements.isEmpty()) {
            Prop p;
            if (t.size() >= 5 && t[1] == "list") {
                p.list = true;
                p.countType = t[2];
                p.type = t[3];
                p.name = t[4];
            } else if (t.size() >= 3) {
                p.type = t[1];
                p.name = t[2];
            }
            elements.last().props.append(p);
        }
    }
    const bool ascii = format == "ascii";
    const bool bigEndian = format == "binary_big_endian";
    if (!ascii && !bigEndian && format != "binary_little_endian")
        return fail(QObject::tr("Unsupported PLY format"));

    QByteArray body = f.readAll();
    qsizetype at = 0;
    QList<QByteArray> words;
    int word = 0;
    bool ok = true;
    auto typeSize = [](const QByteArray &t) {
        if (t == "char" || t == "uchar" || t == "int8" || t == "uint8") return 1;
        if (t == "short" || t == "ushort" || t == "int16" || t == "uint16") return 2;
        if (t == "double" || t == "float64") return 8;
        return 4;
    };
    auto read = [&](const QByteArray &type) -> double {
        if (ascii) {
            while (word >= words.size()) {
                if (at >= body.size()) {
                    ok = false;
                    return 0;
                }
                qsizetype nl = body.indexOf('\n', at);
                if (nl < 0)
                    nl = body.size();
                words = body.mid(at, nl - at).simplified().split(' ');
                if (words.size() == 1 && words[0].isEmpty())
                    words.clear();
                word = 0;
                at = nl + 1;
            }
            return words[word++].toDouble();
        }
        const int n = typeSize(type);
        if (at + n > body.size()) {
            ok = false;
            return 0;
        }
        uchar raw[8];
        std::memcpy(raw, body.constData() + at, n);
        at += n;
        if (bigEndian)
            std::reverse(raw, raw + n);
        if (type == "char" || type == "int8") return qint8(raw[0]);
        if (type == "uchar" || type == "uint8") return raw[0];
        if (type == "short" || type == "int16") { qint16 v; std::memcpy(&v, raw, 2); return v; }
        if (type == "ushort" || type == "uint16") { quint16 v; std::memcpy(&v, raw, 2); return v; }
        if (type == "int" || type == "int32") { qint32 v; std::memcpy(&v, raw, 4); return v; }
        if (type == "uint" || type == "uint32") { quint32 v; std::memcpy(&v, raw, 4); return v; }
        if (type == "double" || type == "float64") { double v; std::memcpy(&v, raw, 8); return v; }
        float v;
        std::memcpy(&v, raw, 4);
        return v;
    };

    const int mat = b.material("Default");
    quint32 vertexBase = 0;
    bool haveNormals = false;
    for (const Element &e : elements) {
        if (e.name == "vertex") {
            int ix = -1, iy = -1, iz = -1, inx = -1, iny = -1, inz = -1, iu = -1, iv = -1;
            for (int i = 0; i < e.props.size(); ++i) {
                const QByteArray &n = e.props[i].name;
                if (n == "x") ix = i; else if (n == "y") iy = i; else if (n == "z") iz = i;
                else if (n == "nx") inx = i; else if (n == "ny") iny = i; else if (n == "nz") inz = i;
                else if (n == "u" || n == "s" || n == "texture_u" || n == "texture_s") iu = i;
                else if (n == "v" || n == "t" || n == "texture_v" || n == "texture_t") iv = i;
            }
            haveNormals = inx >= 0 && iny >= 0 && inz >= 0;
            b.hasUV |= iu >= 0 && iv >= 0;
            vertexBase = quint32(b.vertices.size());
            QVector<double> vals(e.props.size());
            for (qint64 i = 0; i < e.count && ok; ++i) {
                for (int p = 0; p < e.props.size(); ++p) {
                    if (e.props[p].list) {
                        const int n = int(read(e.props[p].countType));
                        for (int k = 0; k < n; ++k)
                            read(e.props[p].type);
                    } else {
                        vals[p] = read(e.props[p].type);
                    }
                }
                auto val = [&](int i) { return i >= 0 ? float(vals[i]) : 0.f; };
                Vertex v;
                v.pos = QVector3D(val(ix), val(iy), val(iz));
                v.normal = QVector3D(val(inx), val(iny), val(inz)).normalized();
                v.u = val(iu);
                v.v = val(iv);
                b.add(v, -1, !haveNormals);
            }
        } else {
            const bool isFace = e.name == "face";
            const quint32 vcount = quint32(b.vertices.size()) - vertexBase;
            QVector<quint32> poly;
            for (qint64 i = 0; i < e.count && ok; ++i)
                for (const Prop &p : e.props) {
                    if (!p.list) {
                        read(p.type);
                        continue;
                    }
                    const int n = int(read(p.countType));
                    poly.clear();
                    for (int k = 0; k < n; ++k)
                        poly.append(quint32(read(p.type)));
                    if (!isFace || (p.name != "vertex_indices" && p.name != "vertex_index"))
                        continue;
                    for (int k = 1; k + 1 < poly.size(); ++k)
                        if (poly[0] < vcount && poly[k] < vcount && poly[k + 1] < vcount)
                            b.triangle(mat, vertexBase + poly[0], vertexBase + poly[k], vertexBase + poly[k + 1]);
                }
        }
    }
    return ok || !b.tris[mat].isEmpty() ? true : fail(QObject::tr("Truncated PLY file"));
}

// ---------------------------------------------------------------- FBX (binary 7.x and ASCII 6/7)

struct FbxProp {
    char type = 0;                // binary type code; ASCII: 'S' string, 'D' number, 'L' integer
    double num = 0;
    qint64 i = 0;
    QByteArray str;               // strings and raw data
    QVector<double> arr;

    bool numeric() const { return type != 'S' && type != 'R' && arr.isEmpty(); }
};

struct FbxNode {
    QByteArray name;
    QVector<FbxProp> props;
    QVector<FbxNode> children;

    const FbxNode *child(const char *n) const
    {
        for (const FbxNode &c : children)
            if (c.name == n)
                return &c;
        return nullptr;
    }
    QByteArray string(const char *n) const
    {
        const FbxNode *c = child(n);
        return c && !c->props.isEmpty() ? c->props[0].str : QByteArray();
    }
    // a child's numbers: the array property (binary, ASCII "*N { a: }") or its plain numeric values
    QVector<double> numbers(const char *n) const
    {
        const FbxNode *c = child(n);
        QVector<double> out;
        if (!c)
            return out;
        for (const FbxProp &p : c->props) {
            if (!p.arr.isEmpty())
                return p.arr;
            if (p.numeric())
                out.append(p.num);
        }
        return out;
    }
    // object key: 64-bit id (FBX 7) or the "Class::Name" string (FBX 6)
    QByteArray key() const
    {
        if (props.isEmpty())
            return QByteArray();
        return props[0].type == 'S' ? props[0].str : QByteArray::number(props[0].i);
    }
};

// ---- binary

bool fbxBinaryNode(const QByteArray &d, qsizetype &at, bool wide, FbxNode &out, bool &end)
{
    const qsizetype head = wide ? 25 : 13;
    if (at + head > d.size())
        return false;
    const char *p = d.constData() + at;
    const quint64 endOffset = wide ? qFromLittleEndian<quint64>(p) : qFromLittleEndian<quint32>(p);
    const quint64 numProps = wide ? qFromLittleEndian<quint64>(p + 8) : qFromLittleEndian<quint32>(p + 4);
    const int nameLen = uchar(p[wide ? 24 : 12]);
    end = endOffset == 0;
    if (end) {
        at += head;
        return true;
    }
    if (endOffset > quint64(d.size()) || at + head + nameLen > d.size())
        return false;
    out.name = d.mid(at + head, nameLen);
    at += head + nameLen;
    for (quint64 i = 0; i < numProps; ++i) {
        if (at >= d.size())
            return false;
        FbxProp prop;
        prop.type = d[at++];
        auto need = [&](qsizetype n) { return at + n <= d.size(); };
        const char *q = d.constData() + at;
        switch (prop.type) {
        case 'Y': if (!need(2)) return false; prop.i = qFromLittleEndian<qint16>(q); at += 2; break;
        case 'C': if (!need(1)) return false; prop.i = q[0] != 0; at += 1; break;
        case 'I': if (!need(4)) return false; prop.i = qFromLittleEndian<qint32>(q); at += 4; break;
        case 'L': if (!need(8)) return false; prop.i = qFromLittleEndian<qint64>(q); at += 8; break;
        case 'F': {
            if (!need(4)) return false;
            quint32 bits = qFromLittleEndian<quint32>(q);
            float f;
            std::memcpy(&f, &bits, 4);
            prop.num = f;
            at += 4;
            break;
        }
        case 'D': {
            if (!need(8)) return false;
            quint64 bits = qFromLittleEndian<quint64>(q);
            std::memcpy(&prop.num, &bits, 8);
            at += 8;
            break;
        }
        case 'S':
        case 'R': {
            if (!need(4)) return false;
            const quint32 len = qFromLittleEndian<quint32>(q);
            if (!need(4 + qsizetype(len))) return false;
            prop.str = d.mid(at + 4, len);
            at += 4 + qsizetype(len);
            break;
        }
        case 'f': case 'd': case 'i': case 'l': case 'b': {
            if (!need(12)) return false;
            const quint32 count = qFromLittleEndian<quint32>(q);
            const quint32 encoding = qFromLittleEndian<quint32>(q + 4);
            const quint32 clen = qFromLittleEndian<quint32>(q + 8);
            if (!need(12 + qsizetype(clen))) return false;
            const int esize = prop.type == 'b' ? 1 : (prop.type == 'f' || prop.type == 'i') ? 4 : 8;
            QByteArray raw;
            if (encoding == 1) {
                raw.resize(qsizetype(count) * esize);
                uLongf rlen = uLongf(raw.size());
                if (uncompress(reinterpret_cast<Bytef *>(raw.data()), &rlen, reinterpret_cast<const Bytef *>(q + 12), clen) != Z_OK ||
                    rlen != uLongf(raw.size()))
                    return false;
            } else {
                raw = d.mid(at + 12, clen);
                if (raw.size() < qsizetype(count) * esize)
                    return false;
            }
            at += 12 + qsizetype(clen);
            prop.arr.resize(count);
            const char *r = raw.constData();
            for (quint32 k = 0; k < count; ++k) {
                const char *e = r + qsizetype(k) * esize;
                switch (prop.type) {
                case 'b': prop.arr[k] = e[0] != 0; break;
                case 'i': prop.arr[k] = qFromLittleEndian<qint32>(e); break;
                case 'l': prop.arr[k] = double(qFromLittleEndian<qint64>(e)); break;
                case 'f': { quint32 bits = qFromLittleEndian<quint32>(e); float f; std::memcpy(&f, &bits, 4); prop.arr[k] = f; break; }
                default: { quint64 bits = qFromLittleEndian<quint64>(e); double v; std::memcpy(&v, &bits, 8); prop.arr[k] = v; }
                }
            }
            break;
        }
        default:
            return false;
        }
        if (prop.type == 'Y' || prop.type == 'C' || prop.type == 'I' || prop.type == 'L')
            prop.num = double(prop.i);
        else if (prop.type == 'F' || prop.type == 'D')
            prop.i = qint64(prop.num);
        out.props.append(prop);
    }
    while (at < qsizetype(endOffset)) {
        FbxNode c;
        bool childEnd = false;
        if (!fbxBinaryNode(d, at, wide, c, childEnd))
            return false;
        if (childEnd)
            break;
        out.children.append(std::move(c));
    }
    at = qsizetype(endOffset);
    return true;
}

bool fbxParseBinary(const QByteArray &d, FbxNode &root)
{
    if (d.size() < 27)
        return false;
    const quint32 version = qFromLittleEndian<quint32>(d.constData() + 23);
    qsizetype at = 27;
    for (;;) {
        FbxNode n;
        bool end = false;
        if (!fbxBinaryNode(d, at, version >= 7500, n, end))
            return !root.children.isEmpty();          // tolerate trailing footer data
        if (end)
            return true;
        root.children.append(std::move(n));
    }
}

// ---- ASCII

struct FbxAscii {
    const QByteArray &d;
    qsizetype at = 0;

    void skipSpace(bool newlines)
    {
        while (at < d.size()) {
            const char c = d[at];
            if (c == ';' && newlines) {                  // comment to end of line
                while (at < d.size() && d[at] != '\n')
                    ++at;
            } else if (c == ' ' || c == '\t' || c == '\r' || (newlines && c == '\n')) {
                ++at;
            } else {
                break;
            }
        }
    }

    void block(FbxNode &parent)
    {
        for (;;) {
            skipSpace(true);
            if (at >= d.size())
                return;
            if (d[at] == '}') {
                ++at;
                return;
            }
            const qsizetype start = at;
            while (at < d.size() && d[at] != ':' && d[at] != '\n' && d[at] != '{' && d[at] != '}')
                ++at;
            if (at >= d.size() || d[at] != ':') {      // junk line
                if (at < d.size() && d[at] != '}')
                    ++at;
                continue;
            }
            FbxNode n;
            n.name = d.mid(start, at - start).trimmed();
            ++at;
            values(n);
            // "Name: *N { a: 1,2,3 }" stores its numbers in a child "a"
            if (const FbxNode *a = n.child("a")) {
                FbxProp arr;
                arr.type = 'd';
                for (const FbxProp &p : a->props)
                    if (p.numeric())
                        arr.arr.append(p.num);
                n.props = {arr};
                n.children.clear();
            }
            parent.children.append(std::move(n));
        }
    }

    void values(FbxNode &n)
    {
        bool continued = false;
        for (;;) {
            skipSpace(false);
            if (at >= d.size())
                return;
            const char c = d[at];
            if (c == '\n') {
                if (!continued)                          // a trailing comma continues on the next line
                    return;
                ++at;
                continue;
            }
            if (c == '{') {
                ++at;
                block(n);
                return;
            }
            if (c == '}')
                return;
            if (c == ',') {
                continued = true;
                ++at;
                continue;
            }
            continued = false;
            FbxProp p;
            if (c == '"') {
                const qsizetype s = ++at;
                while (at < d.size() && d[at] != '"')
                    ++at;
                p.type = 'S';
                p.str = d.mid(s, at - s);
                ++at;
            } else {
                const qsizetype s = at;
                while (at < d.size() && !QByteArrayView(",\n{} \t\r").contains(d[at]))
                    ++at;
                const QByteArray tok = d.mid(s, at - s);
                if (tok.startsWith('*'))                 // array length marker
                    continue;
                bool okI = false, okD = false;
                p.i = tok.toLongLong(&okI);
                p.num = okI ? double(p.i) : tok.toDouble(&okD);
                if (okI || okD) {
                    p.type = okI ? 'L' : 'D';
                    if (okD)
                        p.i = qint64(p.num);
                } else {
                    p.type = 'S';
                    p.str = tok;
                }
            }
            n.props.append(p);
        }
    }
};

// "Name\0\1Class" (binary) or "Class::Name" (ASCII)
QString fbxName(const QByteArray &raw)
{
    const int z = raw.indexOf('\0');
    if (z >= 0)
        return QString::fromUtf8(raw.left(z));
    const int c = raw.indexOf("::");
    return QString::fromUtf8(c >= 0 ? raw.mid(c + 2) : raw);
}

// Properties70 "P" (FBX 7) or Properties60 "Property" (FBX 6) values
QVector<double> fbxProperty(const FbxNode &obj, const char *name)
{
    const FbxNode *props = obj.child("Properties70");
    if (!props)
        props = obj.child("Properties60");
    QVector<double> out;
    if (!props)
        return out;
    for (const FbxNode &p : props->children) {
        if (p.props.isEmpty() || p.props[0].str != name)
            continue;
        for (int i = 1; i < p.props.size(); ++i)
            if (p.props[i].numeric())
                out.append(p.props[i].num);
        return out;
    }
    return out;
}

QVector3D fbxVec(const FbxNode &obj, const char *name, const QVector3D &def)
{
    const QVector<double> v = fbxProperty(obj, name);
    return v.size() >= 3 ? QVector3D(float(v[0]), float(v[1]), float(v[2])) : def;
}

QMatrix4x4 fbxEuler(const QVector3D &deg)
{
    QMatrix4x4 m;                                        // XYZ order: X is applied first
    m.rotate(deg.z(), 0, 0, 1);
    m.rotate(deg.y(), 0, 1, 0);
    m.rotate(deg.x(), 1, 0, 0);
    return m;
}

QMatrix4x4 fbxLocal(const FbxNode &model)
{
    auto tr = [](const QVector3D &v) { QMatrix4x4 m; m.translate(v); return m; };
    const QVector3D rp = fbxVec(model, "RotationPivot", {}), sp = fbxVec(model, "ScalingPivot", {});
    QMatrix4x4 s;
    s.scale(fbxVec(model, "Lcl Scaling", QVector3D(1, 1, 1)));
    return tr(fbxVec(model, "Lcl Translation", {})) * tr(fbxVec(model, "RotationOffset", {})) * tr(rp) *
           fbxEuler(fbxVec(model, "PreRotation", {})) * fbxEuler(fbxVec(model, "Lcl Rotation", {})) *
           fbxEuler(fbxVec(model, "PostRotation", {})).inverted() * tr(-rp) * tr(fbxVec(model, "ScalingOffset", {})) * tr(sp) *
           s * tr(-sp);
}

QMatrix4x4 fbxGeometric(const FbxNode &model)
{
    QMatrix4x4 m;
    m.translate(fbxVec(model, "GeometricTranslation", {}));
    m *= fbxEuler(fbxVec(model, "GeometricRotation", {}));
    m.scale(fbxVec(model, "GeometricScaling", QVector3D(1, 1, 1)));
    return m;
}

struct FbxScene {
    QHash<QByteArray, const FbxNode *> objects;
    QHash<QByteArray, QVector<QByteArray>> parents;            // child -> parents (OO)
    QHash<QByteArray, QVector<QByteArray>> children;           // parent -> children (OO, in file order)
    QHash<QByteArray, QVector<QPair<QByteArray, QByteArray>>> propChildren;   // parent -> (child, property)
    QDir dir;

    bool isA(const QByteArray &key, const char *cls) const
    {
        const FbxNode *n = objects.value(key);
        return n && n->name == cls;
    }

    QMatrix4x4 world(const QByteArray &model, int depth = 0) const
    {
        const FbxNode *n = objects.value(model);
        if (!n || depth > 64)
            return QMatrix4x4();
        QMatrix4x4 m = fbxLocal(*n);
        for (const QByteArray &p : parents.value(model))
            if (isA(p, "Model"))
                return world(p, depth + 1) * m;
        return m;
    }

    QImage texture(const QByteArray &texKey) const
    {
        const FbxNode *tex = objects.value(texKey);
        if (!tex)
            return QImage();
        // embedded image data lives in a connected Video object
        for (const QByteArray &c : children.value(texKey)) {
            const FbxNode *video = objects.value(c);
            if (!video || video->name != "Video")
                continue;
            if (const FbxNode *content = video->child("Content"); content && !content->props.isEmpty()) {
                QByteArray data = content->props[0].str;
                if (content->props[0].type == 'S')
                    data = QByteArray::fromBase64(data);
                QImage img = QImage::fromData(data);
                if (!img.isNull())
                    return img.convertToFormat(QImage::Format_ARGB32);
            }
        }
        for (const char *field : {"RelativeFilename", "FileName"}) {
            QImage img = loadTexture(dir, QString::fromUtf8(tex->string(field)));
            if (!img.isNull())
                return img;
        }
        return QImage();
    }
};

// value index into a LayerElement's data for one polygon corner
int fbxLayerIndex(const FbxNode &le, const QVector<double> &index, int corner, int controlPoint, int polygon)
{
    const QByteArray mapping = le.string("MappingInformationType");
    const QByteArray ref = le.string("ReferenceInformationType");
    int i = corner;
    if (mapping == "ByVertice" || mapping == "ByVertex" || mapping == "ByControlPoint")
        i = controlPoint;
    else if (mapping == "ByPolygon")
        i = polygon;
    else if (mapping == "AllSame")
        i = 0;
    if (ref.startsWith("Index"))                       // "IndexToDirect" or "Index"
        i = i >= 0 && i < index.size() ? int(index[i]) : -1;
    return i;
}

void fbxGeometry(const FbxScene &scene, const FbxNode &geo, const QByteArray &modelKey, const QMatrix4x4 &xf, Builder &b)
{
    const QVector<double> pos = geo.numbers("Vertices");
    const QVector<double> poly = geo.numbers("PolygonVertexIndex");
    if (pos.size() < 9 || poly.size() < 3)
        return;
    const FbxNode *nle = geo.child("LayerElementNormal");
    struct UvLayer { const FbxNode *node; QVector<double> uv, index; int channel; };
    QVector<UvLayer> uvLayers;
    for (const FbxNode &c : geo.children)
        if (c.name == "LayerElementUV") {
            const QByteArray name = c.string("Name");
            uvLayers.append({&c, c.numbers("UV"), c.numbers("UVIndex"),
                             b.uvChannel(name.isEmpty() ? QStringLiteral("UV %1").arg(uvLayers.size() + 1) : QString::fromUtf8(name))});
        }
    const FbxNode *mle = geo.child("LayerElementMaterial");
    const QVector<double> normals = nle ? nle->numbers("Normals") : QVector<double>();
    const QVector<double> normalIdx = nle ? nle->numbers("NormalsIndex") : QVector<double>();
    const QVector<double> mats = mle ? mle->numbers("Materials") : QVector<double>();
    const bool mlAll = mle && mle->string("MappingInformationType") == "AllSame";

    // the model's materials, in connection order (polygon material indices refer to this list)
    QVector<int> materials;
    for (const QByteArray &c : scene.children.value(modelKey)) {
        if (!scene.isA(c, "Material"))
            continue;
        const FbxNode *mo = scene.objects.value(c);
        const int idx = b.material(fbxName(mo->props.value(1).str.isEmpty() ? mo->props.value(0).str : mo->props[1].str));
        Material &m = b.materials[idx];
        QVector3D col = fbxVec(*mo, "DiffuseColor", fbxVec(*mo, "Diffuse", QVector3D(0.8f, 0.8f, 0.8f)));
        m.color = QColor::fromRgbF(std::clamp(col.x(), 0.f, 1.f), std::clamp(col.y(), 0.f, 1.f), std::clamp(col.z(), 0.f, 1.f));
        for (const auto &[tex, prop] : scene.propChildren.value(c)) {
            if (!scene.isA(tex, "Texture"))
                continue;
            if (m.texture.isNull() && (prop == "DiffuseColor" || prop == "Diffuse" || prop.endsWith("baseColor") || prop == "Maya|base_color"))
                m.texture = scene.texture(tex);
            else if (m.normalTexture.isNull() && (prop == "NormalMap" || prop == "Bump" || prop.endsWith("normalCamera")))
                m.normalTexture = scene.texture(tex);
        }
        materials.append(idx);
    }
    if (materials.isEmpty())
        materials.append(b.material("Default"));

    const QMatrix3x3 nxf = xf.normalMatrix();
    const bool mirrored = xf.determinant() < 0;
    const int weldBase = int(b.vertices.size());              // weld ids: control points, unique per geometry
    const int cpCount = int(pos.size() / 3);
    QVector<quint32> face;
    int polygon = 0, corner = 0;
    for (int i = 0; i < poly.size(); ++i, ++corner) {
        int cp = int(poly[i]);
        const bool last = cp < 0;
        if (last)
            cp = ~cp;                                         // negative index ends the polygon
        if (cp >= cpCount)
            continue;
        Vertex v;
        v.pos = xf.map(QVector3D(float(pos[cp * 3]), float(pos[cp * 3 + 1]), float(pos[cp * 3 + 2])));
        bool hasNormal = false;
        if (nle) {
            const int ni = fbxLayerIndex(*nle, normalIdx, corner, cp, polygon);
            if (ni >= 0 && ni * 3 + 2 < normals.size()) {
                const QVector3D n{float(normals[ni * 3]), float(normals[ni * 3 + 1]), float(normals[ni * 3 + 2])};
                const float *m = nxf.constData();
                v.normal = QVector3D(m[0] * n.x() + m[3] * n.y() + m[6] * n.z(), m[1] * n.x() + m[4] * n.y() + m[7] * n.z(),
                                     m[2] * n.x() + m[5] * n.y() + m[8] * n.z()).normalized();
                hasNormal = true;
            }
        }
        const quint32 vi = b.add(v, weldBase + cp, !hasNormal);
        for (const UvLayer &l : uvLayers) {
            const int ti = fbxLayerIndex(*l.node, l.index, corner, cp, polygon);
            if (ti >= 0 && ti * 2 + 1 < l.uv.size())
                b.setUV(l.channel, vi, float(l.uv[ti * 2]), float(l.uv[ti * 2 + 1]));
        }
        face.append(vi);
        if (last) {
            const int mi = mats.isEmpty() ? 0 : int(mats.value(mlAll ? 0 : polygon, 0));
            const int mat = materials.value(mi, materials[0]);
            for (int k = 1; k + 1 < face.size(); ++k)
                mirrored ? b.triangle(mat, face[0], face[k + 1], face[k]) : b.triangle(mat, face[0], face[k], face[k + 1]);
            face.clear();
            ++polygon;
        }
    }
}

bool loadFbx(const QString &path, Builder &b, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QByteArray data = f.readAll();
    FbxNode root;
    if (data.startsWith("Kaydara FBX Binary")) {
        if (!fbxParseBinary(data, root)) {
            if (error)
                *error = QObject::tr("Damaged or unsupported binary FBX file");
            return false;
        }
    } else {
        FbxAscii parser{data};
        parser.block(root);
    }
    const FbxNode *objects = root.child("Objects");
    if (!objects) {
        if (error)
            *error = QObject::tr("Not an FBX file");
        return false;
    }
    FbxScene scene;
    scene.dir = QFileInfo(path).dir();
    for (const FbxNode &o : objects->children)
        scene.objects.insert(o.key(), &o);
    if (const FbxNode *conns = root.child("Connections"))
        for (const FbxNode &c : conns->children) {
            if (c.props.size() < 3)
                continue;
            FbxNode child, parent;                               // reuse key() for ids and names alike
            child.props = {c.props[1]};
            parent.props = {c.props[2]};
            if (c.props[0].str == "OP" && c.props.size() >= 4)
                scene.propChildren[parent.key()].append({child.key(), c.props[3].str});
            else
                scene.parents[child.key()].append(parent.key());
            scene.children[parent.key()].append(child.key());
        }

    // file axes -> Y up, right handed
    QMatrix4x4 axes;
    if (const FbxNode *gs = root.child("GlobalSettings")) {
        const QVector<double> up = fbxProperty(*gs, "UpAxis"), upSign = fbxProperty(*gs, "UpAxisSign");
        const QVector<double> co = fbxProperty(*gs, "CoordAxis"), coSign = fbxProperty(*gs, "CoordAxisSign");
        if (!up.isEmpty() && !co.isEmpty() && int(up[0]) != int(co[0]) && int(up[0]) >= 0 && int(up[0]) < 3 && int(co[0]) >= 0 &&
            int(co[0]) < 3) {
            QVector3D right, upv;
            right[int(co[0])] = coSign.isEmpty() ? 1.f : float(coSign[0]);
            upv[int(up[0])] = upSign.isEmpty() ? 1.f : float(upSign[0]);
            const QVector3D front = QVector3D::crossProduct(right, upv);
            axes = QMatrix4x4(right.x(), right.y(), right.z(), 0, upv.x(), upv.y(), upv.z(), 0, front.x(), front.y(), front.z(), 0,
                              0, 0, 0, 1);
        }
    }

    for (auto it = scene.objects.cbegin(); it != scene.objects.cend(); ++it) {
        const FbxNode &o = *it.value();
        if (o.name == "Geometry") {
            for (const QByteArray &p : scene.parents.value(it.key()))
                if (scene.isA(p, "Model")) {
                    const FbxNode &model = *scene.objects.value(p);
                    fbxGeometry(scene, o, p, axes * scene.world(p) * fbxGeometric(model), b);
                }
        } else if (o.name == "Model" && o.child("Vertices")) {
            fbxGeometry(scene, o, it.key(), axes * scene.world(it.key()), b);   // FBX 6: geometry inside the model
        }
    }
    return true;
}

} // namespace

QStringList modelSuffixes() { return {"obj", "gltf", "glb", "fbx", "stl", "ply"}; }

QString modelFileFilter()
{
    return QObject::tr("3D models (*.obj *.gltf *.glb *.stl *.ply);;Wavefront OBJ (*.obj);;glTF 2.0 (*.gltf *.glb);;"
                       "STL (*.stl);;PLY (*.ply);;All files (*)");
}

void Model::selectUVSet(int set)
{
    if (set < 0 || set >= uvData.size())
        return;
    uvSet = set;
    const QVector<QVector2D> &uv = uvData[set];
    for (int i = 0; i < vertices.size() && i < uv.size(); ++i) {
        vertices[i].u = uv[i].x();
        vertices[i].v = uv[i].y();
    }
}

Model loadModel(const QString &path, QString *error)
{
    Builder b;
    const QString suffix = QFileInfo(path).suffix().toLower();
    bool ok;
    if (suffix == "obj")
        ok = loadObj(path, b, error);
    else if (suffix == "gltf" || suffix == "glb")
        ok = loadGltf(path, b, error);
    else if (suffix == "fbx")
        ok = loadFbx(path, b, error);
    else if (suffix == "stl")
        ok = loadStl(path, b, error);
    else if (suffix == "ply")
        ok = loadPly(path, b, error);
    else {
        if (error)
            *error = QObject::tr("Unsupported model format \"%1\"").arg(suffix);
        return Model();
    }
    if (!ok)
        return Model();
    Model m = b.finish(path);
    if (m.isEmpty() && error)
        *error = QObject::tr("The file contains no triangles");
    return m;
}

} // namespace ModelView
