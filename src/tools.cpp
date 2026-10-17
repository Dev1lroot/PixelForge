#include "tools.h"
#include "mainwindow.h"
#include "document.h"
#include "canvas.h"
#include "settings.h"
#include "icons.h"
#include "parallel.h"

#include <QPainter>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QTextLayout>
#include <QFontMetricsF>
#include <QtMath>
#include <unordered_map>
#include <cmath>
#include <deque>

// ============================================================ shared helpers

QCursor Tool::cursor() const { return Icons::toolCursor(iconName()); }
QIcon Tool::icon() const { return Icons::get(iconName()); }
Document *Tool::doc() const { return main->document(); }
CanvasView *Tool::view() const { return main->view(); }
ToolSettings &Tool::settings() const { return main->settings(); }
ColorState &Tool::colors() const { return main->colors(); }
void Tool::setStatus(const QString &s) const { main->setToolStatus(s); }

static inline int colorDistanceCheck(QRgb a, QRgb b)
{
    int sum = 0, d;
    int aa = qAlpha(a);
    d = qRed(a) - qRed(b);   sum += (1 + d * d) * aa / 256;
    d = qGreen(a) - qGreen(b); sum += (1 + d * d) * aa / 256;
    d = qBlue(a) - qBlue(b); sum += (1 + d * d) * aa / 256;
    d = qAlpha(a) - qAlpha(b); sum += d * d;
    return sum;
}

static inline int toleranceLimit(int pct)
{
    double t = pct / 100.0;
    int tol = int(t * t * 256);
    return tol * tol * 4;
}

QImage floodMask(const QImage &srcIn, QPoint seed, int tolerancePct, bool global, const QImage *limitMask)
{
    QImage src = srcIn.convertToFormat(QImage::Format_ARGB32);
    const int w = src.width(), h = src.height();
    QImage mask(w, h, QImage::Format_Alpha8);
    mask.fill(0);
    if (!src.rect().contains(seed))
        return mask;
    const QRgb seedColor = src.pixel(seed);
    const int limit = toleranceLimit(tolerancePct);
    auto inLimit = [&](int x, int y) {
        return !limitMask || limitMask->constScanLine(y)[x] > 127;
    };
    auto match = [&](int x, int y) {
        const QRgb *l = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        return colorDistanceCheck(seedColor, l[x]) <= limit;
    };
    if (global) {
        for (int y = 0; y < h; ++y) {
            uchar *m = mask.scanLine(y);
            for (int x = 0; x < w; ++x)
                if (inLimit(x, y) && match(x, y))
                    m[x] = 255;
        }
        return mask;
    }
    if (!inLimit(seed.x(), seed.y()))
        return mask;
    std::vector<QPoint> stack;
    stack.push_back(seed);
    while (!stack.empty()) {
        QPoint p = stack.back();
        stack.pop_back();
        int y = p.y();
        uchar *m = mask.scanLine(y);
        if (m[p.x()])
            continue;
        int x1 = p.x(), x2 = p.x();
        while (x1 > 0 && !m[x1 - 1] && inLimit(x1 - 1, y) && match(x1 - 1, y))
            --x1;
        while (x2 < w - 1 && !m[x2 + 1] && inLimit(x2 + 1, y) && match(x2 + 1, y))
            ++x2;
        for (int x = x1; x <= x2; ++x)
            m[x] = 255;
        for (int ny : {y - 1, y + 1}) {
            if (ny < 0 || ny >= h)
                continue;
            const uchar *nm = mask.constScanLine(ny);
            bool inRun = false;
            for (int x = x1; x <= x2; ++x) {
                bool ok = !nm[x] && inLimit(x, ny) && match(x, ny);
                if (ok && !inRun) {
                    stack.push_back(QPoint(x, ny));
                    inRun = true;
                } else if (!ok) {
                    inRun = false;
                }
            }
        }
    }
    return mask;
}

// Traces pixel-exact outlines of an Alpha8 mask (value > 127 = inside).
QPainterPath maskToPath(const QImage &mask)
{
    const int w = mask.width(), h = mask.height();
    auto in = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && mask.constScanLine(y)[x] > 127;
    };
    const qint64 W1 = w + 1;
    std::unordered_multimap<qint64, qint64> edges;      // from vertex -> to vertex
    auto key = [&](int x, int y) { return qint64(y) * W1 + x; };
    for (int y = 0; y < h; ++y) {
        const uchar *row = mask.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            if (row[x] <= 127)
                continue;
            if (!in(x, y - 1)) edges.emplace(key(x, y), key(x + 1, y));
            if (!in(x + 1, y)) edges.emplace(key(x + 1, y), key(x + 1, y + 1));
            if (!in(x, y + 1)) edges.emplace(key(x + 1, y + 1), key(x, y + 1));
            if (!in(x - 1, y)) edges.emplace(key(x, y + 1), key(x, y));
        }
    }
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    while (!edges.empty()) {
        auto it = edges.begin();
        qint64 start = it->first, cur = it->second;
        edges.erase(it);
        QPolygonF poly;
        auto pt = [&](qint64 k) { return QPointF(double(k % W1), double(k / W1)); };
        poly << pt(start);
        QPointF prevDir;
        while (cur != start) {
            QPointF p = pt(cur);
            QPointF dir = p - poly.last();
            if (poly.size() >= 2 && dir.x() * prevDir.y() == dir.y() * prevDir.x() &&
                (dir.x() * prevDir.x() + dir.y() * prevDir.y()) > 0)
                poly.last() = p;                         // collinear: extend segment
            else
                poly << p;
            prevDir = poly.size() >= 2 ? poly.last() - poly[poly.size() - 2] : dir;
            auto nx = edges.find(cur);
            if (nx == edges.end())
                break;
            qint64 next = nx->second;
            edges.erase(nx);
            cur = next;
        }
        poly << poly.first();
        path.addPolygon(poly);
        path.closeSubpath();
    }
    return path;
}

QPainterPath combineSelection(const QPainterPath &oldSel, bool hadSel, const QPainterPath &n, int mode)
{
    if (!hadSel || mode == int(SelectionMode::Replace)) {
        if (!hadSel && (mode == int(SelectionMode::Exclude) || mode == int(SelectionMode::Intersect)))
            return mode == int(SelectionMode::Exclude) ? QPainterPath() : QPainterPath();
        return n;
    }
    switch (SelectionMode(mode)) {
    case SelectionMode::Union: return oldSel.united(n);
    case SelectionMode::Exclude: return oldSel.subtracted(n);
    case SelectionMode::Intersect: return oldSel.intersected(n);
    case SelectionMode::Xor: return oldSel.subtracted(n).united(n.subtracted(oldSel));
    default: return n;
    }
}

static int modeFromEvent(const ToolEvent &e, SelectionMode base)
{
    bool ctrl = e.mods & Qt::ControlModifier;
    bool alt = e.mods & Qt::AltModifier;
    bool right = e.button == Qt::RightButton;
    if (ctrl && right) return int(SelectionMode::Xor);
    if (right) return int(SelectionMode::Exclude);
    if (ctrl) return int(SelectionMode::Union);
    if (alt) return int(SelectionMode::Intersect);
    return int(base);
}

// Composites a temporary layer (premultiplied, painted with real colours) onto `layer`,
// restoring rect `r` from `backup` first.
// `temp` covers the document rect starting at `tempOrigin` (so it can be smaller than the canvas).
enum class CompMode { Normal, Erase };
static void compositeTemp(QImage &layer, const QImage &backup, const QImage &temp, QPoint tempOrigin, QRect r,
                          bool alphaBlend, qreal opacity, CompMode mode, Document *doc)
{
    r = r.intersected(layer.rect());
    if (r.isEmpty())
        return;
    QPainter p(&layer);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(r.topLeft(), backup, r);
    const QRect tr = r.intersected(QRect(tempOrigin, temp.size()));
    if (tr.isEmpty())
        return;
    const QRect src = tr.translated(-tempOrigin);
    doc->clipToSelection(p);
    if (mode == CompMode::Erase) {
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.setOpacity(opacity);
        p.drawImage(tr.topLeft(), temp, src);
    } else if (alphaBlend) {
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setOpacity(opacity);
        p.drawImage(tr.topLeft(), temp, src);
    } else {
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.drawImage(tr.topLeft(), temp, src);
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        p.setOpacity(opacity);
        p.drawImage(tr.topLeft(), temp, src);
    }
}

static void compositeTemp(QImage &layer, const QImage &backup, const QImage &temp, QRect r, bool alphaBlend,
                          qreal opacity, CompMode mode, Document *doc)
{
    compositeTemp(layer, backup, temp, QPoint(0, 0), r, alphaBlend, opacity, mode, doc);
}

QVector<QPointF> tileOffsets(const Document *d, const ToolSettings &s)
{
    if (!s.tileMode)
        return {QPointF(0, 0)};
    QVector<QPointF> v;
    for (int ty = -1; ty <= 1; ++ty)
        for (int tx = -1; tx <= 1; ++tx)
            v << QPointF(tx * d->width(), ty * d->height());
    return v;
}

static QPainterPath tiledPath(const QPainterPath &path, const Document *d, const ToolSettings &s)
{
    if (!s.tileMode)
        return path;
    QPainterPath out;
    const QRectF canvas(d->rect());
    for (const QPointF &o : tileOffsets(d, s)) {
        QPainterPath t = path.translated(o);
        if (t.controlPointRect().adjusted(-s.brushWidth, -s.brushWidth, s.brushWidth, s.brushWidth).intersects(canvas))
            out.addPath(t);
    }
    return out;
}

// transparent scratch image covering `r` (clipped to the canvas); paint with painter translated by -origin
static QImage scratch(const QRect &r)
{
    QImage img(std::max(1, r.width()), std::max(1, r.height()), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

static QColor opaque(QColor c)
{
    c.setAlpha(255);
    return c;
}

static QPen dashPen(const QColor &c, qreal w, int style)
{
    QPen pen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    switch (style) {
    case 1: pen.setStyle(Qt::DashLine); pen.setCapStyle(Qt::FlatCap); break;
    case 2: pen.setStyle(Qt::DotLine); pen.setCapStyle(Qt::FlatCap); break;
    case 3: pen.setStyle(Qt::DashDotLine); pen.setCapStyle(Qt::FlatCap); break;
    case 4: pen.setStyle(Qt::DashDotDotLine); pen.setCapStyle(Qt::FlatCap); break;
    default: break;
    }
    return pen;
}

static void drawHandle(QPainter &p, const QPointF &v, bool round = false)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(20, 20, 20), 1));
    p.setBrush(Qt::white);
    QRectF r(v.x() - 3.5, v.y() - 3.5, 7, 7);
    if (round)
        p.drawEllipse(r);
    else
        p.drawRect(r);
    p.restore();
}

static QRect strokeBounds(const QPointF &a, const QPointF &b, qreal width)
{
    qreal pad = width / 2 + 2;
    return QRectF(a, b).normalized().adjusted(-pad, -pad, pad, pad).toAlignedRect();
}

// ============================================================ selection tools

class SelectToolBase : public Tool
{
public:
    using Tool::Tool;
    quint32 options() const override { return OptSelectionMode; }

    void mousePress(CanvasView *, const ToolEvent &e) override
    {
        main->commitTool(this);
        m_doc = doc();
        m_oldSel = m_doc->selection();
        m_hadSel = m_doc->hasSelection();
        m_mode = modeFromEvent(e, settings().selectionMode);
        m_start = e.pos;
        m_cur = e.pos;
        m_moved = false;
        m_active = true;
        begin(e);
    }

    void mouseMove(CanvasView *, const ToolEvent &e) override
    {
        if (!m_active || m_doc != doc())
            return;
        if ((e.pos - m_start).manhattanLength() > 0.5)
            m_moved = true;
        m_cur = e.pos;
        update(e);
        if (m_moved) {
            QPainterPath shape = buildShape(e);
            m_doc->setSelection(combineSelection(m_oldSel, m_hadSel, shape, m_mode));
            QRectF b = shape.boundingRect();
            setStatus(QObject::tr("%1: %2 x %3").arg(name()).arg(int(b.width())).arg(int(b.height())));
        }
    }

    void mouseRelease(CanvasView *, const ToolEvent &e) override
    {
        if (!m_active || m_doc != doc())
            return;
        m_active = false;
        if (!m_moved) {
            if (m_hadSel && m_mode == int(SelectionMode::Replace)) {
                m_doc->clearSelection();
                m_doc->pushHistory(QObject::tr("Deselect"), Icons::get("deselect"));
            }
            return;
        }
        if (!m_doc->hasSelection() && !m_hadSel)
            return;
        m_doc->pushHistory(name(), icon());
        setStatus(helpText());
    }

    void reset() override { m_active = false; }

protected:
    virtual void begin(const ToolEvent &) {}
    virtual void update(const ToolEvent &) {}
    virtual QPainterPath buildShape(const ToolEvent &e) = 0;

    QRectF dragRect(const ToolEvent &e, bool snap) const
    {
        QPointF a = m_start, b = e.pos;
        if (e.mods & Qt::ShiftModifier) {
            qreal s = std::max(std::fabs(b.x() - a.x()), std::fabs(b.y() - a.y()));
            b = QPointF(a.x() + (b.x() >= a.x() ? s : -s), a.y() + (b.y() >= a.y() ? s : -s));
        }
        if (snap) {
            a = QPointF(std::round(a.x()), std::round(a.y()));
            b = QPointF(std::round(b.x()), std::round(b.y()));
        }
        QRectF r = QRectF(a, b).normalized();
        return r.intersected(QRectF(doc()->rect()));
    }

    Document *m_doc = nullptr;
    QPainterPath m_oldSel;
    bool m_hadSel = false;
    int m_mode = 0;
    QPointF m_start, m_cur;
    bool m_moved = false;
    bool m_active = false;
};

class RectSelectTool : public SelectToolBase
{
public:
    using SelectToolBase::SelectToolBase;
    ToolId id() const override { return ToolId::RectSelect; }
    QString name() const override { return QObject::tr("Rectangle Select"); }
    QString iconName() const override { return "rectselect"; }
    QChar shortcut() const override { return 'S'; }
    QString helpText() const override
    {
        return QObject::tr("Rectangle Select: Click and drag to select a rectangular region. "
                           "Hold Ctrl to add, right-click to subtract, hold Shift for a square.");
    }

protected:
    QPainterPath buildShape(const ToolEvent &e) override
    {
        QPainterPath p;
        QRectF r = dragRect(e, true);
        if (!r.isEmpty())
            p.addRect(r);
        return p;
    }
};

class EllipseSelectTool : public SelectToolBase
{
public:
    using SelectToolBase::SelectToolBase;
    ToolId id() const override { return ToolId::EllipseSelect; }
    QString name() const override { return QObject::tr("Ellipse Select"); }
    QString iconName() const override { return "ellipseselect"; }
    QChar shortcut() const override { return 'S'; }
    QString helpText() const override
    {
        return QObject::tr("Ellipse Select: Click and drag to select an elliptical region. Hold Shift for a circle.");
    }

protected:
    QPainterPath buildShape(const ToolEvent &e) override
    {
        QPainterPath p;
        QRectF r = dragRect(e, false);
        if (!r.isEmpty())
            p.addEllipse(r);
        return p;
    }
};

class LassoSelectTool : public SelectToolBase
{
public:
    using SelectToolBase::SelectToolBase;
    ToolId id() const override { return ToolId::Lasso; }
    QString name() const override { return QObject::tr("Lasso Select"); }
    QString iconName() const override { return "lasso"; }
    QChar shortcut() const override { return 'S'; }
    QString helpText() const override
    {
        return QObject::tr("Lasso Select: Click and drag to draw the outline of a selection area.");
    }

protected:
    void begin(const ToolEvent &e) override
    {
        m_poly.clear();
        m_poly << e.pos;
    }
    void update(const ToolEvent &e) override { m_poly << e.pos; }
    QPainterPath buildShape(const ToolEvent &) override
    {
        QPainterPath p;
        if (m_poly.size() > 2) {
            p.addPolygon(m_poly);
            p.closeSubpath();
            p = p.intersected(QPainterPath([this] { QPainterPath r; r.addRect(doc()->rect()); return r; }()));
        }
        return p;
    }

private:
    QPolygonF m_poly;
};

class MagicWandTool : public Tool
{
public:
    using Tool::Tool;
    TileBehavior tileBehavior() const override { return TileWrap; }
    ToolId id() const override { return ToolId::MagicWand; }
    QString name() const override { return QObject::tr("Magic Wand"); }
    QString iconName() const override { return "magicwand"; }
    QChar shortcut() const override { return 'S'; }
    quint32 options() const override { return OptSelectionMode | OptFloodMode | OptTolerance; }
    QString helpText() const override
    {
        return QObject::tr("Magic Wand: Click to select an area of similar color. Ctrl to add, right-click to subtract.");
    }

    void mousePress(CanvasView *, const ToolEvent &e) override
    {
        main->commitTool(this);
        Document *d = doc();
        QPoint pt(int(std::floor(e.pos.x())), int(std::floor(e.pos.y())));
        if (!d->rect().contains(pt))
            return;
        QImage mask = floodMask(d->activeImage(), pt, settings().tolerance, settings().globalFill);
        QPainterPath shape = maskToPath(mask);
        int mode = modeFromEvent(e, settings().selectionMode);
        d->setSelection(combineSelection(d->selection(), d->hasSelection(), shape, mode));
        d->pushHistory(name(), icon());
    }
};

// ============================================================ move / transform tools

static const int kHandleHit = 6;

class TransformToolBase : public Tool
{
public:
    using Tool::Tool;

    void paintOverlay(QPainter &p, CanvasView *v) override
    {
        if (!hasBox())
            return;
        QPolygonF q = viewQuad(v);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(0, 0, 0, 90), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(q);
        for (const QPointF &h : handles(q))
            drawHandle(p, h);
        p.restore();
    }

protected:
    virtual bool hasBox() const = 0;
    virtual void applyTransform() = 0;

    QPolygonF viewQuad(CanvasView *v) const
    {
        QPolygonF q;
        q << m_T.map(m_R.topLeft()) << m_T.map(m_R.topRight()) << m_T.map(m_R.bottomRight()) << m_T.map(m_R.bottomLeft());
        for (QPointF &pt : q)
            pt = v->imageToView(pt);
        return q;
    }

    static QVector<QPointF> handles(const QPolygonF &q)
    {
        auto mid = [](QPointF a, QPointF b) { return (a + b) / 2; };
        return {q[0], mid(q[0], q[1]), q[1], mid(q[1], q[2]), q[2], mid(q[2], q[3]), q[3], mid(q[3], q[0])};
    }

    int hitHandle(CanvasView *v, const QPointF &viewPos) const
    {
        if (!hasBox())
            return -1;
        auto hs = handles(viewQuad(v));
        for (int i = 0; i < hs.size(); ++i)
            if (std::fabs(hs[i].x() - viewPos.x()) <= kHandleHit && std::fabs(hs[i].y() - viewPos.y()) <= kHandleHit)
                return i;
        return -1;
    }

    void beginDrag(CanvasView *v, const ToolEvent &e)
    {
        m_T0 = m_T;
        m_start = e.pos;
        m_handle = e.button == Qt::LeftButton ? hitHandle(v, e.viewPos) : -1;
        m_rotate = e.button == Qt::RightButton;
        m_dragging = true;
    }

    void dragTo(const ToolEvent &e)
    {
        if (m_rotate) {
            QPointF c = m_T0.map(m_R.center());
            qreal a0 = std::atan2(m_start.y() - c.y(), m_start.x() - c.x());
            qreal a1 = std::atan2(e.pos.y() - c.y(), e.pos.x() - c.x());
            qreal deg = qRadiansToDegrees(a1 - a0);
            if (e.mods & Qt::ShiftModifier)
                deg = std::round(deg / 15.0) * 15.0;
            QTransform r = QTransform::fromTranslate(-c.x(), -c.y()) * QTransform().rotate(deg) *
                           QTransform::fromTranslate(c.x(), c.y());
            m_T = m_T0 * r;
            setStatus(QObject::tr("Rotate: %1°").arg(deg, 0, 'f', 1));
        } else if (m_handle >= 0) {
            QPointF q = m_T0.inverted().map(e.pos);
            qreal l = m_R.left(), t = m_R.top(), r = m_R.right(), b = m_R.bottom();
            switch (m_handle) {
            case 0: l = q.x(); t = q.y(); break;
            case 1: t = q.y(); break;
            case 2: r = q.x(); t = q.y(); break;
            case 3: r = q.x(); break;
            case 4: r = q.x(); b = q.y(); break;
            case 5: b = q.y(); break;
            case 6: l = q.x(); b = q.y(); break;
            case 7: l = q.x(); break;
            }
            qreal sx = (r - l) / m_R.width(), sy = (b - t) / m_R.height();
            if ((e.mods & Qt::ShiftModifier) && (m_handle % 2 == 0)) {
                qreal s = std::max(std::fabs(sx), std::fabs(sy));
                sx = sx < 0 ? -s : s;
                sy = sy < 0 ? -s : s;
                if (m_handle == 0 || m_handle == 6) l = r - sx * m_R.width();
                if (m_handle == 0 || m_handle == 2) t = b - sy * m_R.height();
            }
            if (std::fabs(sx) < 1e-3) sx = 1e-3;
            if (std::fabs(sy) < 1e-3) sy = 1e-3;
            QTransform s(sx, 0, 0, sy, l - m_R.left() * sx, t - m_R.top() * sy);
            m_T = s * m_T0;
            setStatus(QObject::tr("Scale: %1 x %2").arg(int(std::fabs(sx * m_R.width()))).arg(int(std::fabs(sy * m_R.height()))));
        } else {
            QPointF d = e.pos - m_start;
            if (m_T0.type() <= QTransform::TxTranslate)
                d = QPointF(std::round(d.x()), std::round(d.y()));
            m_T = m_T0 * QTransform::fromTranslate(d.x(), d.y());
            setStatus(QObject::tr("Offset: %1, %2").arg(int(std::round(d.x()))).arg(int(std::round(d.y()))));
        }
        applyTransform();
    }

    void hoverCursor(CanvasView *v, const ToolEvent &e)
    {
        int h = hitHandle(v, e.viewPos);
        if (h < 0) {
            v->viewport()->setCursor(cursor());
            return;
        }
        static const Qt::CursorShape shapes[] = {Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor,
                                                 Qt::SizeHorCursor, Qt::SizeFDiagCursor, Qt::SizeVerCursor,
                                                 Qt::SizeBDiagCursor, Qt::SizeHorCursor};
        v->viewport()->setCursor(shapes[h]);
    }

    QRectF m_R;                 // original box in image coords
    QTransform m_T, m_T0;
    QPointF m_start;
    int m_handle = -1;
    bool m_rotate = false;
    bool m_dragging = false;
};

class MoveTool : public TransformToolBase
{
public:
    using TransformToolBase::TransformToolBase;
    ToolId id() const override { return ToolId::Move; }
    QString name() const override { return QObject::tr("Move Selected Pixels"); }
    QString iconName() const override { return "move"; }
    QChar shortcut() const override { return 'M'; }
    quint32 options() const override { return OptResample; }
    QString helpText() const override
    {
        return QObject::tr("Move Selected Pixels: Drag the selection to move it. Drag the handles to scale. "
                           "Drag with the right mouse button to rotate.");
    }
    QCursor cursor() const override { return Icons::toolCursor("move", false); }
    bool isEditing() const override { return m_active; }

    void mousePress(CanvasView *v, const ToolEvent &e) override
    {
        if (m_active && m_doc != doc())
            reset();
        if (!m_active)
            lift();
        if (!m_active)
            return;
        beginDrag(v, e);
    }

    void mouseMove(CanvasView *v, const ToolEvent &e) override
    {
        if (!m_dragging) {
            hoverCursor(v, e);
            return;
        }
        if (m_doc != doc())
            return;
        dragTo(e);
    }

    void mouseRelease(CanvasView *, const ToolEvent &) override
    {
        if (!m_dragging)
            return;
        m_dragging = false;
        if (m_doc != doc())
            return;
        m_doc->pushHistory(name(), icon());
        setStatus(helpText());
    }

    bool keyPress(QKeyEvent *e) override
    {
        QPointF d;
        switch (e->key()) {
        case Qt::Key_Left: d = QPointF(-1, 0); break;
        case Qt::Key_Right: d = QPointF(1, 0); break;
        case Qt::Key_Up: d = QPointF(0, -1); break;
        case Qt::Key_Down: d = QPointF(0, 1); break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            commit();
            return true;
        default: return false;
        }
        if (!m_active)
            lift();
        if (!m_active)
            return true;
        m_T = m_T * QTransform::fromTranslate(d.x(), d.y());
        applyTransform();
        m_doc->pushHistory(name(), icon());
        return true;
    }

    void commit() override
    {
        if (!m_active)
            return;
        m_active = false;
        m_dragging = false;
        if (m_doc == doc()) {
            m_doc->pushHistory(QObject::tr("Finish Pixels"), Icons::get("finish"));
            view()->updateOverlay();
        }
        m_floating = QImage();
        m_base = QImage();
    }

    void reset() override
    {
        m_active = false;
        m_dragging = false;
        m_floating = QImage();
        m_base = QImage();
    }

    void prepareForHistory() override { reset(); }

    void beginPaste(const QImage &img, const QPoint &pos)
    {
        commit();
        m_doc = doc();
        m_layer = m_doc->activeIndex();
        m_floating = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        m_offset = pos;
        m_R = QRectF(pos, img.size());
        m_origPath = QPainterPath();
        m_origPath.addRect(m_R);
        m_base = m_doc->layer(m_layer).image;
        m_T = QTransform();
        m_active = true;
        applyTransform();
        m_doc->pushHistory(QObject::tr("Paste"), Icons::get("paste"));
    }

protected:
    bool hasBox() const override { return m_active || (doc() && doc()->hasSelection()); }

    void paintOverlay(QPainter &p, CanvasView *v) override
    {
        if (!m_active) {
            if (!doc()->hasSelection())
                return;
            m_R = doc()->selection().boundingRect();
            m_T = QTransform();
        }
        TransformToolBase::paintOverlay(p, v);
    }

    void lift()
    {
        m_doc = doc();
        m_layer = m_doc->activeIndex();
        QImage &layer = m_doc->layer(m_layer).image;
        QPainterPath sel = m_doc->selectionOrAll();
        QRect bounds = sel.boundingRect().toAlignedRect().intersected(m_doc->rect());
        if (bounds.isEmpty())
            return;
        m_origPath = sel;
        m_R = QRectF(bounds);
        m_offset = bounds.topLeft();
        const QImage &mask = m_doc->selectionMask();
        m_floating = layer.copy(bounds);
        {
            QPainter p(&m_floating);
            p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            p.drawImage(QPoint(0, 0), mask, bounds);
        }
        m_base = layer;
        {
            QPainter p(&m_base);
            p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            p.drawImage(bounds.topLeft(), mask, bounds);
        }
        m_T = QTransform();
        m_active = true;
    }

    void applyTransform() override
    {
        if (!m_active || m_doc != doc() || m_layer >= m_doc->layerCount())
            return;
        QImage &layer = m_doc->layer(m_layer).image;
        layer = m_base;
        {
            QPainter p(&layer);
            bool integral = m_T.type() <= QTransform::TxTranslate &&
                            m_T.dx() == std::round(m_T.dx()) && m_T.dy() == std::round(m_T.dy());
            p.setRenderHint(QPainter::SmoothPixmapTransform, settings().smoothResample && !integral);
            p.setRenderHint(QPainter::Antialiasing, settings().smoothResample && !integral);
            p.setTransform(m_T);
            p.drawImage(m_offset, m_floating);
        }
        m_doc->setSelection(m_T.map(m_origPath));
        m_doc->invalidate();
    }

private:
    Document *m_doc = nullptr;
    int m_layer = 0;
    bool m_active = false;
    QImage m_floating, m_base;
    QPoint m_offset;
    QPainterPath m_origPath;
};

void movetoolBeginPaste(Tool *t, const QImage &img, const QPoint &pos)
{
    static_cast<MoveTool *>(t)->beginPaste(img, pos);
}

class MoveSelectionTool : public TransformToolBase
{
public:
    using TransformToolBase::TransformToolBase;
    ToolId id() const override { return ToolId::MoveSelection; }
    QString name() const override { return QObject::tr("Move Selection"); }
    QString iconName() const override { return "moveselection"; }
    QChar shortcut() const override { return 'M'; }
    QString helpText() const override
    {
        return QObject::tr("Move Selection: Drag the selection outline to move it. Drag the handles to scale, "
                           "right-drag to rotate.");
    }
    QCursor cursor() const override { return Icons::toolCursor("moveselection", false); }

    void mousePress(CanvasView *v, const ToolEvent &e) override
    {
        main->commitTool(this);
        if (!doc()->hasSelection())
            return;
        m_doc = doc();
        m_orig = m_doc->selection();
        m_R = m_orig.boundingRect();
        m_T = QTransform();
        beginDrag(v, e);
    }

    void mouseMove(CanvasView *v, const ToolEvent &e) override
    {
        if (!m_dragging) {
            if (doc()->hasSelection()) {
                m_R = doc()->selection().boundingRect();
                m_T = QTransform();
            }
            hoverCursor(v, e);
            return;
        }
        if (m_doc != doc())
            return;
        dragTo(e);
    }

    void mouseRelease(CanvasView *, const ToolEvent &) override
    {
        if (!m_dragging)
            return;
        m_dragging = false;
        if (m_doc != doc())
            return;
        m_doc->pushHistory(name(), icon());
        setStatus(helpText());
    }

    void reset() override { m_dragging = false; }

protected:
    bool hasBox() const override { return doc() && doc()->hasSelection(); }
    void paintOverlay(QPainter &p, CanvasView *v) override
    {
        if (!m_dragging && doc()->hasSelection()) {
            m_R = doc()->selection().boundingRect();
            m_T = QTransform();
        }
        TransformToolBase::paintOverlay(p, v);
    }
    void applyTransform() override { m_doc->setSelection(m_T.map(m_orig)); }

private:
    Document *m_doc = nullptr;
    QPainterPath m_orig;
};

// ============================================================ view tools

class ZoomTool : public Tool
{
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Zoom; }
    QString name() const override { return QObject::tr("Zoom"); }
    QString iconName() const override { return "zoom"; }
    QChar shortcut() const override { return 'Z'; }
    QString helpText() const override
    {
        return QObject::tr("Zoom: Click left mouse button to zoom in, right mouse button to zoom out. "
                           "Click and drag to zoom into a region.");
    }
    QCursor cursor() const override { return Icons::toolCursor("zoomin", false); }

    void mousePress(CanvasView *, const ToolEvent &e) override
    {
        m_start = e.viewPos;
        m_cur = e.viewPos;
        m_button = e.button;
        m_drag = true;
    }
    void mouseMove(CanvasView *v, const ToolEvent &e) override
    {
        if (!m_drag)
            return;
        m_cur = e.viewPos;
        v->updateOverlay();
    }
    void mouseRelease(CanvasView *v, const ToolEvent &e) override
    {
        if (!m_drag)
            return;
        m_drag = false;
        QRectF r = QRectF(m_start, m_cur).normalized();
        if (m_button == Qt::LeftButton && r.width() > 4 && r.height() > 4) {
            v->zoomToRect(QRectF(v->viewToImage(r.topLeft()), v->viewToImage(r.bottomRight())));
        } else if (m_button == Qt::LeftButton) {
            v->zoomIn(e.viewPos);
        } else if (m_button == Qt::RightButton) {
            v->zoomOut(e.viewPos);
        }
        v->updateOverlay();
    }
    void paintOverlay(QPainter &p, CanvasView *) override
    {
        if (!m_drag || m_button != Qt::LeftButton)
            return;
        QRectF r = QRectF(m_start, m_cur).normalized();
        p.setPen(QPen(QColor(0, 0, 0), 1, Qt::DashLine));
        p.setBrush(QColor(60, 130, 230, 40));
        p.drawRect(r);
    }

private:
    QPointF m_start, m_cur;
    Qt::MouseButton m_button = Qt::NoButton;
    bool m_drag = false;
};

class PanTool : public Tool
{
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Pan; }
    QString name() const override { return QObject::tr("Pan"); }
    QString iconName() const override { return "pan"; }
    QChar shortcut() const override { return 'H'; }
    QString helpText() const override
    {
        return QObject::tr("Pan: Click and drag to navigate the image. Hold Space with any tool to pan.");
    }
    QCursor cursor() const override { return Qt::OpenHandCursor; }

    void mousePress(CanvasView *v, const ToolEvent &e) override
    {
        m_last = e.viewPos;
        m_drag = true;
        v->viewport()->setCursor(Qt::ClosedHandCursor);
    }
    void mouseMove(CanvasView *v, const ToolEvent &e) override
    {
        if (!m_drag)
            return;
        v->scrollBy(m_last - e.viewPos);
        m_last = e.viewPos;
    }
    void mouseRelease(CanvasView *v, const ToolEvent &) override
    {
        m_drag = false;
        v->viewport()->setCursor(cursor());
    }

private:
    QPointF m_last;
    bool m_drag = false;
};

// ============================================================ fill tools

class BucketTool : public Tool
{
public:
    using Tool::Tool;
    TileBehavior tileBehavior() const override { return TileWrap; }
    ToolId id() const override { return ToolId::Bucket; }
    QString name() const override { return QObject::tr("Paint Bucket"); }
    QString iconName() const override { return "bucket"; }
    QChar shortcut() const override { return 'F'; }
    quint32 options() const override { return OptFloodMode | OptFill | OptTolerance | OptAntialias | OptAlphaBlend; }
    QString helpText() const override
    {
        return QObject::tr("Paint Bucket: Left click to fill a region with the primary color, right click to fill "
                           "with the secondary color.");
    }

    void mousePress(CanvasView *, const ToolEvent &e) override
    {
        main->commitTool(this);
        Document *d = doc();
        QPoint pt(int(std::floor(e.pos.x())), int(std::floor(e.pos.y())));
        if (!d->rect().contains(pt))
            return;
        if (d->hasSelection() && !d->selection().contains(QPointF(pt) + QPointF(0.5, 0.5)))
            return;
        const QImage *limit = d->hasSelection() ? &d->selectionMask() : nullptr;
        QImage mask = floodMask(d->activeImage(), pt, settings().tolerance, settings().globalFill, limit);
        bool right = e.button == Qt::RightButton;
        QColor fg = right ? colors().secondary : colors().primary;
        QColor bg = right ? colors().primary : colors().secondary;

        QImage fill(d->size(), QImage::Format_ARGB32_Premultiplied);
        fill.fill(Qt::transparent);
        {
            QPainter p(&fill);
            p.fillRect(fill.rect(), makeFillBrush(settings().fillStyle, opaque(fg), bg));
            p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            p.drawImage(0, 0, mask);
        }
        QImage backup = d->activeImage();
        QImage &layer = d->activeImage();
        QRect bounds = maskToPath(mask).boundingRect().toAlignedRect();
        if (bounds.isEmpty())
            bounds = d->rect();
        compositeTemp(layer, backup, fill, bounds, settings().alphaBlend, fg.alphaF(), CompMode::Normal, d);
        d->invalidate(bounds);
        d->pushHistory(name(), icon());
    }
};

class GradientTool : public Tool
{
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Gradient; }
    QString name() const override { return QObject::tr("Gradient"); }
    QString iconName() const override { return "gradient"; }
    QChar shortcut() const override { return 'G'; }
    quint32 options() const override { return OptGradient | OptAlphaBlend; }
    QString helpText() const override
    {
        return QObject::tr("Gradient: Click and drag to draw a gradient from primary to secondary color. "
                           "Right mouse button reverses colors. Drag the handles to adjust.");
    }
    bool isEditing() const override { return m_editing; }

    void activate() override
    {
        if (m_hooked)
            return;
        m_hooked = true;
        connect(&settings(), &ToolSettings::changed, this, [this] {
            if (m_editing && m_doc == doc() && main->currentTool() == this) {
                render();
                m_doc->replaceTopHistory();
            }
        });
    }

    void mousePress(CanvasView *v, const ToolEvent &e) override
    {
        if (m_editing && m_doc == doc()) {
            QPointF a = v->imageToView(m_p1), b = v->imageToView(m_p2);
            if (QLineF(a, e.viewPos).length() <= kHandleHit) { m_dragHandle = 1; return; }
            if (QLineF(b, e.viewPos).length() <= kHandleHit) { m_dragHandle = 2; return; }
        }
        main->commitTool(this);
        commit();
        m_doc = doc();
        m_backup = m_doc->activeImage();
        m_src = m_backup.convertToFormat(QImage::Format_ARGB32);
        m_reverse = e.button == Qt::RightButton;
        m_p1 = m_p2 = e.pos;
        m_dragHandle = 2;
        m_editing = true;
        m_pushed = false;
    }

    void mouseMove(CanvasView *v, const ToolEvent &e) override
    {
        if (!m_editing || m_dragHandle == 0 || m_doc != doc())
            return;
        QPointF p = e.pos;
        if (e.mods & Qt::ShiftModifier) {
            QPointF o = m_dragHandle == 1 ? m_p2 : m_p1;
            QLineF l(o, p);
            l.setAngle(std::round(l.angle() / 15.0) * 15.0);
            p = l.p2();
        }
        (m_dragHandle == 1 ? m_p1 : m_p2) = p;
        render();
        v->updateOverlay();
    }

    void mouseRelease(CanvasView *, const ToolEvent &) override
    {
        if (!m_editing || m_dragHandle == 0 || m_doc != doc())
            return;
        m_dragHandle = 0;
        if (!m_pushed) {
            m_doc->pushHistory(name(), icon());
            m_pushed = true;
        } else {
            m_doc->replaceTopHistory();
        }
    }

    bool keyPress(QKeyEvent *e) override
    {
        if (m_editing && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
            commit();
            return true;
        }
        return false;
    }

    void commit() override
    {
        if (!m_editing)
            return;
        m_editing = false;
        m_backup = m_src = QImage();
        if (view())
            view()->updateOverlay();
    }
    void reset() override
    {
        m_editing = false;
        m_dragHandle = 0;
        m_backup = m_src = QImage();
    }
    void prepareForHistory() override { reset(); }

    void paintOverlay(QPainter &p, CanvasView *v) override
    {
        if (!m_editing || m_doc != doc())
            return;
        QPointF a = v->imageToView(m_p1), b = v->imageToView(m_p2);
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(0, 0, 0, 120), 1));
        p.drawLine(a, b);
        p.restore();
        drawHandle(p, a, true);
        drawHandle(p, b, true);
    }

private:
    void render()
    {
        Document *d = m_doc;
        QImage &layer = d->activeImage();
        const QRect bounds = d->selectionBounds();
        if (bounds.isEmpty())
            return;
        QColor c1 = colors().primary, c2 = colors().secondary;
        if (m_reverse)
            std::swap(c1, c2);
        const bool transparency = settings().gradientTransparency;
        const GradientType type = settings().gradientType;
        const double dx = m_p2.x() - m_p1.x(), dy = m_p2.y() - m_p1.y();
        const double len2 = std::max(1e-9, dx * dx + dy * dy);
        const double len = std::sqrt(len2);
        const double baseAngle = std::atan2(dy, dx);

        const QImage &src = m_src;
        QImage out = src.copy(bounds);
        const QImage &mask = d->selectionMask();
        const bool blend = settings().alphaBlend;
        const int a1 = c1.alpha(), a2 = c2.alpha();

        uchar *outBits = out.bits();               // detach once before threads write rows
        const qsizetype outStride = out.bytesPerLine();
        parallelRows(bounds.top(), bounds.bottom(), [&](int y) {
            const QRgb *s = reinterpret_cast<const QRgb *>(src.constScanLine(y));
            QRgb *o = reinterpret_cast<QRgb *>(outBits + (y - bounds.top()) * outStride) - bounds.left();
            const uchar *m = mask.constScanLine(y);
            for (int x = bounds.left(); x <= bounds.right(); ++x) {
                if (!m[x])
                    continue;
                double px = x + 0.5 - m_p1.x(), py = y + 0.5 - m_p1.y();
                double t;
                switch (type) {
                case GradientType::Linear: t = (px * dx + py * dy) / len2; break;
                case GradientType::LinearReflected: t = std::fabs((px * dx + py * dy) / len2); break;
                case GradientType::Diamond: {
                    double u = (px * dx + py * dy) / len, v = (-px * dy + py * dx) / len;
                    t = (std::fabs(u) + std::fabs(v)) / len;
                    break;
                }
                case GradientType::Radial: t = std::sqrt(px * px + py * py) / len; break;
                case GradientType::Conical:
                default: {
                    double a = std::atan2(py, px) - baseAngle;
                    while (a > M_PI) a -= 2 * M_PI;
                    while (a < -M_PI) a += 2 * M_PI;
                    t = std::fabs(a) / M_PI;
                    break;
                }
                }
                t = std::clamp(t, 0.0, 1.0);
                QRgb sp = s[x];
                QRgb res;
                if (transparency) {
                    double f = (a1 + (a2 - a1) * t) / 255.0;   // alpha multiplier
                    res = qRgba(qRed(sp), qGreen(sp), qBlue(sp), int(qAlpha(sp) * f + 0.5));
                } else {
                    int r = int(c1.red() + (c2.red() - c1.red()) * t + 0.5);
                    int g = int(c1.green() + (c2.green() - c1.green()) * t + 0.5);
                    int b = int(c1.blue() + (c2.blue() - c1.blue()) * t + 0.5);
                    int a = int(a1 + (a2 - a1) * t + 0.5);
                    if (blend && a < 255) {
                        // source-over in straight alpha
                        double sa = a / 255.0, da = qAlpha(sp) / 255.0;
                        double oa = sa + da * (1 - sa);
                        if (oa <= 0) {
                            res = 0;
                        } else {
                            auto ch = [&](int sc, int dc) { return int((sc * sa + dc * da * (1 - sa)) / oa + 0.5); };
                            res = qRgba(ch(r, qRed(sp)), ch(g, qGreen(sp)), ch(b, qBlue(sp)), int(oa * 255 + 0.5));
                        }
                    } else {
                        res = qRgba(r, g, b, a);
                    }
                }
                int mv = m[x];
                if (mv < 255) {
                    auto lerp = [mv](int a, int b) { return (a * (255 - mv) + b * mv) / 255; };
                    res = qRgba(lerp(qRed(sp), qRed(res)), lerp(qGreen(sp), qGreen(res)),
                                lerp(qBlue(sp), qBlue(res)), lerp(qAlpha(sp), qAlpha(res)));
                }
                o[x] = res;
            }
        });
        // only the selection bounds changed: write them back into the premultiplied layer
        {
            QPainter p(&layer);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.drawImage(bounds.topLeft(), out);
        }
        d->invalidate(bounds);
    }

    Document *m_doc = nullptr;
    QImage m_backup, m_src;
    QPointF m_p1, m_p2;
    int m_dragHandle = 0;
    bool m_editing = false;
    bool m_reverse = false;
    bool m_pushed = false;
};

// ============================================================ painting tools

class StrokeTool : public Tool
{
public:
    using Tool::Tool;

    void mousePress(CanvasView *, const ToolEvent &e) override
    {
        main->commitTool(this);
        if (m_active)
            return;
        m_doc = doc();
        m_backup = m_doc->activeImage();
        m_stroke = QImage(m_doc->size(), QImage::Format_ARGB32_Premultiplied);
        m_stroke.fill(Qt::transparent);
        m_right = e.button == Qt::RightButton;
        m_last = e.pos;
        m_active = true;
        if (!beginStroke(e)) {
            m_active = false;
            return;
        }
        segment(e.pos, e.pos);
    }

    void mouseMove(CanvasView *v, const ToolEvent &e) override
    {
        if (!m_active) {
            if (showsBrushCursor())
                v->updateOverlay();
            return;
        }
        if (m_doc != doc())
            return;
        segment(m_last, e.pos);
        m_last = e.pos;
        if (showsBrushCursor())
            v->updateOverlay();
    }

    void mouseRelease(CanvasView *, const ToolEvent &) override
    {
        if (!m_active)
            return;
        m_active = false;
        if (m_doc != doc())
            return;
        m_doc->pushHistory(name(), icon());
        m_backup = QImage();
        m_stroke = QImage();
    }

    void reset() override
    {
        m_active = false;
        m_backup = QImage();
        m_stroke = QImage();
    }

    void paintOverlay(QPainter &p, CanvasView *v) override
    {
        if (!showsBrushCursor() || !v->mouseInside())
            return;
        qreal r = settings().brushWidth * v->zoom() / 2.0;
        if (r < 3)
            return;
        QPointF c = v->imageToView(v->mouseImagePos());
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 180), 1));
        p.drawEllipse(c, r + 0.5, r + 0.5);
        p.setPen(QPen(QColor(0, 0, 0, 180), 1));
        p.drawEllipse(c, r - 0.5, r - 0.5);
        p.restore();
    }

protected:
    virtual bool beginStroke(const ToolEvent &) { return true; }
    virtual void segmentAt(const QPointF &a, const QPointF &b) = 0;

    // in tile mode every segment is also painted on the wrapped copies so strokes cross edges seamlessly
    void segment(const QPointF &a, const QPointF &b)
    {
        const qreal pad = settings().brushWidth / 2.0 + 2;
        for (const QPointF &o : tileOffsets(m_doc, settings())) {
            QRectF r = QRectF(a + o, b + o).normalized().adjusted(-pad, -pad, pad, pad);
            if (r.intersects(QRectF(m_doc->rect())))
                segmentAt(a + o, b + o);
        }
    }
    virtual bool showsBrushCursor() const { return true; }

    QColor strokeColor() const { return m_right ? colors().secondary : colors().primary; }

    // paints a round segment into the stroke buffer and composites the touched area
    void paintSegment(const QPointF &a, const QPointF &b, qreal width, bool aa, CompMode mode,
                      qreal opacity, bool blend)
    {
        QRect r = strokeBounds(a, b, width);
        {
            QPainter p(&m_stroke);
            p.setRenderHint(QPainter::Antialiasing, aa);
            p.setPen(QPen(Qt::black, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (a == b) {
                p.setPen(Qt::NoPen);
                p.setBrush(Qt::black);
                p.drawEllipse(a, width / 2, width / 2);
            } else {
                p.drawLine(a, b);
            }
        }
        if (mode == CompMode::Normal) {
            // recolour the coverage buffer with the stroke colour
            QPainter p(&m_stroke);
            p.setCompositionMode(QPainter::CompositionMode_SourceIn);
            p.fillRect(r, opaque(strokeColor()));
        }
        compositeTemp(m_doc->activeImage(), m_backup, m_stroke, r, blend, opacity, mode, m_doc);
        m_doc->invalidate(r);
    }

    Document *m_doc = nullptr;
    QImage m_backup, m_stroke;
    QPointF m_last;
    bool m_right = false;
    bool m_active = false;
};

class BrushTool : public StrokeTool
{
public:
    using StrokeTool::StrokeTool;
    ToolId id() const override { return ToolId::Brush; }
    QString name() const override { return QObject::tr("Paintbrush"); }
    QString iconName() const override { return "brush"; }
    QChar shortcut() const override { return 'B'; }
    quint32 options() const override { return OptBrushWidth | OptAntialias | OptAlphaBlend; }
    QString helpText() const override
    {
        return QObject::tr("Paintbrush: Left click to draw with primary color, right click to draw with secondary color");
    }

protected:
    void segmentAt(const QPointF &a, const QPointF &b) override
    {
        QColor c = strokeColor();
        paintSegment(a, b, settings().brushWidth, settings().antialias, CompMode::Normal, c.alphaF(),
                     settings().alphaBlend);
    }
};

class EraserTool : public StrokeTool
{
public:
    using StrokeTool::StrokeTool;
    ToolId id() const override { return ToolId::Eraser; }
    QString name() const override { return QObject::tr("Eraser"); }
    QString iconName() const override { return "eraser"; }
    QChar shortcut() const override { return 'E'; }
    quint32 options() const override { return OptBrushWidth | OptAntialias; }
    QString helpText() const override
    {
        return QObject::tr("Eraser: Click and drag to erase a portion of the image to transparency.");
    }

protected:
    void segmentAt(const QPointF &a, const QPointF &b) override
    {
        paintSegment(a, b, settings().brushWidth, settings().antialias, CompMode::Erase, 1.0, true);
    }
};

class PencilTool : public StrokeTool
{
public:
    using StrokeTool::StrokeTool;
    ToolId id() const override { return ToolId::Pencil; }
    QString name() const override { return QObject::tr("Pencil"); }
    QString iconName() const override { return "pencil"; }
    QChar shortcut() const override { return 'P'; }
    quint32 options() const override { return OptAlphaBlend; }
    QString helpText() const override
    {
        return QObject::tr("Pencil: Left click to draw a freehand one-pixel wide line with the primary color, "
                           "right click to use the secondary color.");
    }

protected:
    bool showsBrushCursor() const override { return false; }
    void segmentAt(const QPointF &a0, const QPointF &b0) override
    {
        QPointF a(std::floor(a0.x()) + 0.5, std::floor(a0.y()) + 0.5);
        QPointF b(std::floor(b0.x()) + 0.5, std::floor(b0.y()) + 0.5);
        QRect r = strokeBounds(a, b, 2);
        {
            QPainter p(&m_stroke);
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(QPen(opaque(strokeColor()), 1));
            if (a == b)
                p.drawPoint(QPoint(int(a.x()), int(a.y())));
            else
                p.drawLine(QPoint(int(a.x()), int(a.y())), QPoint(int(b.x()), int(b.y())));
        }
        compositeTemp(m_doc->activeImage(), m_backup, m_stroke, r, settings().alphaBlend,
                      strokeColor().alphaF(), CompMode::Normal, m_doc);
        m_doc->invalidate(r);
    }
};

class CloneStampTool : public StrokeTool
{
public:
    using StrokeTool::StrokeTool;
    ToolId id() const override { return ToolId::CloneStamp; }
    QString name() const override { return QObject::tr("Clone Stamp"); }
    QString iconName() const override { return "clonestamp"; }
    QChar shortcut() const override { return 'L'; }
    quint32 options() const override { return OptBrushWidth | OptAntialias | OptAlphaBlend; }
    QString helpText() const override
    {
        return QObject::tr("Clone Stamp: Ctrl-click to set origin, then left click to start painting.");
    }

    void mousePress(CanvasView *v, const ToolEvent &e) override
    {
        if (e.mods & Qt::ControlModifier) {
            m_source = e.pos;
            m_sourceDoc = doc();
            m_sourceLayer = doc()->activeIndex();
            m_hasSource = true;
            m_offsetSet = false;
            v->updateOverlay();
            return;
        }
        StrokeTool::mousePress(v, e);
    }

    void paintOverlay(QPainter &p, CanvasView *v) override
    {
        StrokeTool::paintOverlay(p, v);
        if (!m_hasSource || m_sourceDoc != doc())
            return;
        QPointF s = m_source;
        if (m_active)
            s = m_last + m_offset;
        QPointF c = v->imageToView(s);
        p.save();
        p.setPen(QPen(Qt::white, 3));
        p.drawLine(c - QPointF(6, 0), c + QPointF(6, 0));
        p.drawLine(c - QPointF(0, 6), c + QPointF(0, 6));
        p.setPen(QPen(Qt::black, 1));
        p.drawLine(c - QPointF(6, 0), c + QPointF(6, 0));
        p.drawLine(c - QPointF(0, 6), c + QPointF(0, 6));
        p.restore();
    }

protected:
    bool beginStroke(const ToolEvent &e) override
    {
        if (!m_hasSource || m_sourceDoc != m_doc) {
            setStatus(QObject::tr("Clone Stamp: Ctrl-click to set the source location first."));
            return false;
        }
        if (!m_offsetSet) {
            m_offset = m_source - e.pos;
            m_offsetSet = true;
        }
        m_src = m_sourceLayer < m_doc->layerCount() ? m_doc->layer(m_sourceLayer).image : m_backup;
        if (m_sourceLayer == m_doc->activeIndex())
            m_src = m_backup;
        return true;
    }

    void segmentAt(const QPointF &a, const QPointF &b) override
    {
        qreal width = settings().brushWidth;
        QRect r = strokeBounds(a, b, width).intersected(m_doc->rect());
        {
            QPainter p(&m_stroke);
            p.setRenderHint(QPainter::Antialiasing, settings().antialias);
            p.setPen(QPen(Qt::black, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (a == b) {
                p.setPen(Qt::NoPen);
                p.setBrush(Qt::black);
                p.drawEllipse(a, width / 2, width / 2);
            } else {
                p.drawLine(a, b);
            }
        }
        if (r.isEmpty())
            return;
        // temp = (source shifted) masked by stroke coverage
        QImage temp = scratch(r);
        {
            QPainter p(&temp);
            p.translate(-r.topLeft());
            for (const QPointF &o : tileOffsets(m_doc, settings()))
                p.drawImage(QPointF(-m_offset.x(), -m_offset.y()) + o, m_src);
            p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            p.drawImage(r.topLeft(), m_stroke, r);
        }
        // accumulate: draw onto a persistent result so overlapping dabs don't re-blend
        if (m_result.isNull() || m_result.size() != m_doc->size()) {
            m_result = QImage(m_doc->size(), QImage::Format_ARGB32_Premultiplied);
            m_result.fill(Qt::transparent);
        }
        {
            QPainter p(&m_result);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.drawImage(r.topLeft(), temp);
        }
        compositeTemp(m_doc->activeImage(), m_backup, m_result, r, settings().alphaBlend, 1.0, CompMode::Normal, m_doc);
        m_doc->invalidate(r);
    }

    void reset() override
    {
        StrokeTool::reset();
        m_result = QImage();
    }

    void mouseRelease(CanvasView *v, const ToolEvent &e) override
    {
        StrokeTool::mouseRelease(v, e);
        m_result = QImage();
        m_src = QImage();
    }

private:
    QPointF m_source, m_offset;
    Document *m_sourceDoc = nullptr;
    int m_sourceLayer = 0;
    bool m_hasSource = false, m_offsetSet = false;
    QImage m_src, m_result;
};

class RecolorTool : public StrokeTool
{
public:
    using StrokeTool::StrokeTool;
    ToolId id() const override { return ToolId::Recolor; }
    QString name() const override { return QObject::tr("Recolor"); }
    QString iconName() const override { return "recolor"; }
    QChar shortcut() const override { return 'R'; }
    quint32 options() const override { return OptBrushWidth | OptTolerance | OptAntialias; }
    QString helpText() const override
    {
        return QObject::tr("Recolor: Left click to replace the secondary color with the primary color, "
                           "right click to do the reverse.");
    }

protected:
    bool beginStroke(const ToolEvent &) override
    {
        m_src = m_backup.convertToFormat(QImage::Format_ARGB32);
        m_work = m_src.copy();
        return true;
    }

    void segmentAt(const QPointF &a, const QPointF &b) override
    {
        const qreal radius = std::max(0.5, settings().brushWidth / 2.0);
        QColor from = m_right ? colors().primary : colors().secondary;
        QColor to = m_right ? colors().secondary : colors().primary;
        const QRgb fromRgb = from.rgba();
        const int limit = toleranceLimit(settings().tolerance);
        QRect r = strokeBounds(a, b, radius * 2).intersected(m_doc->rect());
        const QImage &mask = m_doc->selectionMask();
        QLineF seg(a, b);
        const qreal segLen2 = seg.dx() * seg.dx() + seg.dy() * seg.dy();
        for (int y = r.top(); y <= r.bottom(); ++y) {
            const QRgb *s = reinterpret_cast<const QRgb *>(m_src.constScanLine(y));
            QRgb *w = reinterpret_cast<QRgb *>(m_work.scanLine(y));
            const uchar *m = mask.constScanLine(y);
            for (int x = r.left(); x <= r.right(); ++x) {
                if (!m[x])
                    continue;
                // distance from pixel centre to segment
                QPointF pc(x + 0.5, y + 0.5);
                qreal t = segLen2 > 0 ? ((pc.x() - a.x()) * seg.dx() + (pc.y() - a.y()) * seg.dy()) / segLen2 : 0;
                t = std::clamp(t, 0.0, 1.0);
                QPointF q = a + t * QPointF(seg.dx(), seg.dy());
                qreal dist = QLineF(pc, q).length();
                if (dist > radius + 0.5)
                    continue;
                QRgb sp = s[x];
                if (colorDistanceCheck(fromRgb, sp) > limit)
                    continue;
                int nr = std::clamp(to.red() + qRed(sp) - from.red(), 0, 255);
                int ng = std::clamp(to.green() + qGreen(sp) - from.green(), 0, 255);
                int nb = std::clamp(to.blue() + qBlue(sp) - from.blue(), 0, 255);
                double cov = settings().antialias ? std::clamp(radius + 0.5 - dist, 0.0, 1.0) : 1.0;
                cov *= m[x] / 255.0;
                auto mix = [cov](int o, int n) { return int(o + (n - o) * cov + 0.5); };
                QRgb np = qRgba(mix(qRed(sp), nr), mix(qGreen(sp), ng), mix(qBlue(sp), nb), qAlpha(sp));
                // keep the strongest application so far
                if (cov >= 1.0 || qAbs(qRed(np) - qRed(sp)) + qAbs(qGreen(np) - qGreen(sp)) + qAbs(qBlue(np) - qBlue(sp)) >
                                      qAbs(qRed(w[x]) - qRed(sp)) + qAbs(qGreen(w[x]) - qGreen(sp)) + qAbs(qBlue(w[x]) - qBlue(sp)))
                    w[x] = np;
            }
        }
        QImage &layer = m_doc->activeImage();
        QPainter p(&layer);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(r.topLeft(), m_work.copy(r).convertToFormat(QImage::Format_ARGB32_Premultiplied));
        p.end();
        m_doc->invalidate(r);
    }

    void mouseRelease(CanvasView *v, const ToolEvent &e) override
    {
        StrokeTool::mouseRelease(v, e);
        m_src = m_work = QImage();
    }

private:
    QImage m_src, m_work;
};

// ============================================================ colour picker

class ColorPickerTool : public Tool
{
public:
    using Tool::Tool;
    TileBehavior tileBehavior() const override { return TileWrap; }
    ToolId id() const override { return ToolId::ColorPicker; }
    QString name() const override { return QObject::tr("Color Picker"); }
    QString iconName() const override { return "colorpicker"; }
    QChar shortcut() const override { return 'K'; }
    quint32 options() const override { return OptSampling; }
    QString helpText() const override
    {
        return QObject::tr("Color Picker: Left click to set primary color. Right click to set secondary color.");
    }
    QCursor cursor() const override { return Icons::toolCursor("colorpicker"); }

    void mousePress(CanvasView *, const ToolEvent &e) override
    {
        m_button = e.button;
        pick(e);
    }
    void mouseMove(CanvasView *, const ToolEvent &e) override
    {
        if (e.buttons & (Qt::LeftButton | Qt::RightButton))
            pick(e);
    }
    void mouseRelease(CanvasView *, const ToolEvent &) override
    {
        switch (settings().afterPick) {
        case AfterPick::PreviousTool: main->switchToPreviousTool(); break;
        case AfterPick::Pencil: main->setActiveTool(ToolId::Pencil); break;
        default: break;
        }
    }

private:
    void pick(const ToolEvent &e)
    {
        Document *d = doc();
        QPoint c(int(std::floor(e.pos.x())), int(std::floor(e.pos.y())));
        if (!d->rect().contains(c))
            return;
        const QImage &img = settings().sampleImage ? d->composite() : d->activeImage();
        int rad = settings().sampling;          // 0..4 -> 1x1..9x9
        double r = 0, g = 0, b = 0, a = 0;
        int n = 0;
        for (int y = c.y() - rad; y <= c.y() + rad; ++y)
            for (int x = c.x() - rad; x <= c.x() + rad; ++x) {
                if (!img.rect().contains(x, y))
                    continue;
                QRgb p = img.pixel(x, y);      // premultiplied: average then unpremultiply
                r += qRed(p); g += qGreen(p); b += qBlue(p); a += qAlpha(p);
                ++n;
            }
        if (!n)
            return;
        r /= n; g /= n; b /= n; a /= n;
        QColor col;
        if (a > 0)
            col = QColor(std::min(255, int(r * 255 / a + 0.5)), std::min(255, int(g * 255 / a + 0.5)),
                         std::min(255, int(b * 255 / a + 0.5)), int(a + 0.5));
        else
            col = QColor(0, 0, 0, 0);
        if (m_button == Qt::RightButton)
            colors().setSecondary(col);
        else
            colors().setPrimary(col);
    }

    Qt::MouseButton m_button = Qt::LeftButton;
};

// ============================================================ text tool

class TextTool : public Tool
{
public:
    using Tool::Tool;
    TileBehavior tileBehavior() const override { return TileWrap; }
    ToolId id() const override { return ToolId::Text; }
    QString name() const override { return QObject::tr("Text"); }
    QString iconName() const override { return "text"; }
    QChar shortcut() const override { return 'T'; }
    quint32 options() const override { return OptFont | OptFill | OptAntialias | OptAlphaBlend; }
    QString helpText() const override
    {
        return QObject::tr("Text: Left click to place cursor, then type desired text. Text color is primary color. "
                           "Drag the small handle to move the text.");
    }
    QCursor cursor() const override { return Qt::IBeamCursor; }
    bool isEditing() const override { return m_editing; }
    bool wantsKeys() const override { return m_editing; }

    void activate() override
    {
        if (m_hooked)
            return;
        m_hooked = true;
        connect(&settings(), &ToolSettings::changed, this, [this] { if (m_editing) render(); });
        connect(&colors(), &ColorState::changed, this, [this] { if (m_editing) render(); });
    }

    void mousePress(CanvasView *v, const ToolEvent &e) override
    {
        if (m_editing && m_doc == doc()) {
            QRectF box = v->imageToView(textBounds());
            QPointF h = box.bottomRight() + QPointF(4, 4);
            if (QLineF(h, e.viewPos).length() <= kHandleHit + 2) {
                m_moving = true;
                m_moveStart = e.pos;
                m_originStart = m_origin;
                return;
            }
            if (box.adjusted(-2, -2, 2, 2).contains(e.viewPos)) {
                // click inside text: place caret on the nearest line/column
                placeCaret(e.pos);
                v->updateOverlay();
                return;
            }
        }
        main->commitTool(this);
        commit();
        m_doc = doc();
        m_backup = m_doc->activeImage();
        m_origin = QPointF(std::round(e.pos.x()), std::round(e.pos.y()));
        m_lines = QStringList{QString()};
        m_line = 0;
        m_col = 0;
        m_editing = true;
        render();
    }

    void mouseMove(CanvasView *v, const ToolEvent &e) override
    {
        if (m_moving && m_doc == doc()) {
            QPointF d = e.pos - m_moveStart;
            m_origin = m_originStart + QPointF(std::round(d.x()), std::round(d.y()));
            render();
            return;
        }
        if (m_editing) {
            QRectF box = v->imageToView(textBounds());
            QPointF h = box.bottomRight() + QPointF(4, 4);
            v->viewport()->setCursor(QLineF(h, e.viewPos).length() <= kHandleHit + 2 ? QCursor(Qt::SizeAllCursor) : cursor());
        }
    }

    void mouseRelease(CanvasView *, const ToolEvent &) override { m_moving = false; }

    bool keyPress(QKeyEvent *e) override
    {
        if (!m_editing || m_doc != doc())
            return false;
        QString &ln = m_lines[m_line];
        switch (e->key()) {
        case Qt::Key_Escape:
            commit();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter: {
            QString rest = ln.mid(m_col);
            ln.truncate(m_col);
            m_lines.insert(m_line + 1, rest);
            ++m_line;
            m_col = 0;
            break;
        }
        case Qt::Key_Backspace:
            if (m_col > 0) {
                ln.remove(m_col - 1, 1);
                --m_col;
            } else if (m_line > 0) {
                m_col = m_lines[m_line - 1].size();
                m_lines[m_line - 1] += ln;
                m_lines.removeAt(m_line);
                --m_line;
            }
            break;
        case Qt::Key_Delete:
            if (m_col < ln.size())
                ln.remove(m_col, 1);
            else if (m_line < m_lines.size() - 1) {
                ln += m_lines[m_line + 1];
                m_lines.removeAt(m_line + 1);
            }
            break;
        case Qt::Key_Left:
            if (m_col > 0) --m_col;
            else if (m_line > 0) { --m_line; m_col = m_lines[m_line].size(); }
            break;
        case Qt::Key_Right:
            if (m_col < ln.size()) ++m_col;
            else if (m_line < m_lines.size() - 1) { ++m_line; m_col = 0; }
            break;
        case Qt::Key_Up:
            if (m_line > 0) { --m_line; m_col = std::min(m_col, int(m_lines[m_line].size())); }
            break;
        case Qt::Key_Down:
            if (m_line < m_lines.size() - 1) { ++m_line; m_col = std::min(m_col, int(m_lines[m_line].size())); }
            break;
        case Qt::Key_Home: m_col = 0; break;
        case Qt::Key_End: m_col = ln.size(); break;
        default:
            if (e->matches(QKeySequence::Paste)) {
                insertText(QGuiApplication::clipboard()->text());
                break;
            }
            if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier))
                return false;
            if (!e->text().isEmpty() && e->text().at(0).isPrint())
                insertText(e->text());
            else
                return true;
        }
        render();
        return true;
    }

    void commit() override
    {
        if (!m_editing)
            return;
        m_editing = false;
        m_moving = false;
        bool empty = m_lines.join(QString()).isEmpty();
        if (m_doc == doc()) {
            if (empty) {
                m_doc->activeImage() = m_backup;
                m_doc->invalidate();
            } else {
                m_doc->pushHistory(name(), icon());
            }
            view()->updateOverlay();
        }
        m_backup = QImage();
    }

    void reset() override
    {
        m_editing = false;
        m_moving = false;
        m_backup = QImage();
    }

    void paintOverlay(QPainter &p, CanvasView *v) override
    {
        if (!m_editing || m_doc != doc())
            return;
        QRectF box = v->imageToView(textBounds());
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(QColor(0, 0, 0, 140), 1, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(box.adjusted(-2, -2, 2, 2));
        // caret
        QFontMetricsF fm(font());
        qreal lh = fm.lineSpacing();
        qreal x = lineX(m_line) + fm.horizontalAdvance(m_lines[m_line].left(m_col));
        QPointF a = v->imageToView(QPointF(x, m_origin.y() + m_line * lh));
        QPointF b = v->imageToView(QPointF(x, m_origin.y() + (m_line + 1) * lh));
        p.setPen(QPen(Qt::black, 1));
        p.drawLine(a, b);
        p.setPen(QPen(Qt::white, 1));
        p.drawLine(a + QPointF(1, 0), b + QPointF(1, 0));
        p.restore();
        drawHandle(p, box.bottomRight() + QPointF(4, 4));
    }

private:
    QFont font() const
    {
        QFont f(settings().fontFamily);
        f.setPixelSize(std::max(1, int(std::round(settings().fontSize * doc()->dpi / 72.0))));
        f.setBold(settings().bold);
        f.setItalic(settings().italic);
        f.setUnderline(settings().underline);
        f.setStrikeOut(settings().strike);
        f.setStyleStrategy(settings().antialias ? QFont::PreferAntialias : QFont::NoAntialias);
        return f;
    }

    qreal maxWidth() const
    {
        QFontMetricsF fm(font());
        qreal w = 0;
        for (const QString &l : m_lines)
            w = std::max(w, fm.horizontalAdvance(l));
        return w;
    }

    qreal lineX(int i) const
    {
        QFontMetricsF fm(font());
        qreal w = fm.horizontalAdvance(m_lines[i]);
        switch (settings().textAlign) {
        case 1: return m_origin.x() - w / 2;
        case 2: return m_origin.x() - w;
        default: return m_origin.x();
        }
    }

    QRectF textBounds() const
    {
        QFontMetricsF fm(font());
        qreal w = std::max<qreal>(maxWidth(), 2);
        qreal x = settings().textAlign == 1 ? m_origin.x() - w / 2 : settings().textAlign == 2 ? m_origin.x() - w : m_origin.x();
        return QRectF(x, m_origin.y(), w, fm.lineSpacing() * m_lines.size());
    }

    void insertText(const QString &t)
    {
        QStringList parts = t.split('\n');
        for (int i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                QString rest = m_lines[m_line].mid(m_col);
                m_lines[m_line].truncate(m_col);
                m_lines.insert(m_line + 1, rest);
                ++m_line;
                m_col = 0;
            }
            QString s = parts[i];
            s.remove('\r');
            m_lines[m_line].insert(m_col, s);
            m_col += s.size();
        }
    }

    void placeCaret(const QPointF &pos)
    {
        QFontMetricsF fm(font());
        int line = std::clamp(int((pos.y() - m_origin.y()) / fm.lineSpacing()), 0, int(m_lines.size()) - 1);
        m_line = line;
        qreal x0 = lineX(line);
        const QString &l = m_lines[line];
        int best = 0;
        qreal bestD = 1e9;
        for (int i = 0; i <= l.size(); ++i) {
            qreal d = std::fabs(x0 + fm.horizontalAdvance(l.left(i)) - pos.x());
            if (d < bestD) { bestD = d; best = i; }
        }
        m_col = best;
    }

    void render()
    {
        if (!m_editing || m_doc != doc())
            return;
        QImage &layer = m_doc->activeImage();
        QFont f = font();
        QFontMetricsF fm(f);
        QPainterPath text;
        for (int i = 0; i < m_lines.size(); ++i)
            text.addText(QPointF(lineX(i), m_origin.y() + i * fm.lineSpacing() + fm.ascent()), f, m_lines[i]);
        const QRectF editBox = textBounds();
        text = tiledPath(text, m_doc, settings());
        QRect bounds = (text.boundingRect() | editBox).adjusted(-4, -4, 4, 4).toAlignedRect().intersected(m_doc->rect());
        QImage temp = scratch(bounds);
        {
            QPainter p(&temp);
            p.translate(-bounds.topLeft());
            p.setRenderHint(QPainter::Antialiasing, settings().antialias);
            p.fillPath(text, makeFillBrush(settings().fillStyle, opaque(colors().primary), colors().secondary));
        }
        QRect dirty = bounds.united(m_lastRect);
        m_lastRect = bounds;
        compositeTemp(layer, m_backup, temp, bounds.topLeft(), dirty, settings().alphaBlend, colors().primary.alphaF(),
                      CompMode::Normal, m_doc);
        m_doc->invalidate(dirty);
        view()->updateOverlay();
    }

    Document *m_doc = nullptr;
    QImage m_backup;
    QPointF m_origin, m_moveStart, m_originStart;
    QStringList m_lines;
    int m_line = 0, m_col = 0;
    bool m_editing = false, m_moving = false;
    QRect m_lastRect;
};

// ============================================================ line / curve

class LineTool : public Tool
{
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Line; }
    QString name() const override { return QObject::tr("Line / Curve"); }
    QString iconName() const override { return "line"; }
    QChar shortcut() const override { return 'O'; }
    quint32 options() const override { return OptBrushWidth | OptLineStyle | OptAntialias | OptAlphaBlend; }
    QString helpText() const override
    {
        return QObject::tr("Line/Curve: Click and drag to draw a line. Drag the handles or the line itself to bend it. "
                           "Press Enter to finish.");
    }
    bool isEditing() const override { return m_editing; }

    void activate() override
    {
        if (m_hooked)
            return;
        m_hooked = true;
        connect(&settings(), &ToolSettings::changed, this, [this] {
            if (m_editing && m_doc == doc() && main->currentTool() == this) {
                render();
                m_doc->replaceTopHistory();
            }
        });
    }

    void mousePress(CanvasView *v, const ToolEvent &e) override
    {
        if (m_editing && m_doc == doc()) {
            for (int i = 0; i < 4; ++i)
                if (QLineF(v->imageToView(m_p[i]), e.viewPos).length() <= kHandleHit) {
                    m_drag = i;
                    return;
                }
            // bend: drag on the curve moves the nearest control point
            if (distanceToCurve(e.pos) * v->zoom() <= std::max(6.0, settings().brushWidth * v->zoom() / 2 + 3)) {
                m_drag = QLineF(e.pos, m_p[1]).length() < QLineF(e.pos, m_p[2]).length() ? 1 : 2;
                m_p[m_drag] = e.pos;
                render();
                return;
            }
        }
        main->commitTool(this);
        commit();
        m_doc = doc();
        m_backup = m_doc->activeImage();
        m_right = e.button == Qt::RightButton;
        for (auto &p : m_p)
            p = e.pos;
        m_drag = 3;
        m_new = true;
        m_editing = true;
        m_pushed = false;
    }

    void mouseMove(CanvasView *v, const ToolEvent &e) override
    {
        if (!m_editing || m_drag < 0 || m_doc != doc())
            return;
        QPointF p = e.pos;
        if (m_new && (e.mods & Qt::ShiftModifier)) {
            QLineF l(m_p[0], p);
            l.setAngle(std::round(l.angle() / 15.0) * 15.0);
            p = l.p2();
        }
        if (m_new) {
            m_p[3] = p;
            m_p[1] = m_p[0] + (m_p[3] - m_p[0]) / 3.0;
            m_p[2] = m_p[0] + (m_p[3] - m_p[0]) * 2.0 / 3.0;
        } else {
            QPointF d = p - m_p[m_drag];
            m_p[m_drag] = p;
            // moving an endpoint keeps the shape of a straight line
            if (m_drag == 0 && m_straight) m_p[1] = m_p[0] + (m_p[3] - m_p[0]) / 3.0;
            if (m_drag == 3 && m_straight) m_p[2] = m_p[0] + (m_p[3] - m_p[0]) * 2.0 / 3.0;
            if (m_straight && (m_drag == 0 || m_drag == 3)) {
                m_p[1] = m_p[0] + (m_p[3] - m_p[0]) / 3.0;
                m_p[2] = m_p[0] + (m_p[3] - m_p[0]) * 2.0 / 3.0;
            }
            Q_UNUSED(d);
            if (m_drag == 1 || m_drag == 2)
                m_straight = false;
        }
        render();
        v->updateOverlay();
    }

    void mouseRelease(CanvasView *, const ToolEvent &) override
    {
        if (!m_editing || m_drag < 0 || m_doc != doc())
            return;
        if (m_new) {
            m_straight = true;
            if (QLineF(m_p[0], m_p[3]).length() < 0.5) {
                // a click without drag: nothing drawn
                m_editing = false;
                m_drag = -1;
                m_doc->activeImage() = m_backup;
                m_doc->invalidate();
                return;
            }
        }
        m_new = false;
        m_drag = -1;
        if (!m_pushed) {
            m_doc->pushHistory(name(), icon());
            m_pushed = true;
        } else {
            m_doc->replaceTopHistory();
        }
    }

    bool keyPress(QKeyEvent *e) override
    {
        if (m_editing && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
            commit();
            return true;
        }
        return false;
    }

    void commit() override
    {
        if (!m_editing)
            return;
        m_editing = false;
        m_drag = -1;
        m_backup = QImage();
        if (view())
            view()->updateOverlay();
    }

    void reset() override
    {
        m_editing = false;
        m_drag = -1;
        m_backup = QImage();
    }
    void prepareForHistory() override { reset(); }

    void paintOverlay(QPainter &p, CanvasView *v) override
    {
        if (!m_editing || m_doc != doc() || m_new)
            return;
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(0, 0, 0, 90), 1, Qt::DashLine));
        p.drawLine(v->imageToView(m_p[0]), v->imageToView(m_p[1]));
        p.drawLine(v->imageToView(m_p[3]), v->imageToView(m_p[2]));
        p.restore();
        for (int i = 0; i < 4; ++i)
            drawHandle(p, v->imageToView(m_p[i]), i == 1 || i == 2);
    }

private:
    QPainterPath curve() const
    {
        QPainterPath path(m_p[0]);
        path.cubicTo(m_p[1], m_p[2], m_p[3]);
        return path;
    }

    qreal distanceToCurve(const QPointF &pt) const
    {
        QPainterPath c = curve();
        qreal best = 1e9;
        for (int i = 0; i <= 64; ++i) {
            QPointF q = c.pointAtPercent(i / 64.0);
            best = std::min(best, QLineF(q, pt).length());
        }
        return best;
    }

    void render()
    {
        QColor col = m_right ? colors().secondary : colors().primary;
        const QPainterPath path = tiledPath(curve(), m_doc, settings());
        QRect r = path.controlPointRect().adjusted(-settings().brushWidth - 2, -settings().brushWidth - 2,
                                                       settings().brushWidth + 2, settings().brushWidth + 2).toAlignedRect();
        const QRect tr = r.intersected(m_doc->rect());
        QImage temp = scratch(tr);
        {
            QPainter p(&temp);
            p.translate(-tr.topLeft());
            p.setRenderHint(QPainter::Antialiasing, settings().antialias);
            p.setPen(dashPen(opaque(col), settings().brushWidth, settings().lineStyle));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
        QRect dirty = r.united(m_lastRect);
        m_lastRect = r;
        compositeTemp(m_doc->activeImage(), m_backup, temp, tr.topLeft(), dirty, settings().alphaBlend, col.alphaF(), CompMode::Normal, m_doc);
        m_doc->invalidate(dirty);
    }

    Document *m_doc = nullptr;
    QImage m_backup;
    QPointF m_p[4];
    int m_drag = -1;
    bool m_editing = false, m_new = false, m_right = false, m_pushed = false, m_straight = true;
    QRect m_lastRect;
};

// ============================================================ shapes

class ShapeTool : public Tool
{
public:
    enum Kind { Rect, Round, Ellipse, Free };
    ShapeTool(MainWindow *m, Kind k) : Tool(m), m_kind(k) {}

    ToolId id() const override
    {
        switch (m_kind) {
        case Rect: return ToolId::Rectangle;
        case Round: return ToolId::RoundRect;
        case Ellipse: return ToolId::Ellipse;
        default: return ToolId::Freeform;
        }
    }
    QString name() const override
    {
        switch (m_kind) {
        case Rect: return QObject::tr("Rectangle");
        case Round: return QObject::tr("Rounded Rectangle");
        case Ellipse: return QObject::tr("Ellipse");
        default: return QObject::tr("Freeform Shape");
        }
    }
    QString iconName() const override
    {
        switch (m_kind) {
        case Rect: return "rectangle";
        case Round: return "roundrect";
        case Ellipse: return "ellipse";
        default: return "freeform";
        }
    }
    QChar shortcut() const override { return 'O'; }
    quint32 options() const override
    {
        quint32 o = OptBrushWidth | OptShapeType | OptFill | OptAntialias | OptAlphaBlend;
        if (m_kind != Free)
            o |= OptLineStyle;
        if (m_kind == Round)
            o |= OptRadius;
        return o;
    }
    QString helpText() const override
    {
        if (m_kind == Free)
            return QObject::tr("Freeform Shape: Click and drag to draw the outline of a shape. "
                               "Outline uses primary color, interior uses secondary color.");
        return QObject::tr("%1: Click and drag to draw a shape. Hold Shift to constrain proportions. "
                           "Outline uses primary color, interior uses secondary color.").arg(name());
    }

    void mousePress(CanvasView *, const ToolEvent &e) override
    {
        main->commitTool(this);
        if (m_active)
            return;
        m_doc = doc();
        m_backup = m_doc->activeImage();
        m_right = e.button == Qt::RightButton;
        m_start = m_cur = e.pos;
        m_poly.clear();
        m_poly << e.pos;
        m_active = true;
        m_lastRect = QRect();
    }

    void mouseMove(CanvasView *, const ToolEvent &e) override
    {
        if (!m_active || m_doc != doc())
            return;
        m_cur = e.pos;
        if (m_kind != Free && (e.mods & Qt::ShiftModifier)) {
            qreal s = std::max(std::fabs(m_cur.x() - m_start.x()), std::fabs(m_cur.y() - m_start.y()));
            m_cur = QPointF(m_start.x() + (m_cur.x() >= m_start.x() ? s : -s),
                            m_start.y() + (m_cur.y() >= m_start.y() ? s : -s));
        }
        m_poly << e.pos;
        render();
        QRectF r = QRectF(m_start, m_cur).normalized();
        setStatus(QObject::tr("%1: %2 x %3").arg(name()).arg(int(r.width())).arg(int(r.height())));
    }

    void mouseRelease(CanvasView *, const ToolEvent &) override
    {
        if (!m_active || m_doc != doc())
            return;
        m_active = false;
        if (QLineF(m_start, m_cur).length() < 1 && m_poly.size() < 3) {
            m_doc->activeImage() = m_backup;
            m_doc->invalidate();
            m_backup = QImage();
            return;
        }
        m_doc->pushHistory(name(), icon());
        m_backup = QImage();
        setStatus(helpText());
    }

    void reset() override
    {
        m_active = false;
        m_backup = QImage();
    }

private:
    void render()
    {
        QColor outline = m_right ? colors().secondary : colors().primary;
        QColor interior = m_right ? colors().primary : colors().secondary;
        ShapeType st = settings().shapeType;
        if (st == ShapeType::Filled)
            interior = outline;
        const qreal w = settings().brushWidth;
        QRectF r = QRectF(m_start, m_cur).normalized();
        const bool aa = settings().antialias;
        if (!aa || int(w) % 2 == 1) {
            r = QRectF(QPointF(std::floor(r.left()) + 0.5, std::floor(r.top()) + 0.5),
                       QPointF(std::floor(r.right()) + 0.5, std::floor(r.bottom()) + 0.5));
        }
        QPainterPath path;
        switch (m_kind) {
        case Rect: path.addRect(r); break;
        case Round: {
            qreal rad = std::min<qreal>(settings().radius, std::min(r.width(), r.height()) / 2);
            path.addRoundedRect(r, rad, rad);
            break;
        }
        case Ellipse: path.addEllipse(r); break;
        case Free:
            path.addPolygon(m_poly);
            path.closeSubpath();
            break;
        }
        path = tiledPath(path, m_doc, settings());
        QRect bounds = path.boundingRect().adjusted(-w - 2, -w - 2, w + 2, w + 2).toAlignedRect();
        const QRect tr = bounds.intersected(m_doc->rect());
        QImage temp = scratch(tr);
        {
            QPainter p(&temp);
            p.translate(-tr.topLeft());
            p.setRenderHint(QPainter::Antialiasing, aa);
            if (st != ShapeType::Outline) {
                p.setPen(Qt::NoPen);
                p.setOpacity(interior.alphaF());
                p.setBrush(makeFillBrush(settings().fillStyle, opaque(interior), Qt::transparent));
                p.drawPath(path);
                p.setOpacity(1);
            }
            if (st != ShapeType::Filled) {
                QPen pen = dashPen(opaque(outline), w, m_kind == Free ? 0 : settings().lineStyle);
                pen.setJoinStyle(m_kind == Rect ? Qt::MiterJoin : Qt::RoundJoin);
                if (m_kind == Rect && settings().lineStyle == 0)
                    pen.setCapStyle(Qt::SquareCap);
                p.setPen(pen);
                p.setBrush(Qt::NoBrush);
                p.setOpacity(outline.alphaF());
                if (st == ShapeType::FilledOutline) {
                    p.setCompositionMode(QPainter::CompositionMode_Source);   // outline replaces fill
                    QColor oc = opaque(outline);
                    oc.setAlphaF(outline.alphaF());
                    pen.setColor(oc);
                    p.setPen(pen);
                    p.setOpacity(1);
                }
                p.drawPath(path);
            }
        }
        QRect dirty = bounds.united(m_lastRect);
        m_lastRect = bounds;
        compositeTemp(m_doc->activeImage(), m_backup, temp, tr.topLeft(), dirty, settings().alphaBlend, 1.0, CompMode::Normal, m_doc);
        m_doc->invalidate(dirty);
    }

    Kind m_kind;
    Document *m_doc = nullptr;
    QImage m_backup;
    QPointF m_start, m_cur;
    QPolygonF m_poly;
    bool m_right = false, m_active = false;
    QRect m_lastRect;
};

// ============================================================ factory

QVector<Tool *> createTools(MainWindow *m)
{
    QVector<Tool *> t(int(ToolId::Count), nullptr);
    t[int(ToolId::RectSelect)] = new RectSelectTool(m);
    t[int(ToolId::Move)] = new MoveTool(m);
    t[int(ToolId::Lasso)] = new LassoSelectTool(m);
    t[int(ToolId::MoveSelection)] = new MoveSelectionTool(m);
    t[int(ToolId::EllipseSelect)] = new EllipseSelectTool(m);
    t[int(ToolId::Zoom)] = new ZoomTool(m);
    t[int(ToolId::MagicWand)] = new MagicWandTool(m);
    t[int(ToolId::Pan)] = new PanTool(m);
    t[int(ToolId::Bucket)] = new BucketTool(m);
    t[int(ToolId::Gradient)] = new GradientTool(m);
    t[int(ToolId::Brush)] = new BrushTool(m);
    t[int(ToolId::Eraser)] = new EraserTool(m);
    t[int(ToolId::Pencil)] = new PencilTool(m);
    t[int(ToolId::ColorPicker)] = new ColorPickerTool(m);
    t[int(ToolId::CloneStamp)] = new CloneStampTool(m);
    t[int(ToolId::Recolor)] = new RecolorTool(m);
    t[int(ToolId::Text)] = new TextTool(m);
    t[int(ToolId::Line)] = new LineTool(m);
    t[int(ToolId::Rectangle)] = new ShapeTool(m, ShapeTool::Rect);
    t[int(ToolId::RoundRect)] = new ShapeTool(m, ShapeTool::Round);
    t[int(ToolId::Ellipse)] = new ShapeTool(m, ShapeTool::Ellipse);
    t[int(ToolId::Freeform)] = new ShapeTool(m, ShapeTool::Free);
    for (Tool *tool : t)
        tool->setParent(reinterpret_cast<QObject *>(m));
    return t;
}
