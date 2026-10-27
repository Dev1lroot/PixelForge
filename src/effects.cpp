#include "effects.h"
#include "document.h"
#include "mainwindow.h"
#include "settings.h"
#include "parallel.h"
#include <cstring>

#include <QPainter>
#include <QMouseEvent>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QGroupBox>
#include <QDial>
#include <QRandomGenerator>
#include <thread>
#include <vector>
#include <cmath>
#include <algorithm>

// ============================================================ helpers

static inline int clamp255(double v) { return v < 0 ? 0 : v > 255 ? 255 : int(v + 0.5); }
static inline int clamp255i(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

template <typename F>
static void perPixel(const QImage &src, QImage &dst, const QRect &roi, F fn)
{
    parallelRows(roi.top(), roi.bottom(), [&](int y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(const_cast<uchar *>(dst.constScanLine(y)));
        for (int x = roi.left(); x <= roi.right(); ++x)
            d[x] = fn(s[x], x, y);
    });
}

static QImage toPremul(const QImage &src) { return src.convertToFormat(QImage::Format_ARGB32_Premultiplied); }

static inline QRgb unpremul(QRgb p) { return qUnpremultiply(p); }

// bilinear sample from premultiplied image, edge-clamped
static inline QRgb sampleBilinear(const QImage &pm, double x, double y)
{
    const int w = pm.width(), h = pm.height();
    x -= 0.5;
    y -= 0.5;
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    double fx = x - x0, fy = y - y0;
    auto px = [&](int xx, int yy) {
        xx = std::clamp(xx, 0, w - 1);
        yy = std::clamp(yy, 0, h - 1);
        return reinterpret_cast<const QRgb *>(pm.constScanLine(yy))[xx];
    };
    QRgb a = px(x0, y0), b = px(x0 + 1, y0), c = px(x0, y0 + 1), d = px(x0 + 1, y0 + 1);
    auto ch = [&](int sh) {
        double top = ((a >> sh) & 255) * (1 - fx) + ((b >> sh) & 255) * fx;
        double bot = ((c >> sh) & 255) * (1 - fx) + ((d >> sh) & 255) * fx;
        return int(top * (1 - fy) + bot * fy + 0.5);
    };
    return qRgba(ch(16), ch(8), ch(0), ch(24));
}

// separable box blur on premultiplied data (in place, whole image)
static void boxBlurH(const QImage &in, QImage &out, int r, int y0, int y1)
{
    const int w = in.width();
    parallelRows(y0, y1, [&](int y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(in.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(const_cast<uchar *>(out.constScanLine(y)));
        int sum[4] = {0, 0, 0, 0};
        const int div = 2 * r + 1;
        auto add = [&](QRgb p, int sign) {
            sum[0] += sign * qAlpha(p); sum[1] += sign * qRed(p); sum[2] += sign * qGreen(p); sum[3] += sign * qBlue(p);
        };
        for (int i = -r; i <= r; ++i)
            add(s[std::clamp(i, 0, w - 1)], 1);
        for (int x = 0; x < w; ++x) {
            d[x] = qRgba(sum[1] / div, sum[2] / div, sum[3] / div, sum[0] / div);
            add(s[std::clamp(x + r + 1, 0, w - 1)], 1);
            add(s[std::clamp(x - r, 0, w - 1)], -1);
        }
    });
}

static QImage transposed(const QImage &img)
{
    QImage t(img.height(), img.width(), img.format());
    parallelRows(0, img.height() - 1, [&](int y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            reinterpret_cast<QRgb *>(const_cast<uchar *>(t.constScanLine(x)))[y] = s[x];
    });
    return t;
}

static QImage boxBlur(const QImage &pm, int r, int passes = 1)
{
    if (r <= 0)
        return pm;
    QImage a = pm.copy(), b(pm.size(), pm.format());
    for (int i = 0; i < passes; ++i) {
        boxBlurH(a, b, r, 0, a.height() - 1);
        std::swap(a, b);
    }
    a = transposed(a);
    b = QImage(a.size(), a.format());
    for (int i = 0; i < passes; ++i) {
        boxBlurH(a, b, r, 0, a.height() - 1);
        std::swap(a, b);
    }
    return transposed(a);
}

static QImage gaussianBlur(const QImage &pm, int radius)
{
    if (radius <= 0)
        return pm;
    // three box passes approximate a gaussian with sigma ~ radius/2
    int r = std::max(1, int(std::round(radius / 2.0)));
    return boxBlur(pm, r, 3);
}

static inline int lum(QRgb p) { return (qRed(p) * 299 + qGreen(p) * 587 + qBlue(p) * 114) / 1000; }

static double hash2(int x, int y, int seed)
{
    unsigned h = unsigned(x) * 374761393u + unsigned(y) * 668265263u + unsigned(seed) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xffffff) / double(0xffffff);
}

static double valueNoise(double x, double y, int seed)
{
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    double fx = x - x0, fy = y - y0;
    auto s = [](double t) { return t * t * (3 - 2 * t); };
    double a = hash2(x0, y0, seed), b = hash2(x0 + 1, y0, seed);
    double c = hash2(x0, y0 + 1, seed), d = hash2(x0 + 1, y0 + 1, seed);
    double u = s(fx), v = s(fy);
    return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}

static QRgb lerpRgb(QRgb a, QRgb b, double t)
{
    auto l = [t](int x, int y) { return clamp255(x + (y - x) * t); };
    return qRgba(l(qRed(a), qRed(b)), l(qGreen(a), qGreen(b)), l(qBlue(a), qBlue(b)), l(qAlpha(a), qAlpha(b)));
}

static void rgbToHsl(int r, int g, int b, double &h, double &s, double &l)
{
    double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
    double mx = std::max({rf, gf, bf}), mn = std::min({rf, gf, bf});
    l = (mx + mn) / 2;
    if (mx == mn) {
        h = s = 0;
        return;
    }
    double d = mx - mn;
    s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == rf) h = (gf - bf) / d + (gf < bf ? 6 : 0);
    else if (mx == gf) h = (bf - rf) / d + 2;
    else h = (rf - gf) / d + 4;
    h /= 6;
}

static double hue2rgb(double p, double q, double t)
{
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0 / 6) return p + (q - p) * 6 * t;
    if (t < 0.5) return q;
    if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}

static void hslToRgb(double h, double s, double l, int &r, int &g, int &b)
{
    if (s == 0) {
        r = g = b = clamp255(l * 255);
        return;
    }
    double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
    double p = 2 * l - q;
    r = clamp255(hue2rgb(p, q, h + 1.0 / 3) * 255);
    g = clamp255(hue2rgb(p, q, h) * 255);
    b = clamp255(hue2rgb(p, q, h - 1.0 / 3) * 255);
}

// monotone cubic interpolation through control points (0..255 domain)
QVector<int> curveLut(const QVariantList &ptsIn)
{
    QVector<QPointF> pts;
    for (const QVariant &v : ptsIn)
        pts << v.toPointF();
    if (pts.size() < 2)
        pts = {QPointF(0, 0), QPointF(255, 255)};
    std::sort(pts.begin(), pts.end(), [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    const int n = pts.size();
    QVector<double> m(n), dk(n - 1);
    for (int i = 0; i < n - 1; ++i) {
        double dx = pts[i + 1].x() - pts[i].x();
        dk[i] = dx > 0 ? (pts[i + 1].y() - pts[i].y()) / dx : 0;
    }
    m[0] = dk[0];
    m[n - 1] = dk[n - 2];
    for (int i = 1; i < n - 1; ++i)
        m[i] = (dk[i - 1] * dk[i] <= 0) ? 0 : (dk[i - 1] + dk[i]) / 2;
    QVector<int> lut(256);
    for (int x = 0; x < 256; ++x) {
        if (x <= pts[0].x()) { lut[x] = clamp255(pts[0].y()); continue; }
        if (x >= pts[n - 1].x()) { lut[x] = clamp255(pts[n - 1].y()); continue; }
        int i = 0;
        while (i < n - 2 && x > pts[i + 1].x())
            ++i;
        double h = pts[i + 1].x() - pts[i].x();
        double t = (x - pts[i].x()) / h;
        double t2 = t * t, t3 = t2 * t;
        double y = (2 * t3 - 3 * t2 + 1) * pts[i].y() + (t3 - 2 * t2 + t) * h * m[i] +
                   (-2 * t3 + 3 * t2) * pts[i + 1].y() + (t3 - t2) * h * m[i + 1];
        lut[x] = clamp255(y);
    }
    return lut;
}

// ============================================================ effect implementations

static EffectParam P(const QString &key, const QString &label, double mn, double mx, double def,
                     EffectParam::Type t = EffectParam::Int, int decimals = 0)
{
    EffectParam p;
    p.key = key; p.label = label; p.min = mn; p.max = mx; p.def = def; p.type = t; p.decimals = decimals;
    return p;
}

static EffectParam C(const QString &key, const QString &label, const QStringList &choices, int def = 0)
{
    EffectParam p;
    p.key = key; p.label = label; p.type = EffectParam::Choice; p.choices = choices; p.def = def; p.max = choices.size() - 1;
    return p;
}

static EffectParam B(const QString &key, const QString &label, bool def)
{
    EffectParam p;
    p.key = key; p.label = label; p.type = EffectParam::Bool; p.def = def; p.max = 1;
    return p;
}

static void fxInvert(const QImage &s, QImage &d, const QRect &r, const QVariantMap &)
{
    perPixel(s, d, r, [](QRgb p, int, int) { return qRgba(255 - qRed(p), 255 - qGreen(p), 255 - qBlue(p), qAlpha(p)); });
}

static void fxBlackWhite(const QImage &s, QImage &d, const QRect &r, const QVariantMap &)
{
    perPixel(s, d, r, [](QRgb p, int, int) { int l = lum(p); return qRgba(l, l, l, qAlpha(p)); });
}

// greyscale, then white becomes transparent: every pixel is black, its darkness kept as opacity
static void fxBlackAlpha(const QImage &s, QImage &d, const QRect &r, const QVariantMap &)
{
    perPixel(s, d, r, [](QRgb p, int, int) { return qRgba(0, 0, 0, (255 - lum(p)) * qAlpha(p) / 255); });
}

static void fxSepia(const QImage &s, QImage &d, const QRect &r, const QVariantMap &)
{
    perPixel(s, d, r, [](QRgb p, int, int) {
        int l = lum(p);
        return qRgba(clamp255(l * 1.07 + 20), clamp255(l * 0.95 + 8), clamp255(l * 0.75), qAlpha(p));
    });
}

static void fxAutoLevel(const QImage &s, QImage &d, const QRect &r, const QVariantMap &)
{
    std::vector<long> hist[3] = {std::vector<long>(256), std::vector<long>(256), std::vector<long>(256)};
    long total = 0;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const QRgb *l = reinterpret_cast<const QRgb *>(s.constScanLine(y));
        for (int x = r.left(); x <= r.right(); ++x) {
            if (!qAlpha(l[x]))
                continue;
            hist[0][qRed(l[x])]++; hist[1][qGreen(l[x])]++; hist[2][qBlue(l[x])]++;
            ++total;
        }
    }
    int lo[3], hi[3];
    long cut = total / 200;
    for (int c = 0; c < 3; ++c) {
        long acc = 0;
        lo[c] = 0;
        while (lo[c] < 255 && (acc += hist[c][lo[c]]) <= cut) ++lo[c];
        acc = 0;
        hi[c] = 255;
        while (hi[c] > 0 && (acc += hist[c][hi[c]]) <= cut) --hi[c];
        if (hi[c] <= lo[c]) { lo[c] = 0; hi[c] = 255; }
    }
    perPixel(s, d, r, [&](QRgb p, int, int) {
        auto f = [&](int v, int c) { return clamp255((v - lo[c]) * 255.0 / (hi[c] - lo[c])); };
        return qRgba(f(qRed(p), 0), f(qGreen(p), 1), f(qBlue(p), 2), qAlpha(p));
    });
}

static void fxBrightnessContrast(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double b = p["brightness"].toDouble();
    double c = p["contrast"].toDouble();
    double k = c >= 0 ? 1.0 + c / 100.0 * 3.0 : 1.0 + c / 100.0;
    int lut[256];
    for (int i = 0; i < 256; ++i)
        lut[i] = clamp255((i - 127.5) * k + 127.5 + b * 1.275);
    perPixel(s, d, r, [&](QRgb px, int, int) { return qRgba(lut[qRed(px)], lut[qGreen(px)], lut[qBlue(px)], qAlpha(px)); });
}

static void fxHueSat(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double hue = p["hue"].toDouble() / 360.0;
    double sat = p["saturation"].toDouble() / 100.0;
    double light = p["lightness"].toDouble() / 100.0;
    perPixel(s, d, r, [&](QRgb px, int, int) {
        double h, sl, l;
        rgbToHsl(qRed(px), qGreen(px), qBlue(px), h, sl, l);
        h = std::fmod(h + hue + 1.0, 1.0);
        sl = std::clamp(sl * sat, 0.0, 1.0);
        if (light > 0) l = l + (1 - l) * light;
        else l = l * (1 + light);
        int rr, gg, bb;
        hslToRgb(h, sl, l, rr, gg, bb);
        return qRgba(rr, gg, bb, qAlpha(px));
    });
}

static void fxLevels(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double inB = p["inBlack"].toDouble(), inW = p["inWhite"].toDouble();
    double g = p["gamma"].toDouble();
    double outB = p["outBlack"].toDouble(), outW = p["outWhite"].toDouble();
    if (inW <= inB) inW = inB + 1;
    int lut[256];
    for (int i = 0; i < 256; ++i) {
        double t = std::clamp((i - inB) / (inW - inB), 0.0, 1.0);
        t = std::pow(t, 1.0 / g);
        lut[i] = clamp255(outB + t * (outW - outB));
    }
    perPixel(s, d, r, [&](QRgb px, int, int) { return qRgba(lut[qRed(px)], lut[qGreen(px)], lut[qBlue(px)], qAlpha(px)); });
}

static void fxCurves(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    QVector<int> lut = curveLut(p["curve"].toList());
    int mode = p["mode"].toInt();
    perPixel(s, d, r, [&](QRgb px, int, int) {
        if (mode == 1)
            return qRgba(lut[qRed(px)], lut[qGreen(px)], lut[qBlue(px)], qAlpha(px));
        // luminosity: scale colour by luminance ratio
        int l = lum(px);
        int nl = lut[l];
        if (l == 0)
            return qRgba(nl, nl, nl, qAlpha(px));
        double k = double(nl) / l;
        return qRgba(clamp255(qRed(px) * k), clamp255(qGreen(px) * k), clamp255(qBlue(px) * k), qAlpha(px));
    });
}

static void fxPosterize(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    int lv[3] = {p["red"].toInt(), p["green"].toInt(), p["blue"].toInt()};
    if (p["linked"].toBool())
        lv[1] = lv[2] = lv[0];
    int lut[3][256];
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i) {
            int n = std::max(2, lv[c]);
            int q = int(std::round(i / 255.0 * (n - 1)));
            lut[c][i] = clamp255(q * 255.0 / (n - 1));
        }
    perPixel(s, d, r, [&](QRgb px, int, int) { return qRgba(lut[0][qRed(px)], lut[1][qGreen(px)], lut[2][qBlue(px)], qAlpha(px)); });
}

static void copyPremulResult(const QImage &pmResult, QImage &d, const QRect &r)
{
    parallelRows(r.top(), r.bottom(), [&](int y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(pmResult.constScanLine(y));
        QRgb *o = reinterpret_cast<QRgb *>(const_cast<uchar *>(d.constScanLine(y)));
        for (int x = r.left(); x <= r.right(); ++x)
            o[x] = unpremul(s[x]);
    });
}

static void fxGaussian(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    copyPremulResult(gaussianBlur(toPremul(s), p["radius"].toInt()), d, r);
}

static void fxUnfocus(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    copyPremulResult(boxBlur(toPremul(s), p["radius"].toInt(), 2), d, r);
}

// generic multi-sample blur: offsets(x, y, i) returns the sample position
template <typename F>
static void sampleBlur(const QImage &s, QImage &d, const QRect &r, int samples, F pos)
{
    QImage pm = toPremul(s);
    parallelRows(r.top(), r.bottom(), [&](int y) {
        QRgb *o = reinterpret_cast<QRgb *>(const_cast<uchar *>(d.constScanLine(y)));
        for (int x = r.left(); x <= r.right(); ++x) {
            double acc[4] = {0, 0, 0, 0};
            for (int i = 0; i < samples; ++i) {
                QPointF q = pos(x + 0.5, y + 0.5, i);
                QRgb c = sampleBilinear(pm, q.x(), q.y());
                acc[0] += qAlpha(c); acc[1] += qRed(c); acc[2] += qGreen(c); acc[3] += qBlue(c);
            }
            o[x] = unpremul(qRgba(clamp255(acc[1] / samples), clamp255(acc[2] / samples), clamp255(acc[3] / samples),
                                  clamp255(acc[0] / samples)));
        }
    });
}

static void fxMotionBlur(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double ang = p["angle"].toDouble() * M_PI / 180;
    double dist = p["distance"].toDouble();
    bool centered = p["centered"].toBool();
    int n = std::clamp(int(dist), 2, 96);
    double dx = std::cos(ang) * dist, dy = -std::sin(ang) * dist;
    sampleBlur(s, d, r, n, [&](double x, double y, int i) {
        double t = double(i) / (n - 1);
        if (centered) t -= 0.5;
        return QPointF(x - dx * t, y - dy * t);
    });
}

static void fxZoomBlur(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double amount = p["amount"].toDouble() / 100.0 * 0.5;
    QPointF c(s.width() * (0.5 + p["offsetX"].toDouble() / 200.0), s.height() * (0.5 + p["offsetY"].toDouble() / 200.0));
    const int n = 24;
    sampleBlur(s, d, r, n, [&](double x, double y, int i) {
        double t = 1.0 - amount * i / (n - 1);
        return QPointF(c.x() + (x - c.x()) * t, c.y() + (y - c.y()) * t);
    });
}

static void fxRadialBlur(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double span = p["angle"].toDouble() * M_PI / 180;
    QPointF c(s.width() * (0.5 + p["offsetX"].toDouble() / 200.0), s.height() * (0.5 + p["offsetY"].toDouble() / 200.0));
    const int n = 24;
    sampleBlur(s, d, r, n, [&](double x, double y, int i) {
        double a = span * (double(i) / (n - 1) - 0.5);
        double ca = std::cos(a), sa = std::sin(a);
        double dx = x - c.x(), dy = y - c.y();
        return QPointF(c.x() + dx * ca - dy * sa, c.y() + dx * sa + dy * ca);
    });
}

static void fxFragment(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    int n = p["fragments"].toInt();
    double dist = p["distance"].toDouble();
    double rot = p["rotation"].toDouble() * M_PI / 180;
    sampleBlur(s, d, r, n, [&](double x, double y, int i) {
        double a = rot + 2 * M_PI * i / n;
        return QPointF(x + std::cos(a) * dist, y + std::sin(a) * dist);
    });
}

template <typename F>
static void warp(const QImage &s, QImage &d, const QRect &r, F map)
{
    QImage pm = toPremul(s);
    parallelRows(r.top(), r.bottom(), [&](int y) {
        QRgb *o = reinterpret_cast<QRgb *>(const_cast<uchar *>(d.constScanLine(y)));
        for (int x = r.left(); x <= r.right(); ++x) {
            QPointF q = map(x + 0.5, y + 0.5);
            o[x] = unpremul(sampleBilinear(pm, q.x(), q.y()));
        }
    });
}

static void fxBulge(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double amount = p["amount"].toDouble() / 100.0;
    QPointF c(r.center().x() + r.width() * p["offsetX"].toDouble() / 200.0, r.center().y() + r.height() * p["offsetY"].toDouble() / 200.0);
    double R = std::min(r.width(), r.height()) / 2.0;
    warp(s, d, r, [&](double x, double y) {
        double dx = x - c.x(), dy = y - c.y();
        double dist = std::sqrt(dx * dx + dy * dy);
        if (dist >= R || R <= 0)
            return QPointF(x, y);
        double t = dist / R;
        double k = 1.0 - amount * (1 - t * t) * 0.75;
        return QPointF(c.x() + dx * k, c.y() + dy * k);
    });
}

static void fxTwist(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double amount = p["amount"].toDouble() / 10.0;
    QPointF c = QRectF(r).center();
    double R = std::min(r.width(), r.height()) / 2.0;
    warp(s, d, r, [&](double x, double y) {
        double dx = x - c.x(), dy = y - c.y();
        double dist = std::sqrt(dx * dx + dy * dy);
        if (dist >= R)
            return QPointF(x, y);
        double t = 1 - dist / R;
        double a = amount * t * t;
        double ca = std::cos(a), sa = std::sin(a);
        return QPointF(c.x() + dx * ca - dy * sa, c.y() + dx * sa + dy * ca);
    });
}

static void fxPolarInversion(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double amount = p["amount"].toDouble();
    QPointF c = QRectF(r).center();
    double R2 = (r.width() * r.width() + r.height() * r.height()) / 16.0 * amount;
    warp(s, d, r, [&](double x, double y) {
        double dx = x - c.x(), dy = y - c.y();
        double d2 = dx * dx + dy * dy;
        if (d2 < 1e-6)
            return QPointF(x, y);
        double k = R2 / d2;
        double nx = c.x() + dx * k, ny = c.y() + dy * k;
        // reflect into image
        auto refl = [](double v, double lo, double hi) {
            double w = hi - lo;
            if (w <= 0) return lo;
            double t = std::fmod(v - lo, 2 * w);
            if (t < 0) t += 2 * w;
            return lo + (t > w ? 2 * w - t : t);
        };
        return QPointF(refl(nx, r.left(), r.right() + 1), refl(ny, r.top(), r.bottom() + 1));
    });
}

static void fxFrostedGlass(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double amount = p["amount"].toDouble();
    int seed = p["seed"].toInt();
    warp(s, d, r, [&](double x, double y) {
        double a = hash2(int(x), int(y), seed) * 2 * M_PI;
        double m = hash2(int(y), int(x), seed + 7) * amount;
        return QPointF(x + std::cos(a) * m, y + std::sin(a) * m);
    });
}

static void fxPixelate(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    int cell = std::max(1, p["size"].toInt());
    QImage pm = toPremul(s);
    for (int cy = (r.top() / cell) * cell; cy <= r.bottom(); cy += cell)
        for (int cx = (r.left() / cell) * cell; cx <= r.right(); cx += cell) {
            QRect c = QRect(cx, cy, cell, cell).intersected(s.rect());
            long acc[4] = {0, 0, 0, 0};
            for (int y = c.top(); y <= c.bottom(); ++y) {
                const QRgb *l = reinterpret_cast<const QRgb *>(pm.constScanLine(y));
                for (int x = c.left(); x <= c.right(); ++x) {
                    acc[0] += qAlpha(l[x]); acc[1] += qRed(l[x]); acc[2] += qGreen(l[x]); acc[3] += qBlue(l[x]);
                }
            }
            long n = std::max<long>(1, long(c.width()) * c.height());
            QRgb v = unpremul(qRgba(acc[1] / n, acc[2] / n, acc[3] / n, acc[0] / n));
            QRect w = c.intersected(r);
            for (int y = w.top(); y <= w.bottom(); ++y) {
                QRgb *o = reinterpret_cast<QRgb *>(d.scanLine(y));
                for (int x = w.left(); x <= w.right(); ++x)
                    o[x] = v;
            }
        }
}

static void fxAddNoise(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double intensity = p["intensity"].toDouble() / 100.0;
    double colorSat = p["saturation"].toDouble() / 100.0;
    double coverage = p["coverage"].toDouble() / 100.0;
    int seed = p["seed"].toInt();
    perPixel(s, d, r, [&](QRgb px, int x, int y) {
        if (hash2(x, y, seed + 3) > coverage)
            return px;
        double n = (hash2(x, y, seed) - 0.5) * 2 * intensity * 128;
        double cr = (hash2(x, y, seed + 11) - 0.5) * colorSat * intensity * 128;
        double cg = (hash2(x, y, seed + 17) - 0.5) * colorSat * intensity * 128;
        double cb = (hash2(x, y, seed + 23) - 0.5) * colorSat * intensity * 128;
        return qRgba(clamp255(qRed(px) + n + cr), clamp255(qGreen(px) + n + cg), clamp255(qBlue(px) + n + cb), qAlpha(px));
    });
}

static void fxMedian(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    const int rad = p["radius"].toInt();
    const double pct = p["percentile"].toDouble() / 100.0;
    const int w = s.width(), h = s.height();
    parallelRows(r.top(), r.bottom(), [&](int y) {
        int hist[4][256];
        memset(hist, 0, sizeof(hist));
        int count = 0;
        auto addCol = [&](int x, int sign) {
            if (x < 0 || x >= w)
                return;
            for (int yy = std::max(0, y - rad); yy <= std::min(h - 1, y + rad); ++yy) {
                QRgb px = reinterpret_cast<const QRgb *>(s.constScanLine(yy))[x];
                hist[0][qRed(px)] += sign; hist[1][qGreen(px)] += sign; hist[2][qBlue(px)] += sign; hist[3][qAlpha(px)] += sign;
                count += sign;
            }
        };
        for (int x = r.left() - rad; x <= r.left() + rad; ++x)
            addCol(x, 1);
        QRgb *o = reinterpret_cast<QRgb *>(const_cast<uchar *>(d.constScanLine(y)));
        for (int x = r.left(); x <= r.right(); ++x) {
            int target = std::max(1, int(count * pct + 0.5));
            int v[4];
            for (int c = 0; c < 4; ++c) {
                int acc = 0, i = 0;
                for (; i < 255; ++i) {
                    acc += hist[c][i];
                    if (acc >= target)
                        break;
                }
                v[c] = i;
            }
            o[x] = qRgba(v[0], v[1], v[2], v[3]);
            addCol(x - rad, -1);
            addCol(x + rad + 1, 1);
        }
    });
}

static void fxSharpen(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    int amount = p["amount"].toInt();
    QImage blur = gaussianBlur(toPremul(s), std::max(1, amount / 2));
    double k = 0.4 + amount * 0.08;
    perPixel(s, d, r, [&](QRgb px, int x, int y) {
        QRgb b = unpremul(reinterpret_cast<const QRgb *>(blur.constScanLine(y))[x]);
        return qRgba(clamp255(qRed(px) + (qRed(px) - qRed(b)) * k), clamp255(qGreen(px) + (qGreen(px) - qGreen(b)) * k),
                     clamp255(qBlue(px) + (qBlue(px) - qBlue(b)) * k), qAlpha(px));
    });
}

static void fxGlow(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    QImage blur = gaussianBlur(toPremul(s), p["radius"].toInt());
    double br = p["brightness"].toDouble() / 100.0, ct = p["contrast"].toDouble() / 100.0;
    perPixel(s, d, r, [&](QRgb px, int x, int y) {
        QRgb b = unpremul(reinterpret_cast<const QRgb *>(blur.constScanLine(y))[x]);
        auto ch = [&](int a, int g) {
            double gv = std::clamp((g - 128) * (1 + ct) + 128 + br * 128, 0.0, 255.0);
            return clamp255(255 - (255 - a) * (255 - gv) / 255.0);  // screen
        };
        return qRgba(ch(qRed(px), qRed(b)), ch(qGreen(px), qGreen(b)), ch(qBlue(px), qBlue(b)), qAlpha(px));
    });
}

static void fxSoftenPortrait(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    QImage blur = gaussianBlur(toPremul(s), std::max(1, p["softness"].toInt()));
    double light = p["lighting"].toDouble() / 100.0;
    double warmth = p["warmth"].toDouble() / 100.0;
    perPixel(s, d, r, [&](QRgb px, int x, int y) {
        QRgb b = unpremul(reinterpret_cast<const QRgb *>(blur.constScanLine(y))[x]);
        QRgb m = lerpRgb(px, b, 0.6);
        double lr = 1 + light * 0.4;
        return qRgba(clamp255(qRed(m) * lr + warmth * 25), clamp255(qGreen(m) * lr + warmth * 8),
                     clamp255(qBlue(m) * lr - warmth * 15), qAlpha(px));
    });
}

static void fxVignette(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double radius = p["radius"].toDouble() / 100.0;
    double strength = p["strength"].toDouble() / 100.0;
    QPointF c(r.center().x() + r.width() * p["offsetX"].toDouble() / 200.0, r.center().y() + r.height() * p["offsetY"].toDouble() / 200.0);
    double maxd = std::sqrt(r.width() * r.width() + r.height() * r.height()) / 2.0 * radius;
    perPixel(s, d, r, [&](QRgb px, int x, int y) {
        double dist = std::hypot(x - c.x(), y - c.y()) / std::max(1.0, maxd);
        double f = 1 - strength * std::clamp(dist * dist, 0.0, 1.0);
        return qRgba(clamp255(qRed(px) * f), clamp255(qGreen(px) * f), clamp255(qBlue(px) * f), qAlpha(px));
    });
}

static void fxRedEye(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double tol = p["tolerance"].toDouble() / 100.0;
    double sat = p["saturation"].toDouble() / 100.0;
    perPixel(s, d, r, [&](QRgb px, int, int) {
        int R = qRed(px), G = qGreen(px), Bc = qBlue(px);
        double redness = (R - std::max(G, Bc)) / 255.0;
        if (redness < (1 - tol) * 0.4)
            return px;
        int nr = clamp255(R - (R - (G + Bc) / 2.0) * (0.5 + sat * 0.5));
        return qRgba(nr, G, Bc, qAlpha(px));
    });
}

static void fxClouds(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double scale = std::max(2.0, p["scale"].toDouble());
    double rough = p["power"].toDouble() / 100.0;
    int seed = p["seed"].toInt();
    int blend = p["blend"].toInt();
    QColor c1 = p["_primary"].value<QColor>(), c2 = p["_secondary"].value<QColor>();
    perPixel(s, d, r, [&](QRgb px, int x, int y) {
        double v = 0, amp = 1, freq = 1.0 / scale, norm = 0;
        for (int o = 0; o < 8; ++o) {
            v += valueNoise(x * freq, y * freq, seed + o * 31) * amp;
            norm += amp;
            amp *= rough;
            freq *= 2;
        }
        v /= norm;
        QRgb cl = lerpRgb(c1.rgba(), c2.rgba(), std::clamp((v - 0.5) * 1.6 + 0.5, 0.0, 1.0));
        switch (blend) {
        case 1: // multiply
            return qRgba(qRed(px) * qRed(cl) / 255, qGreen(px) * qGreen(cl) / 255, qBlue(px) * qBlue(cl) / 255, qAlpha(px));
        case 2: // screen
            return qRgba(255 - (255 - qRed(px)) * (255 - qRed(cl)) / 255, 255 - (255 - qGreen(px)) * (255 - qGreen(cl)) / 255,
                         255 - (255 - qBlue(px)) * (255 - qBlue(cl)) / 255, qAlpha(px));
        case 3: // overlay-ish average
            return lerpRgb(px, cl, 0.5);
        default:
            return cl;
        }
    });
}

static QRgb fractalColor(double t, bool invert)
{
    if (t < 0)
        return invert ? qRgba(255, 255, 255, 255) : qRgba(0, 0, 0, 255);
    double h = std::fmod(t * 0.02, 1.0);
    QColor c = QColor::fromHsvF(h, 0.85, std::min(1.0, 0.35 + t * 0.03));
    if (invert)
        return qRgba(255 - c.red(), 255 - c.green(), 255 - c.blue(), 255);
    return c.rgba();
}

static void fxMandelbrot(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double zoom = p["zoom"].toDouble();
    int quality = p["quality"].toInt();
    bool inv = p["invert"].toBool();
    int maxIt = 40 + int(p["factor"].toDouble()) * 20;
    double w = r.width(), h = r.height();
    perPixel(s, d, r, [&](QRgb, int x, int y) {
        double acc[3] = {0, 0, 0};
        for (int q = 0; q < quality * quality; ++q) {
            double sx = x + (q % quality + 0.5) / quality, sy = y + (q / quality + 0.5) / quality;
            double cx = -0.6 + (sx - r.left() - w / 2) / (w / 3.2) / zoom;
            double cy = (sy - r.top() - h / 2) / (w / 3.2) / zoom;
            double zx = 0, zy = 0;
            int i = 0;
            for (; i < maxIt && zx * zx + zy * zy < 16; ++i) {
                double t = zx * zx - zy * zy + cx;
                zy = 2 * zx * zy + cy;
                zx = t;
            }
            double sm = i >= maxIt ? -1 : i + 1 - std::log2(std::log2(std::max(1.0001, zx * zx + zy * zy)) / 2);
            QRgb c = fractalColor(sm, inv);
            acc[0] += qRed(c); acc[1] += qGreen(c); acc[2] += qBlue(c);
        }
        int n = quality * quality;
        return qRgba(clamp255(acc[0] / n), clamp255(acc[1] / n), clamp255(acc[2] / n), 255);
    });
}

static void fxJulia(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double zoom = p["zoom"].toDouble();
    int quality = p["quality"].toInt();
    double factor = p["factor"].toDouble();
    double w = r.width(), h = r.height();
    const double cr = -0.7 + factor * 0.01, ci = 0.27015;
    perPixel(s, d, r, [&](QRgb, int x, int y) {
        double acc[3] = {0, 0, 0};
        for (int q = 0; q < quality * quality; ++q) {
            double sx = x + (q % quality + 0.5) / quality, sy = y + (q / quality + 0.5) / quality;
            double zx = (sx - r.left() - w / 2) / (w / 3.0) / zoom;
            double zy = (sy - r.top() - h / 2) / (w / 3.0) / zoom;
            int i = 0;
            for (; i < 200 && zx * zx + zy * zy < 16; ++i) {
                double t = zx * zx - zy * zy + cr;
                zy = 2 * zx * zy + ci;
                zx = t;
            }
            double sm = i >= 200 ? -1 : i + 1 - std::log2(std::log2(std::max(1.0001, zx * zx + zy * zy)) / 2);
            QRgb c = fractalColor(sm * 2, false);
            acc[0] += qRed(c); acc[1] += qGreen(c); acc[2] += qBlue(c);
        }
        int n = quality * quality;
        return qRgba(clamp255(acc[0] / n), clamp255(acc[1] / n), clamp255(acc[2] / n), 255);
    });
}

// 3x3 directional kernel response helper
static void directional(const QImage &s, QImage &d, const QRect &r, double angleDeg,
                        const std::function<QRgb(QRgb, double, double, double)> &fn)
{
    double a = angleDeg * M_PI / 180;
    double dx = std::cos(a), dy = -std::sin(a);
    const int w = s.width(), h = s.height();
    parallelRows(r.top(), r.bottom(), [&](int y) {
        QRgb *o = reinterpret_cast<QRgb *>(const_cast<uchar *>(d.constScanLine(y)));
        const QRgb *row = reinterpret_cast<const QRgb *>(s.constScanLine(y));
        for (int x = r.left(); x <= r.right(); ++x) {
            double acc[3] = {0, 0, 0};
            for (int ky = -1; ky <= 1; ++ky)
                for (int kx = -1; kx <= 1; ++kx) {
                    double k = kx * dx + ky * dy;
                    if (k == 0)
                        continue;
                    QRgb px = reinterpret_cast<const QRgb *>(s.constScanLine(std::clamp(y + ky, 0, h - 1)))[std::clamp(x + kx, 0, w - 1)];
                    acc[0] += qRed(px) * k; acc[1] += qGreen(px) * k; acc[2] += qBlue(px) * k;
                }
            o[x] = fn(row[x], acc[0], acc[1], acc[2]);
        }
    });
}

static void fxEdgeDetect(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    directional(s, d, r, p["angle"].toDouble(), [](QRgb px, double a, double b, double c) {
        return qRgba(clamp255(128 + a), clamp255(128 + b), clamp255(128 + c), qAlpha(px));
    });
}

static void fxEmboss(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    directional(s, d, r, p["angle"].toDouble(), [](QRgb px, double a, double b, double c) {
        int v = clamp255(128 + (a * 0.299 + b * 0.587 + c * 0.114));
        return qRgba(v, v, v, qAlpha(px));
    });
}

static void fxRelief(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    directional(s, d, r, p["angle"].toDouble(), [](QRgb px, double a, double b, double c) {
        double l = (a * 0.299 + b * 0.587 + c * 0.114) * 0.5;
        return qRgba(clamp255(qRed(px) + l), clamp255(qGreen(px) + l), clamp255(qBlue(px) + l), qAlpha(px));
    });
}

static void fxOutline(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    int t = p["thickness"].toInt();
    double intensity = p["intensity"].toDouble() / 100.0;
    // local minimum (darkest neighbour) via two separable passes
    const int w = s.width(), h = s.height();
    QImage tmp(s.size(), QImage::Format_ARGB32);
    parallelRows(0, h - 1, [&](int y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(s.constScanLine(y));
        QRgb *o = reinterpret_cast<QRgb *>(const_cast<uchar *>(tmp.constScanLine(y)));
        for (int x = 0; x < w; ++x) {
            QRgb best = row[x];
            for (int k = -t; k <= t; ++k) {
                QRgb c = row[std::clamp(x + k, 0, w - 1)];
                if (lum(c) < lum(best)) best = c;
            }
            o[x] = best;
        }
    });
    perPixel(s, d, r, [&](QRgb px, int x, int y) {
        QRgb best = px;
        for (int k = -t; k <= t; ++k) {
            QRgb c = reinterpret_cast<const QRgb *>(tmp.constScanLine(std::clamp(y + k, 0, h - 1)))[x];
            if (lum(c) < lum(best)) best = c;
        }
        QRgb res = lerpRgb(px, best, intensity);
        return qRgba(qRed(res), qGreen(res), qBlue(res), qAlpha(px));
    });
}

static void fxOilPainting(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    const int rad = p["brush"].toInt();
    const int levels = std::clamp(p["coarseness"].toInt(), 3, 255);
    const int w = s.width(), h = s.height();
    parallelRows(r.top(), r.bottom(), [&](int y) {
        std::vector<int> cnt(levels), sr(levels), sg(levels), sb(levels);
        QRgb *o = reinterpret_cast<QRgb *>(const_cast<uchar *>(d.constScanLine(y)));
        for (int x = r.left(); x <= r.right(); ++x) {
            std::fill(cnt.begin(), cnt.end(), 0);
            std::fill(sr.begin(), sr.end(), 0);
            std::fill(sg.begin(), sg.end(), 0);
            std::fill(sb.begin(), sb.end(), 0);
            for (int yy = std::max(0, y - rad); yy <= std::min(h - 1, y + rad); ++yy) {
                const QRgb *row = reinterpret_cast<const QRgb *>(s.constScanLine(yy));
                for (int xx = std::max(0, x - rad); xx <= std::min(w - 1, x + rad); ++xx) {
                    QRgb c = row[xx];
                    int l = lum(c) * (levels - 1) / 255;
                    cnt[l]++; sr[l] += qRed(c); sg[l] += qGreen(c); sb[l] += qBlue(c);
                }
            }
            int best = int(std::max_element(cnt.begin(), cnt.end()) - cnt.begin());
            int n = std::max(1, cnt[best]);
            QRgb src = reinterpret_cast<const QRgb *>(s.constScanLine(y))[x];
            o[x] = qRgba(sr[best] / n, sg[best] / n, sb[best] / n, qAlpha(src));
        }
    });
}

static void fxPencilSketch(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    int tip = p["tip"].toInt();
    double range = p["range"].toDouble();
    QImage inv(s.size(), QImage::Format_ARGB32);
    perPixel(s, inv, s.rect(), [](QRgb px, int, int) { int l = 255 - lum(px); return qRgba(l, l, l, 255); });
    QImage blur = gaussianBlur(toPremul(inv), tip);
    perPixel(s, d, r, [&](QRgb px, int x, int y) {
        int base = lum(px);
        int b = qRed(reinterpret_cast<const QRgb *>(blur.constScanLine(y))[x]);
        double v = b >= 255 ? 255 : std::min(255.0, base * 255.0 / (255 - b));   // colour dodge
        v = std::clamp(v + range * 4, 0.0, 255.0);
        int iv = clamp255(v);
        return qRgba(iv, iv, iv, qAlpha(px));
    });
}

static void fxInkSketch(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    double ink = p["ink"].toDouble() / 100.0;
    double coloring = p["coloring"].toDouble() / 100.0;
    const int w = s.width(), h = s.height();
    perPixel(s, d, r, [&](QRgb px, int x, int y) {
        auto L = [&](int xx, int yy) {
            return lum(reinterpret_cast<const QRgb *>(s.constScanLine(std::clamp(yy, 0, h - 1)))[std::clamp(xx, 0, w - 1)]);
        };
        double gx = -L(x - 1, y - 1) - 2 * L(x - 1, y) - L(x - 1, y + 1) + L(x + 1, y - 1) + 2 * L(x + 1, y) + L(x + 1, y + 1);
        double gy = -L(x - 1, y - 1) - 2 * L(x, y - 1) - L(x + 1, y - 1) + L(x - 1, y + 1) + 2 * L(x, y + 1) + L(x + 1, y + 1);
        double e = std::min(1.0, std::sqrt(gx * gx + gy * gy) / 255.0 * (0.5 + ink * 2));
        int l = lum(px);
        QRgb base = lerpRgb(qRgba(l, l, l, 255), px, coloring);
        double f = 1 - e;
        return qRgba(clamp255(qRed(base) * f), clamp255(qGreen(base) * f), clamp255(qBlue(base) * f), qAlpha(px));
    });
}

// ---------------------------------------------------------------- texture maps

// height field in [0,1] from one channel of a straight-alpha image
static std::vector<float> heightField(const QImage &s, int source, bool invert)
{
    const int w = s.width(), h = s.height();
    std::vector<float> hf(size_t(w) * h);
    parallelRows(0, h - 1, [&](int y) {
        const QRgb *l = reinterpret_cast<const QRgb *>(s.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb p = l[x];
            float v;
            switch (source) {
            case 1: v = qRed(p) / 255.0f; break;
            case 2: v = qGreen(p) / 255.0f; break;
            case 3: v = qBlue(p) / 255.0f; break;
            case 4: v = qAlpha(p) / 255.0f; break;
            default: v = lum(p) / 255.0f * (qAlpha(p) / 255.0f); break;
            }
            hf[size_t(y) * w + x] = invert ? 1.0f - v : v;
        }
    });
    return hf;
}

// separable box blur (3 passes ~ gaussian) on a float field, optionally wrapping at the edges
static void blurField(std::vector<float> &f, int w, int h, int radius, bool wrap)
{
    if (radius <= 0)
        return;
    std::vector<float> tmp(f.size());
    auto idx = [&](int i, int n) { return wrap ? ((i % n) + n) % n : std::clamp(i, 0, n - 1); };
    const float div = 1.0f / (2 * radius + 1);
    for (int pass = 0; pass < 3; ++pass) {
        parallelRows(0, h - 1, [&](int y) {
            const float *row = &f[size_t(y) * w];
            float acc = 0;
            for (int i = -radius; i <= radius; ++i)
                acc += row[idx(i, w)];
            for (int x = 0; x < w; ++x) {
                tmp[size_t(y) * w + x] = acc * div;
                acc += row[idx(x + radius + 1, w)] - row[idx(x - radius, w)];
            }
        });
        parallelRows(0, w - 1, [&](int x) {
            float acc = 0;
            for (int i = -radius; i <= radius; ++i)
                acc += tmp[size_t(idx(i, h)) * w + x];
            for (int y = 0; y < h; ++y) {
                f[size_t(y) * w + x] = acc * div;
                acc += tmp[size_t(idx(y + radius + 1, h)) * w + x] - tmp[size_t(idx(y - radius, h)) * w + x];
            }
        });
    }
}

static void fxNormalMap(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    const int w = s.width(), h = s.height();
    const bool wrap = p["wrap"].toBool();
    std::vector<float> hf = heightField(s, p["source"].toInt(), p["invert"].toBool());
    blurField(hf, w, h, p["smooth"].toInt(), wrap);
    const float strength = float(p["strength"].toDouble());
    const float ySign = p["format"].toInt() == 1 ? -1.0f : 1.0f;     // DirectX flips green
    auto H = [&](int x, int y) {
        x = wrap ? ((x % w) + w) % w : std::clamp(x, 0, w - 1);
        y = wrap ? ((y % h) + h) % h : std::clamp(y, 0, h - 1);
        return hf[size_t(y) * w + x];
    };
    perPixel(s, d, r, [&](QRgb, int x, int y) {
        // Sobel gradient of the height field
        float dx = (H(x + 1, y - 1) + 2 * H(x + 1, y) + H(x + 1, y + 1)) - (H(x - 1, y - 1) + 2 * H(x - 1, y) + H(x - 1, y + 1));
        float dy = (H(x - 1, y + 1) + 2 * H(x, y + 1) + H(x + 1, y + 1)) - (H(x - 1, y - 1) + 2 * H(x, y - 1) + H(x + 1, y - 1));
        float nx = -dx * strength, ny = dy * strength * ySign, nz = 1.0f;
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        nx /= len; ny /= len; nz /= len;
        return qRgba(clamp255((nx * 0.5 + 0.5) * 255), clamp255((ny * 0.5 + 0.5) * 255), clamp255((nz * 0.5 + 0.5) * 255), 255);
    });
}

static void fxHeightMap(const QImage &s, QImage &d, const QRect &r, const QVariantMap &p)
{
    const int w = s.width(), h = s.height();
    std::vector<float> hf = heightField(s, p["source"].toInt(), p["invert"].toBool());
    blurField(hf, w, h, p["smooth"].toInt(), p["wrap"].toBool());
    const double c = p["contrast"].toDouble() / 100.0;
    const double k = c >= 0 ? 1 + c * 3 : 1 + c;
    perPixel(s, d, r, [&](QRgb, int x, int y) {
        int v = clamp255(((hf[size_t(y) * w + x] - 0.5) * k + 0.5) * 255);
        return qRgba(v, v, v, 255);
    });
}

static void fxNormalize(const QImage &s, QImage &d, const QRect &r, const QVariantMap &)
{
    perPixel(s, d, r, [](QRgb px, int, int) {
        double nx = qRed(px) / 127.5 - 1, ny = qGreen(px) / 127.5 - 1, nz = qBlue(px) / 127.5 - 1;
        nz = std::max(nz, 0.0);
        double len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len < 1e-6) { nx = ny = 0; nz = len = 1; }
        return qRgba(clamp255((nx / len * 0.5 + 0.5) * 255), clamp255((ny / len * 0.5 + 0.5) * 255),
                     clamp255((nz / len * 0.5 + 0.5) * 255), qAlpha(px));
    });
}

static void fxFlipGreen(const QImage &s, QImage &d, const QRect &r, const QVariantMap &)
{
    perPixel(s, d, r, [](QRgb px, int, int) { return qRgba(qRed(px), 255 - qGreen(px), qBlue(px), qAlpha(px)); });
}

// ============================================================ registry

const QVector<EffectDef> &effectRegistry()
{
    static QVector<EffectDef> reg;
    if (!reg.isEmpty())
        return reg;
    auto seed = [] { return P("seed", QObject::tr("Random seed"), 0, 99999, 0, EffectParam::Seed); };
    auto adj = [&](const QString &n, const QString &icon, QKeySequence ks, QVector<EffectParam> params, EffectFn fn) {
        EffectDef d;
        d.name = n; d.icon = icon; d.shortcut = ks; d.adjustment = true; d.params = params; d.apply = fn;
        reg << d;
    };
    auto fx = [&](const QString &sub, const QString &n, QVector<EffectParam> params, EffectFn fn) {
        EffectDef d;
        d.name = n; d.submenu = sub; d.icon = "fx_" + sub.toLower(); d.params = params; d.apply = fn;
        reg << d;
    };
    using QO = QObject;
    adj(QO::tr("Auto-Level"), "autolevel", QKeySequence("Ctrl+Shift+L"), {}, fxAutoLevel);
    adj(QO::tr("Black and White"), "blackwhite", QKeySequence("Ctrl+Shift+G"), {}, fxBlackWhite);
    adj(QO::tr("Black and Alpha"), "blackalpha", QKeySequence(), {}, fxBlackAlpha);
    adj(QO::tr("Brightness / Contrast"), "brightness", QKeySequence(),
        {P("brightness", QO::tr("Brightness"), -100, 100, 0), P("contrast", QO::tr("Contrast"), -100, 100, 0)}, fxBrightnessContrast);
    {
        EffectParam cp;
        cp.key = "curve"; cp.label = QO::tr("Curve"); cp.type = EffectParam::Curve;
        adj(QO::tr("Curves"), "curves", QKeySequence("Ctrl+Shift+M"),
            {C("mode", QO::tr("Transfer map"), {QO::tr("Luminosity"), QO::tr("RGB")}), cp}, fxCurves);
    }
    adj(QO::tr("Hue / Saturation"), "huesat", QKeySequence("Ctrl+Shift+U"),
        {P("hue", QO::tr("Hue"), -180, 180, 0), P("saturation", QO::tr("Saturation"), 0, 200, 100),
         P("lightness", QO::tr("Lightness"), -100, 100, 0)}, fxHueSat);
    adj(QO::tr("Invert Colors"), "invertcolors", QKeySequence("Ctrl+Shift+I"), {}, fxInvert);
    adj(QO::tr("Levels"), "levels", QKeySequence("Ctrl+L"),
        {P("inBlack", QO::tr("Input black"), 0, 254, 0), P("inWhite", QO::tr("Input white"), 1, 255, 255),
         P("gamma", QO::tr("Gamma"), 0.1, 10, 1, EffectParam::Double, 2),
         P("outBlack", QO::tr("Output black"), 0, 255, 0), P("outWhite", QO::tr("Output white"), 0, 255, 255)}, fxLevels);
    adj(QO::tr("Posterize"), "posterize", QKeySequence("Ctrl+Shift+P"),
        {P("red", QO::tr("Red"), 2, 64, 16), P("green", QO::tr("Green"), 2, 64, 16), P("blue", QO::tr("Blue"), 2, 64, 16),
         B("linked", QO::tr("Linked"), true)}, fxPosterize);
    adj(QO::tr("Sepia"), "sepia", QKeySequence("Ctrl+Shift+E"), {}, fxSepia);

    fx("Artistic", QO::tr("Ink Sketch"), {P("ink", QO::tr("Ink outline"), 0, 99, 50), P("coloring", QO::tr("Coloring"), 0, 100, 50)}, fxInkSketch);
    fx("Artistic", QO::tr("Oil Painting"), {P("brush", QO::tr("Brush size"), 1, 8, 3), P("coarseness", QO::tr("Coarseness"), 3, 255, 50)}, fxOilPainting);
    fx("Artistic", QO::tr("Pencil Sketch"), {P("tip", QO::tr("Pencil tip size"), 1, 20, 2), P("range", QO::tr("Color range"), -20, 20, 0)}, fxPencilSketch);

    fx("Blurs", QO::tr("Fragment"), {P("fragments", QO::tr("Fragments"), 2, 50, 4), P("distance", QO::tr("Distance"), 0, 100, 8),
                                      P("rotation", QO::tr("Rotation"), 0, 360, 0, EffectParam::Angle)}, fxFragment);
    fx("Blurs", QO::tr("Gaussian Blur"), {P("radius", QO::tr("Radius"), 0, 200, 2)}, fxGaussian);
    fx("Blurs", QO::tr("Motion Blur"), {P("angle", QO::tr("Angle"), -180, 180, 25, EffectParam::Angle),
                                         B("centered", QO::tr("Centered"), true), P("distance", QO::tr("Distance"), 1, 200, 10)}, fxMotionBlur);
    fx("Blurs", QO::tr("Radial Blur"), {P("angle", QO::tr("Angle"), 0, 360, 2, EffectParam::Angle),
                                         P("offsetX", QO::tr("Offset X"), -100, 100, 0), P("offsetY", QO::tr("Offset Y"), -100, 100, 0)}, fxRadialBlur);
    fx("Blurs", QO::tr("Unfocus"), {P("radius", QO::tr("Radius"), 1, 200, 4)}, fxUnfocus);
    fx("Blurs", QO::tr("Zoom Blur"), {P("amount", QO::tr("Zoom amount"), 0, 100, 10),
                                       P("offsetX", QO::tr("Offset X"), -100, 100, 0), P("offsetY", QO::tr("Offset Y"), -100, 100, 0)}, fxZoomBlur);

    fx("Distort", QO::tr("Bulge"), {P("amount", QO::tr("Amount"), -200, 100, 45),
                                     P("offsetX", QO::tr("Offset X"), -100, 100, 0), P("offsetY", QO::tr("Offset Y"), -100, 100, 0)}, fxBulge);
    fx("Distort", QO::tr("Frosted Glass"), {P("amount", QO::tr("Scatter radius"), 1, 50, 3), seed()}, fxFrostedGlass);
    fx("Distort", QO::tr("Pixelate"), {P("size", QO::tr("Cell size"), 1, 100, 2)}, fxPixelate);
    fx("Distort", QO::tr("Polar Inversion"), {P("amount", QO::tr("Amount"), 0.1, 4, 1, EffectParam::Double, 2)}, fxPolarInversion);
    fx("Distort", QO::tr("Twist"), {P("amount", QO::tr("Amount"), -100, 100, 30)}, fxTwist);

    fx("Noise", QO::tr("Add Noise"), {P("intensity", QO::tr("Intensity"), 0, 100, 64), P("saturation", QO::tr("Color saturation"), 0, 400, 100),
                                       P("coverage", QO::tr("Coverage"), 0, 100, 100), seed()}, fxAddNoise);
    fx("Noise", QO::tr("Median"), {P("radius", QO::tr("Radius"), 1, 50, 10), P("percentile", QO::tr("Percentile"), 0, 100, 50)}, fxMedian);

    fx("Photo", QO::tr("Glow"), {P("radius", QO::tr("Radius"), 1, 20, 6), P("brightness", QO::tr("Brightness"), -100, 100, 10),
                                  P("contrast", QO::tr("Contrast"), -100, 100, 10)}, fxGlow);
    fx("Photo", QO::tr("Red Eye Removal"), {P("tolerance", QO::tr("Tolerance"), 0, 100, 70), P("saturation", QO::tr("Saturation percentage"), 0, 100, 90)}, fxRedEye);
    fx("Photo", QO::tr("Sharpen"), {P("amount", QO::tr("Amount"), 1, 20, 10)}, fxSharpen);
    fx("Photo", QO::tr("Soften Portrait"), {P("softness", QO::tr("Softness"), 0, 10, 5), P("lighting", QO::tr("Lighting"), -20, 20, 0),
                                             P("warmth", QO::tr("Warmth"), 0, 20, 10)}, fxSoftenPortrait);
    fx("Photo", QO::tr("Vignette"), {P("radius", QO::tr("Radius"), 10, 200, 100), P("strength", QO::tr("Density"), 0, 100, 100),
                                      P("offsetX", QO::tr("Offset X"), -100, 100, 0), P("offsetY", QO::tr("Offset Y"), -100, 100, 0)}, fxVignette);

    fx("Render", QO::tr("Clouds"), {P("scale", QO::tr("Scale"), 2, 1000, 250), P("power", QO::tr("Power"), 0, 100, 50),
                                     C("blend", QO::tr("Blend mode"), {QO::tr("Normal"), QO::tr("Multiply"), QO::tr("Screen"), QO::tr("Average")}),
                                     seed()}, fxClouds);
    fx("Render", QO::tr("Julia Fractal"), {P("factor", QO::tr("Factor"), 1, 10, 4), P("quality", QO::tr("Quality"), 1, 5, 2),
                                            P("zoom", QO::tr("Zoom"), 0.1, 50, 1, EffectParam::Double, 1)}, fxJulia);
    fx("Render", QO::tr("Mandelbrot Fractal"), {P("factor", QO::tr("Factor"), 1, 10, 1), P("quality", QO::tr("Quality"), 1, 5, 2),
                                                 P("zoom", QO::tr("Zoom"), 0.1, 50, 1, EffectParam::Double, 1), B("invert", QO::tr("Invert colors"), false)}, fxMandelbrot);

    const QStringList sources = {QO::tr("Luminance"), QO::tr("Red"), QO::tr("Green"), QO::tr("Blue"), QO::tr("Alpha")};
    fx("Maps", QO::tr("Normal Map"), {P("strength", QO::tr("Strength"), 0.1, 30, 4, EffectParam::Double, 1),
                                       P("smooth", QO::tr("Smoothing radius"), 0, 20, 0), C("source", QO::tr("Height from"), sources),
                                       B("invert", QO::tr("Invert height"), false),
                                       C("format", QO::tr("Format"), {QO::tr("OpenGL (Y+)"), QO::tr("DirectX (Y-)")}),
                                       B("wrap", QO::tr("Seamless (wrap edges)"), true)}, fxNormalMap);
    fx("Maps", QO::tr("Height Map"), {C("source", QO::tr("Height from"), sources), B("invert", QO::tr("Invert"), false),
                                       P("smooth", QO::tr("Smoothing radius"), 0, 20, 0), P("contrast", QO::tr("Contrast"), -100, 100, 0),
                                       B("wrap", QO::tr("Seamless (wrap edges)"), true)}, fxHeightMap);
    fx("Maps", QO::tr("Normalize Normal Map"), {}, fxNormalize);
    fx("Maps", QO::tr("Flip Normal Green (OpenGL / DirectX)"), {}, fxFlipGreen);

    fx("Stylize", QO::tr("Edge Detect"), {P("angle", QO::tr("Angle"), -180, 180, 45, EffectParam::Angle)}, fxEdgeDetect);
    fx("Stylize", QO::tr("Emboss"), {P("angle", QO::tr("Angle"), -180, 180, 0, EffectParam::Angle)}, fxEmboss);
    fx("Stylize", QO::tr("Outline"), {P("thickness", QO::tr("Thickness"), 1, 50, 3), P("intensity", QO::tr("Intensity"), 0, 100, 50)}, fxOutline);
    fx("Stylize", QO::tr("Relief"), {P("angle", QO::tr("Angle"), -180, 180, 45, EffectParam::Angle)}, fxRelief);
    return reg;
}

void runEffect(Document *doc, const EffectDef &def, const QVariantMap &params, const QImage &backup)
{
    QImage src = backup.convertToFormat(QImage::Format_ARGB32);
    QImage dst = src.copy();
    QRect roi = doc->selectionBounds();
    if (roi.isEmpty())
        return;
    def.apply(src, dst, roi, params);
    if (doc->hasSelection()) {
        const QImage &mask = doc->selectionMask();
        for (int y = 0; y < dst.height(); ++y) {
            QRgb *d = reinterpret_cast<QRgb *>(dst.scanLine(y));
            const QRgb *s = reinterpret_cast<const QRgb *>(src.constScanLine(y));
            const uchar *m = mask.constScanLine(y);
            bool inRows = y >= roi.top() && y <= roi.bottom();
            for (int x = 0; x < dst.width(); ++x) {
                int mv = inRows && x >= roi.left() && x <= roi.right() ? m[x] : 0;
                if (mv == 255)
                    continue;
                d[x] = mv == 0 ? s[x] : lerpRgb(s[x], d[x], mv / 255.0);
            }
        }
    }
    doc->activeImage() = dst.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    doc->invalidate(roi);
}

// ============================================================ dialog

EffectDialog::EffectDialog(MainWindow *main, Document *doc, const EffectDef &def, const QVariantMap &initial, QWidget *parent)
    : QDialog(parent), m_main(main), m_doc(doc), m_def(def), m_values(initial)
{
    setWindowTitle(def.name);
    m_backup = doc->activeImage();
    m_timer.setSingleShot(true);
    m_timer.setInterval(40);
    connect(&m_timer, &QTimer::timeout, this, &EffectDialog::preview);

    auto *v = new QVBoxLayout(this);
    for (const EffectParam &p : def.params) {
        if (!m_values.contains(p.key)) {
            if (p.type == EffectParam::Curve)
                m_values[p.key] = QVariantList{QPointF(0, 0), QPointF(255, 255)};
            else
                m_values[p.key] = p.def;
        }
        auto *box = new QGroupBox(p.label);
        box->setFlat(true);
        auto *h = new QHBoxLayout(box);
        h->setContentsMargins(4, 4, 4, 4);
        switch (p.type) {
        case EffectParam::Int:
        case EffectParam::Double:
        case EffectParam::Angle: {
            const double scale = std::pow(10.0, p.decimals);
            QWidget *angleWidget = nullptr;
            QDial *dial = nullptr;
            if (p.type == EffectParam::Angle) {
                dial = new QDial;
                dial->setRange(-180, 180);
                dial->setWrapping(true);
                dial->setFixedSize(54, 54);
                dial->setNotchesVisible(true);
                angleWidget = dial;
            }
            auto *slider = new QSlider(Qt::Horizontal);
            slider->setRange(int(p.min * scale), int(p.max * scale));
            slider->setMinimumWidth(220);
            auto *spin = new QDoubleSpinBox;
            spin->setDecimals(p.decimals);
            spin->setRange(p.min, p.max);
            spin->setSingleStep(p.decimals ? std::pow(10.0, -p.decimals) * 10 : 1);
            spin->setFixedWidth(80);
            auto *reset = new QToolButton;
            reset->setText(QString::fromUtf8("↺"));
            reset->setToolTip(tr("Reset"));
            reset->setAutoRaise(true);
            double cur = m_values[p.key].toDouble();
            slider->setValue(int(std::round(cur * scale)));
            spin->setValue(cur);
            if (dial)
                dial->setValue(int(std::round(90 - cur)) > 180 ? int(std::round(90 - cur)) - 360 : int(std::round(90 - cur)));
            QString key = p.key;
            connect(slider, &QSlider::valueChanged, spin, [spin, scale](int x) { spin->setValue(x / scale); });
            connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [=](double x) {
                QSignalBlocker b(slider);
                slider->setValue(int(std::round(x * scale)));
                if (dial) {
                    QSignalBlocker bd(dial);
                    int dv = int(std::round(90 - x));
                    while (dv > 180) dv -= 360;
                    while (dv < -180) dv += 360;
                    dial->setValue(dv);
                }
                m_values[key] = p.decimals ? QVariant(x) : QVariant(int(std::round(x)));
                schedulePreview();
            });
            if (dial)
                connect(dial, &QDial::valueChanged, spin, [spin, p](int dv) {
                    double a = 90 - dv;
                    while (a > p.max) a -= 360;
                    while (a < p.min) a += 360;
                    spin->setValue(a);
                });
            double def = p.def;
            connect(reset, &QToolButton::clicked, spin, [spin, def] { spin->setValue(def); });
            if (angleWidget)
                h->addWidget(angleWidget);
            h->addWidget(slider, 1);
            h->addWidget(spin);
            h->addWidget(reset);
            break;
        }
        case EffectParam::Bool: {
            auto *cb = new QCheckBox(p.label);
            box->setTitle(QString());
            cb->setChecked(m_values[p.key].toBool());
            QString key = p.key;
            connect(cb, &QCheckBox::toggled, this, [this, key](bool on) {
                m_values[key] = on;
                schedulePreview();
            });
            h->addWidget(cb);
            break;
        }
        case EffectParam::Choice: {
            auto *combo = new QComboBox;
            combo->addItems(p.choices);
            combo->setCurrentIndex(m_values[p.key].toInt());
            QString key = p.key;
            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, key](int i) {
                m_values[key] = i;
                schedulePreview();
            });
            h->addWidget(combo);
            break;
        }
        case EffectParam::Seed: {
            auto *btn = new QPushButton(tr("Reseed"));
            QString key = p.key;
            connect(btn, &QPushButton::clicked, this, [this, key] {
                m_values[key] = int(QRandomGenerator::global()->bounded(100000));
                schedulePreview();
            });
            h->addWidget(btn);
            h->addStretch();
            break;
        }
        case EffectParam::Curve: {
            auto *ed = new CurveEditor;
            ed->setPoints(m_values[p.key].toList());
            QString key = p.key;
            connect(ed, &CurveEditor::changed, this, [this, ed, key] {
                m_values[key] = ed->points();
                schedulePreview();
            });
            h->addWidget(ed);
            break;
        }
        }
        v->addWidget(box);
    }
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(bb);
    preview();
}

void EffectDialog::schedulePreview() { m_timer.start(); }

void EffectDialog::preview()
{
    QVariantMap p = m_values;
    p["_primary"] = m_main->colors().primary;
    p["_secondary"] = m_main->colors().secondary;
    runEffect(m_doc, m_def, p, m_backup);
}

void EffectDialog::done(int r)
{
    m_timer.stop();
    if (r == QDialog::Accepted) {
        preview();
    } else {
        m_doc->activeImage() = m_backup;
        m_doc->invalidate();
    }
    QDialog::done(r);
}

// ============================================================ CurveEditor

CurveEditor::CurveEditor(QWidget *parent) : QWidget(parent)
{
    setFixedSize(262, 262);
    m_pts = {QPointF(0, 0), QPointF(255, 255)};
    setToolTip(tr("Click to add a control point, drag to move it, right-click to remove it."));
}

QVariantList CurveEditor::points() const
{
    QVariantList l;
    for (const QPointF &p : m_pts)
        l << p;
    return l;
}

void CurveEditor::setPoints(const QVariantList &pts)
{
    m_pts.clear();
    for (const QVariant &v : pts)
        m_pts << v.toPointF();
    if (m_pts.size() < 2)
        m_pts = {QPointF(0, 0), QPointF(255, 255)};
    update();
}

QPointF CurveEditor::toWidget(const QPointF &p) const { return QPointF(3 + p.x(), 3 + 255 - p.y()); }
QPointF CurveEditor::fromWidget(const QPointF &p) const
{
    return QPointF(std::clamp(p.x() - 3, 0.0, 255.0), std::clamp(255 - (p.y() - 3), 0.0, 255.0));
}

void CurveEditor::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    p.setPen(QColor(220, 220, 220));
    for (int i = 0; i <= 4; ++i) {
        double v = 3 + i * 255 / 4.0;
        p.drawLine(QPointF(v, 3), QPointF(v, 258));
        p.drawLine(QPointF(3, v), QPointF(258, v));
    }
    p.setPen(QColor(160, 160, 160));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    p.setPen(QPen(QColor(190, 190, 190), 1, Qt::DashLine));
    p.drawLine(toWidget(QPointF(0, 0)), toWidget(QPointF(255, 255)));
    QVector<int> lut = curveLut(points());
    QPolygonF poly;
    for (int x = 0; x < 256; ++x)
        poly << toWidget(QPointF(x, lut[x]));
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(20, 20, 20), 1.5));
    p.drawPolyline(poly);
    for (const QPointF &pt : m_pts) {
        p.setPen(QPen(Qt::black, 1));
        p.setBrush(Qt::white);
        p.drawEllipse(toWidget(pt), 4, 4);
    }
}

void CurveEditor::mousePressEvent(QMouseEvent *e)
{
    QPointF w = e->position();
    for (int i = 0; i < m_pts.size(); ++i)
        if (QLineF(toWidget(m_pts[i]), w).length() < 7) {
            if (e->button() == Qt::RightButton && m_pts.size() > 2) {
                m_pts.remove(i);
                update();
                emit changed();
                return;
            }
            m_drag = i;
            return;
        }
    if (e->button() == Qt::LeftButton) {
        m_pts << fromWidget(w);
        m_drag = m_pts.size() - 1;
        update();
        emit changed();
    }
}

void CurveEditor::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag < 0)
        return;
    m_pts[m_drag] = fromWidget(e->position());
    update();
    emit changed();
}

void CurveEditor::mouseReleaseEvent(QMouseEvent *) { m_drag = -1; }
