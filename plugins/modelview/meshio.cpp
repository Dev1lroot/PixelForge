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
            b.add(v, -1, !hasNormals);
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

} // namespace

QStringList modelSuffixes() { return {"obj", "gltf", "glb", "stl", "ply"}; }

QString modelFileFilter()
{
    return QObject::tr("3D models (*.obj *.gltf *.glb *.stl *.ply);;Wavefront OBJ (*.obj);;glTF 2.0 (*.gltf *.glb);;"
                       "STL (*.stl);;PLY (*.ply);;All files (*)");
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
