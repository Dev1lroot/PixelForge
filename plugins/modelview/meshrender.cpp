#include "meshrender.h"
#include "parallel.h"

#include <QMatrix4x4>
#include <QVector4D>
#include <cmath>
#include <limits>

namespace ModelView {

namespace {

struct Rgba { float r, g, b, a; };

Rgba sample(const QImage &img, float u, float v, bool pixelated)
{
    const int w = img.width(), h = img.height();
    // repeat wrapping; v = 0 is the bottom row
    u -= std::floor(u);
    v = 1.0f - (v - std::floor(v));
    auto px = [&](int x, int y) {
        x %= w; if (x < 0) x += w;
        y %= h; if (y < 0) y += h;
        return reinterpret_cast<const QRgb *>(img.constScanLine(y))[x];
    };
    float fx = u * w, fy = v * h;
    if (pixelated) {
        const QRgb c = px(std::min(int(fx), w - 1), std::min(int(fy), h - 1));
        return {qRed(c) / 255.f, qGreen(c) / 255.f, qBlue(c) / 255.f, qAlpha(c) / 255.f};
    }
    fx -= 0.5f;
    fy -= 0.5f;
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const float tx = fx - x0, ty = fy - y0;
    const QRgb c[4] = {px(x0, y0), px(x0 + 1, y0), px(x0, y0 + 1), px(x0 + 1, y0 + 1)};
    auto ch = [&](int sh) {
        const float top = ((c[0] >> sh) & 255) * (1 - tx) + ((c[1] >> sh) & 255) * tx;
        const float bottom = ((c[2] >> sh) & 255) * (1 - tx) + ((c[3] >> sh) & 255) * tx;
        return (top * (1 - ty) + bottom * ty) / 255.f;
    };
    return {ch(16), ch(8), ch(0), ch(24)};
}

struct ClipVert {
    QVector4D clip;
    QVector3D n;                  // view space
    float u, v;
};

struct Tri {
    float x[3], y[3], z[3], iw[3];
    QVector3D nw[3];              // attributes divided by w for perspective-correct interpolation
    float uw[3], vw[3];
    QVector3D t, b;               // view-space tangent frame (normal-mapped materials only)
    float area;
    float edgeScale[3];           // 1 / edge length: turns edge functions into pixel distances
    int minX, maxX, minY, maxY;
    int material;
    bool back;
};

ClipVert lerp(const ClipVert &a, const ClipVert &b, float t)
{
    return {a.clip + (b.clip - a.clip) * t, a.n + (b.n - a.n) * t, a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t};
}

// clips against the near plane (z > -w); returns 0, 3 or 4 vertices
int clipNear(const ClipVert in[3], ClipVert out[4])
{
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        const ClipVert &a = in[i], &b = in[(i + 1) % 3];
        const float da = a.clip.z() + a.clip.w(), db = b.clip.z() + b.clip.w();
        if (da >= 0)
            out[n++] = a;
        if ((da >= 0) != (db >= 0))
            out[n++] = lerp(a, b, da / (da - db));
    }
    return n;
}

} // namespace

void renderModel(const Model &model, const QVector<MaterialTextures> &textures, const RenderParams &rp, QImage &out)
{
    const int W = out.width(), H = out.height();
    if (W <= 0 || H <= 0)
        return;
    if (out.format() != QImage::Format_RGB32 && out.format() != QImage::Format_ARGB32_Premultiplied)
        out = QImage(W, H, QImage::Format_RGB32);
    // neutral dark gradient, so both light and dark textures read well
    for (int y = 0; y < H; ++y) {
        const float t = float(y) / std::max(1, H - 1);
        const QRgb c = qRgb(int(78 - 36 * t), int(82 - 38 * t), int(90 - 40 * t));
        QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(y));
        std::fill(row, row + W, c);
    }
    if (model.isEmpty())
        return;

    QMatrix4x4 view, proj, norm;
    const QVector3D eye(float(rp.distance * std::cos(rp.pitch) * std::sin(rp.yaw)), float(rp.distance * std::sin(rp.pitch)),
                        float(rp.distance * std::cos(rp.pitch) * std::cos(rp.yaw)));
    view.lookAt(eye, QVector3D(0, 0, 0), QVector3D(0, 1, 0));
    QMatrix4x4 pan;
    pan.translate(float(rp.panX), float(rp.panY), 0);
    view = pan * view;
    proj.perspective(40.0f, float(W) / H, 0.01f, 100.0f);
    norm.scale(1.0f / model.radius);
    norm.translate(-model.center);
    const QMatrix4x4 mv = view * norm;
    const QMatrix4x4 mvp = proj * mv;

    // ---- vertex stage
    QVector<ClipVert> verts(model.vertices.size());
    for (int i = 0; i < model.vertices.size(); ++i) {
        const Vertex &v = model.vertices[i];
        verts[i] = {mvp.map(QVector4D(v.pos, 1)), view.mapVector(v.normal), v.u, v.v};
    }

    // ---- triangle setup
    QVector<Tri> tris;
    tris.reserve(model.triangleCount());
    for (const Part &part : model.parts) {
        if (part.material < textures.size() && textures[part.material].hidden)
            continue;
        const bool normalMapped = rp.normalMaps && model.hasUV && part.material < textures.size() &&
                                  !textures[part.material].normal.isNull();
        for (int i = part.first; i + 2 < part.first + part.count; i += 3) {
            const quint32 idx[3] = {model.indices[i], model.indices[i + 1], model.indices[i + 2]};
            ClipVert in[3] = {verts[idx[0]], verts[idx[1]], verts[idx[2]]};
            QVector3D tangent, bitangent;
            if (normalMapped) {
                const Vertex &a = model.vertices[idx[0]], &b = model.vertices[idx[1]], &c = model.vertices[idx[2]];
                const QVector3D e1 = b.pos - a.pos, e2 = c.pos - a.pos;
                const float du1 = b.u - a.u, dv1 = b.v - a.v, du2 = c.u - a.u, dv2 = c.v - a.v;
                const float det = du1 * dv2 - du2 * dv1;
                if (std::fabs(det) > 1e-12f) {
                    tangent = mv.mapVector((e1 * dv2 - e2 * dv1) / det).normalized();
                    bitangent = mv.mapVector((e2 * du1 - e1 * du2) / det).normalized();
                }
            }
            ClipVert poly[4];
            const int n = clipNear(in, poly);
            for (int k = 1; k + 1 < n; ++k) {
                const ClipVert *cv[3] = {&poly[0], &poly[k], &poly[k + 1]};
                Tri t;
                for (int j = 0; j < 3; ++j) {
                    const float w = std::max(cv[j]->clip.w(), 1e-6f);
                    t.iw[j] = 1.0f / w;
                    t.x[j] = (cv[j]->clip.x() * t.iw[j] + 1) * 0.5f * W;
                    t.y[j] = (1 - cv[j]->clip.y() * t.iw[j]) * 0.5f * H;
                    t.z[j] = cv[j]->clip.z() * t.iw[j];
                    t.nw[j] = cv[j]->n * t.iw[j];
                    t.uw[j] = cv[j]->u * t.iw[j];
                    t.vw[j] = cv[j]->v * t.iw[j];
                }
                t.area = (t.x[1] - t.x[0]) * (t.y[2] - t.y[0]) - (t.x[2] - t.x[0]) * (t.y[1] - t.y[0]);
                if (std::fabs(t.area) < 1e-8f)
                    continue;
                t.back = t.area > 0;                                // screen y points down: front faces are negative
                if (t.back && !rp.twoSided)
                    continue;
                t.minX = std::max(0, int(std::floor(std::min({t.x[0], t.x[1], t.x[2]}))));
                t.maxX = std::min(W - 1, int(std::ceil(std::max({t.x[0], t.x[1], t.x[2]}))));
                t.minY = std::max(0, int(std::floor(std::min({t.y[0], t.y[1], t.y[2]}))));
                t.maxY = std::min(H - 1, int(std::ceil(std::max({t.y[0], t.y[1], t.y[2]}))));
                if (t.minX > t.maxX || t.minY > t.maxY)
                    continue;
                for (int j = 0; j < 3; ++j) {
                    const int a = (j + 1) % 3, b = (j + 2) % 3;     // edge opposite vertex j
                    const float len = std::hypot(t.x[b] - t.x[a], t.y[b] - t.y[a]);
                    t.edgeScale[j] = len > 0 ? std::fabs(t.area) / len : 0;
                }
                t.t = tangent;
                t.b = bitangent;
                t.material = part.material;
                tris.append(t);
            }
        }
    }

    // ---- raster stage, in horizontal bands so threads never share pixels
    static const QVector3D key = QVector3D(-0.35f, 0.55f, 0.75f).normalized();
    static const QVector3D fill = QVector3D(0.6f, -0.3f, 0.5f).normalized();
    const MaterialTextures fallback;
    QVector<float> depth(qsizetype(W) * H, std::numeric_limits<float>::max());
    const int bandHeight = 8;
    const int bands = (H + bandHeight - 1) / bandHeight;
    parallelRows(0, bands - 1, [&](int band) {
        const int y0 = band * bandHeight, y1 = std::min(H - 1, y0 + bandHeight - 1);
        for (const Tri &t : tris) {
            if (t.maxY < y0 || t.minY > y1)
                continue;
            const MaterialTextures &mt = t.material < textures.size() ? textures[t.material] : fallback;
            const bool textured = model.hasUV && !mt.base.isNull();
            const bool normalMapped = !t.t.isNull() && !mt.normal.isNull();
            const float inv = 1.0f / t.area;
            for (int y = std::max(y0, t.minY); y <= std::min(y1, t.maxY); ++y) {
                QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(y));
                float *zrow = depth.data() + qsizetype(y) * W;
                const float py = y + 0.5f;
                for (int x = t.minX; x <= t.maxX; ++x) {
                    const float px = x + 0.5f;
                    const float e0 = (t.x[2] - t.x[1]) * (py - t.y[1]) - (px - t.x[1]) * (t.y[2] - t.y[1]);
                    const float e1 = (t.x[0] - t.x[2]) * (py - t.y[2]) - (px - t.x[2]) * (t.y[0] - t.y[2]);
                    const float e2 = (t.x[1] - t.x[0]) * (py - t.y[0]) - (px - t.x[0]) * (t.y[1] - t.y[0]);
                    const float b0 = e0 * inv, b1 = e1 * inv, b2 = e2 * inv;
                    if (b0 < 0 || b1 < 0 || b2 < 0)
                        continue;
                    const float z = b0 * t.z[0] + b1 * t.z[1] + b2 * t.z[2];
                    if (z >= zrow[x] || z > 1)
                        continue;
                    const float iw = b0 * t.iw[0] + b1 * t.iw[1] + b2 * t.iw[2];
                    const float u = (b0 * t.uw[0] + b1 * t.uw[1] + b2 * t.uw[2]) / iw;
                    const float v = (b0 * t.vw[0] + b1 * t.vw[1] + b2 * t.vw[2]) / iw;
                    Rgba c{float(mt.color.redF()), float(mt.color.greenF()), float(mt.color.blueF()), 1};
                    if (textured) {
                        c = sample(mt.base, u, v, rp.pixelated);
                        if (c.a < 0.5f)
                            continue;                               // alpha cutout (leaves, fences...)
                    }
                    zrow[x] = z;
                    if (rp.lighting) {
                        QVector3D n = ((t.nw[0] * b0 + t.nw[1] * b1 + t.nw[2] * b2) / iw).normalized();
                        if (n.isNull())
                            n = QVector3D(0, 0, 1);
                        if (t.back)
                            n = -n;
                        if (normalMapped) {
                            const Rgba s = sample(mt.normal, u, v, rp.pixelated);
                            QVector3D T = (t.t - n * QVector3D::dotProduct(n, t.t)).normalized();
                            QVector3D B = QVector3D::crossProduct(n, T);
                            if (QVector3D::dotProduct(B, t.b) < 0)
                                B = -B;                             // mirrored UVs
                            const QVector3D mapped = T * (s.r * 2 - 1) + B * (s.g * 2 - 1) + n * (s.b * 2 - 1);
                            if (!mapped.isNull())
                                n = mapped.normalized();
                        }
                        const float light = 0.32f + 0.78f * std::max(0.0f, QVector3D::dotProduct(n, key)) +
                                            0.18f * std::max(0.0f, QVector3D::dotProduct(n, fill));
                        c.r *= light;
                        c.g *= light;
                        c.b *= light;
                    }
                    if (rp.wireframe) {
                        const float d = std::min({b0 * t.edgeScale[0], b1 * t.edgeScale[1], b2 * t.edgeScale[2]});
                        if (d < 1.0f) {
                            const float k = 0.6f * (1.0f - d);
                            c.r += (1 - c.r) * k;
                            c.g += (1 - c.g) * k;
                            c.b += (1 - c.b) * k;
                        }
                    }
                    row[x] = qRgb(std::clamp(int(c.r * 255 + 0.5f), 0, 255), std::clamp(int(c.g * 255 + 0.5f), 0, 255),
                                  std::clamp(int(c.b * 255 + 0.5f), 0, 255));
                }
            }
        }
    });
}

} // namespace ModelView
