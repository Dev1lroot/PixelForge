#include "canvas.h"
#include "document.h"
#include "mainwindow.h"
#include "tools.h"
#include "icons.h"
#include "settings.h"

#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <cmath>

static const QColor kWorkspace(0xC7, 0xCE, 0xD9);

// ---------------------------------------------------------------- Ruler

Ruler::Ruler(Qt::Orientation o, CanvasView *view) : QWidget(view), m_orient(o), m_view(view)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void Ruler::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const bool horiz = m_orient == Qt::Horizontal;
    QLinearGradient g(0, 0, horiz ? 0 : width(), horiz ? height() : 0);
    g.setColorAt(0, QColor(250, 251, 253));
    g.setColorAt(1, QColor(222, 228, 238));
    p.fillRect(rect(), g);
    p.setPen(QColor(150, 160, 175));
    if (horiz)
        p.drawLine(0, height() - 1, width(), height() - 1);
    else
        p.drawLine(width() - 1, 0, width() - 1, height());

    Document *doc = m_view->document();
    const ToolSettings &st = m_view->main()->settings();
    double unitPx = 1.0;
    if (st.units == Units::Inches)
        unitPx = doc->dpi;
    else if (st.units == Units::Centimeters)
        unitPx = doc->dpi / 2.54;

    const double zoom = m_view->zoom();
    const double screenPerUnit = unitPx * zoom;
    // pick a major step so that labels are at least ~60px apart
    static const double steps[] = {0.01, 0.02, 0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 25, 50, 100, 250, 500,
                                   1000, 2500, 5000, 10000, 25000};
    double step = 1;
    for (double s : steps) {
        step = s;
        if (s * screenPerUnit >= 60)
            break;
    }
    const int subdiv = 10;
    QPointF origin = m_view->imageOrigin();
    const int offset = Thickness;            // rulers sit outside the viewport
    const double o = horiz ? origin.x() + offset : origin.y() + offset;
    const int len = horiz ? width() : height();

    double firstUnit = std::floor((0 - o) / screenPerUnit / step) * step;
    QFont f = font();
    f.setPixelSize(9);
    p.setFont(f);
    p.setPen(QColor(70, 80, 95));
    for (double u = firstUnit; ; u += step) {
        double pos = o + u * screenPerUnit;
        if (pos > len)
            break;
        for (int i = 0; i < subdiv; ++i) {
            double sp = pos + i * step * screenPerUnit / subdiv;
            if (sp < 0 || sp > len)
                continue;
            int tick = i == 0 ? Thickness : (i == subdiv / 2 ? Thickness / 2 : Thickness / 4);
            int ip = int(std::floor(sp)) ;
            if (horiz)
                p.drawLine(ip, Thickness - tick, ip, Thickness - 1);
            else
                p.drawLine(Thickness - tick, ip, Thickness - 1, ip);
        }
        QString label = QString::number(u, 'g', 6);
        if (horiz) {
            p.drawText(QRectF(pos + 2, 0, 80, 10), Qt::AlignLeft | Qt::AlignTop, label);
        } else {
            p.save();
            p.translate(1, pos + 2);
            p.rotate(90);
            p.drawText(QRectF(0, -10, 80, 10), Qt::AlignLeft | Qt::AlignBottom, label);
            p.restore();
        }
    }

    // selection extent highlight + mouse marker
    if (doc->hasSelection()) {
        QRectF sb = m_view->imageToView(QRectF(doc->selection().boundingRect()));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 130, 230, 60));
        if (horiz)
            p.drawRect(QRectF(sb.left() + offset, 0, sb.width(), Thickness));
        else
            p.drawRect(QRectF(0, sb.top() + offset, Thickness, sb.height()));
    }
    if (m_view->mouseInside()) {
        QPointF mp = m_view->imageToView(m_view->mouseImagePos());
        p.setPen(QColor(220, 30, 30));
        if (horiz)
            p.drawLine(QPointF(mp.x() + offset, 0), QPointF(mp.x() + offset, Thickness));
        else
            p.drawLine(QPointF(0, mp.y() + offset), QPointF(Thickness, mp.y() + offset));
    }
}

// ---------------------------------------------------------------- CanvasView

CanvasView::CanvasView(Document *doc, MainWindow *main, QWidget *parent)
    : QAbstractScrollArea(parent), m_doc(doc), m_main(main)
{
    setFrameShape(QFrame::NoFrame);
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    m_checker = Icons::checkerboard(8);
    m_hruler = new Ruler(Qt::Horizontal, this);
    m_vruler = new Ruler(Qt::Vertical, this);
    m_hruler->hide();
    m_vruler->hide();

    connect(doc, &Document::imageChanged, this, [this](const QRect &r) {
        m_mipDirty |= r;
        updateImageRect(r);
    });
    connect(doc, &Document::sizeChanged, this, [this] {
        m_mip = QImage();
        updateScrollBars();
        viewport()->update();
    });
    connect(doc, &Document::selectionChanged, this, [this] {
        viewport()->update();
        if (m_rulers) {
            m_hruler->update();
            m_vruler->update();
        }
    });
    connect(&m_antsTimer, &QTimer::timeout, this, [this] {
        if (!m_doc->hasSelection())
            return;
        m_antsOffset = (m_antsOffset + 1) % 8;
        viewport()->update();
    });
    m_antsTimer.start(120);
    updateScrollBars();
}

QVector<double> CanvasView::zoomPresets()
{
    return {0.01, 0.02, 0.03, 0.04, 0.05, 0.08, 0.12, 0.16, 0.25, 0.33, 0.50, 0.66, 1.0, 1.25, 1.5, 1.75,
            2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 12.0, 16.0, 24.0, 36.0};
}

QPointF CanvasView::tileMargin() const
{
    return m_tiled ? QPointF(m_doc->width() * m_zoom, m_doc->height() * m_zoom) : QPointF(0, 0);
}

QPointF CanvasView::imageOrigin() const
{
    const int n = m_tiled ? 3 : 1;
    const double w = m_doc->width() * m_zoom * n, h = m_doc->height() * m_zoom * n;
    const QSize vp = viewport()->size();
    double x = w < vp.width() ? std::floor((vp.width() - w) / 2) : -horizontalScrollBar()->value();
    double y = h < vp.height() ? std::floor((vp.height() - h) / 2) : -verticalScrollBar()->value();
    return QPointF(x, y) + tileMargin();
}

QPointF CanvasView::viewToImage(const QPointF &v) const
{
    QPointF o = imageOrigin();
    return QPointF((v.x() - o.x()) / m_zoom, (v.y() - o.y()) / m_zoom);
}

QPointF CanvasView::imageToView(const QPointF &i) const
{
    QPointF o = imageOrigin();
    return QPointF(o.x() + i.x() * m_zoom, o.y() + i.y() * m_zoom);
}

QRectF CanvasView::imageToView(const QRectF &r) const
{
    return QRectF(imageToView(r.topLeft()), imageToView(r.bottomRight()));
}

QRectF CanvasView::visibleImageRect() const
{
    return QRectF(viewToImage(QPointF(0, 0)), viewToImage(QPointF(viewport()->width(), viewport()->height())))
        .intersected(QRectF(m_doc->rect()));
}

void CanvasView::updateScrollBars()
{
    const int n = m_tiled ? 3 : 1;
    const int w = int(std::ceil(m_doc->width() * m_zoom * n));
    const int h = int(std::ceil(m_doc->height() * m_zoom * n));
    const QSize vp = viewport()->size();
    horizontalScrollBar()->setRange(0, std::max(0, w - vp.width()));
    verticalScrollBar()->setRange(0, std::max(0, h - vp.height()));
    horizontalScrollBar()->setPageStep(vp.width());
    verticalScrollBar()->setPageStep(vp.height());
    horizontalScrollBar()->setSingleStep(std::max(1, vp.width() / 20));
    verticalScrollBar()->setSingleStep(std::max(1, vp.height() / 20));
}

void CanvasView::setZoom(double z, const QPointF &anchorIn)
{
    z = std::clamp(z, 0.01, 36.0);
    QPointF anchor = anchorIn;
    if (anchor.x() < 0)
        anchor = QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0);
    QPointF img = viewToImage(anchor);
    m_zoom = z;
    updateScrollBars();
    horizontalScrollBar()->setValue(int(std::round(img.x() * z + tileMargin().x() - anchor.x())));
    verticalScrollBar()->setValue(int(std::round(img.y() * z + tileMargin().y() - anchor.y())));
    viewport()->update();
    m_hruler->update();
    m_vruler->update();
    emit zoomChanged(m_zoom);
}

void CanvasView::zoomIn(const QPointF &anchor)
{
    for (double p : zoomPresets())
        if (p > m_zoom + 1e-6) {
            setZoom(p, anchor);
            return;
        }
}

void CanvasView::zoomOut(const QPointF &anchor)
{
    auto pr = zoomPresets();
    for (int i = pr.size() - 1; i >= 0; --i)
        if (pr[i] < m_zoom - 1e-6) {
            setZoom(pr[i], anchor);
            return;
        }
}

void CanvasView::zoomToWindow()
{
    QSize vp = viewport()->size();
    double z = std::min((vp.width() - 10.0) / m_doc->width(), (vp.height() - 10.0) / m_doc->height());
    setZoom(std::max(0.01, z));
    horizontalScrollBar()->setValue(horizontalScrollBar()->maximum() / 2);
    verticalScrollBar()->setValue(verticalScrollBar()->maximum() / 2);
}

void CanvasView::actualSize()
{
    setZoom(1.0);
}

void CanvasView::zoomToRect(const QRectF &r)
{
    if (r.width() < 1 || r.height() < 1)
        return;
    QSize vp = viewport()->size();
    double z = std::min(vp.width() / r.width(), vp.height() / r.height());
    z = std::clamp(z, 0.01, 36.0);
    m_zoom = z;
    updateScrollBars();
    QPointF c = r.center();
    horizontalScrollBar()->setValue(int(c.x() * z + tileMargin().x() - vp.width() / 2.0));
    verticalScrollBar()->setValue(int(c.y() * z + tileMargin().y() - vp.height() / 2.0));
    viewport()->update();
    m_hruler->update();
    m_vruler->update();
    emit zoomChanged(m_zoom);
}

void CanvasView::scrollBy(const QPointF &d)
{
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + int(d.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() + int(d.y()));
}

void CanvasView::setTiled(bool on)
{
    if (on == m_tiled)
        return;
    QPointF centre(viewport()->width() / 2.0, viewport()->height() / 2.0);
    QPointF img = viewToImage(centre);
    m_tiled = on;
    updateScrollBars();
    horizontalScrollBar()->setValue(int(std::round(img.x() * m_zoom + tileMargin().x() - centre.x())));
    verticalScrollBar()->setValue(int(std::round(img.y() * m_zoom + tileMargin().y() - centre.y())));
    viewport()->update();
    m_hruler->update();
    m_vruler->update();
}

void CanvasView::setShowGrid(bool on)
{
    m_grid = on;
    viewport()->update();
}

void CanvasView::setShowRulers(bool on)
{
    m_rulers = on;
    setViewportMargins(on ? Ruler::Thickness : 0, on ? Ruler::Thickness : 0, 0, 0);
    m_hruler->setVisible(on);
    m_vruler->setVisible(on);
    layoutRulers();
}

void CanvasView::layoutRulers()
{
    QRect cr = contentsRect();
    m_hruler->setGeometry(cr.left(), cr.top(), cr.width(), Ruler::Thickness);
    m_vruler->setGeometry(cr.left(), cr.top(), Ruler::Thickness, cr.height());
    m_hruler->raise();
    m_vruler->raise();
}

void CanvasView::updateImageRect(const QRectF &r)
{
    QRectF v = imageToView(r).adjusted(-2, -2, 2, 2);
    if (!m_tiled) {
        viewport()->update(v.toAlignedRect());
        return;
    }
    // the same pixels are shown on every copy in tile view
    const double tw = m_doc->width() * m_zoom, th = m_doc->height() * m_zoom;
    QRegion region;
    for (int ty = -1; ty <= 1; ++ty)
        for (int tx = -1; tx <= 1; ++tx)
            region += v.translated(tx * tw, ty * th).toAlignedRect();
    viewport()->update(region);
}

void CanvasView::resizeEvent(QResizeEvent *e)
{
    QAbstractScrollArea::resizeEvent(e);
    updateScrollBars();
    layoutRulers();
}

void CanvasView::scrollContentsBy(int, int)
{
    viewport()->update();
    if (m_rulers) {
        m_hruler->update();
        m_vruler->update();
    }
}

void CanvasView::drawTile(QPainter &p, const QRect &exposed, const QPointF &o)
{
    const QRectF ir(o, QSizeF(m_doc->width() * m_zoom, m_doc->height() * m_zoom));
    const QRect target = ir.toAlignedRect().intersected(exposed);
    if (target.isEmpty())
        return;
    p.setBrushOrigin(o);
    p.fillRect(target, QBrush(m_checker));
    p.setBrushOrigin(0, 0);
    auto toImg = [&](const QPointF &v) { return QPointF((v.x() - o.x()) / m_zoom, (v.y() - o.y()) / m_zoom); };
    auto toView = [&](const QPointF &i) { return QPointF(o.x() + i.x() * m_zoom, o.y() + i.y() * m_zoom); };
    const QPointF a = toImg(target.topLeft());
    const QPointF b = toImg(QPointF(target.right() + 1, target.bottom() + 1));
    p.save();
    p.setClipRect(target);
    if (m_zoom < 0.5) {
        updateMip();
        const double s = 1.0 / (1 << m_mipLevel);
        QRectF src = QRectF(a * s, b * s).intersected(QRectF(m_mip.rect()));
        QRectF dst(toView(src.topLeft() / s), toView(src.bottomRight() / s));
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(dst, m_mip, src);
    } else {
        // integer source rect covering the exposed area
        QRect src(QPoint(int(std::floor(a.x())), int(std::floor(a.y()))),
                  QPoint(int(std::ceil(b.x())) - 1, int(std::ceil(b.y())) - 1));
        src = src.intersected(m_doc->rect());
        if (!src.isEmpty()) {
            QRectF dst(toView(QPointF(src.topLeft())), QSizeF(src.width() * m_zoom, src.height() * m_zoom));
            if (m_zoom < 1.0)
                p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(dst, m_doc->composite(), src);
        }
    }
    if (m_grid && m_zoom >= 2.0) {
        p.setPen(QColor(0, 0, 0, m_zoom >= 6 ? 90 : 50));
        QRect vis = QRectF(a, b).toAlignedRect().intersected(m_doc->rect());
        for (int x = vis.left(); x <= vis.right() + 1; ++x) {
            double vx = std::floor(o.x() + x * m_zoom);
            p.drawLine(QPointF(vx, ir.top()), QPointF(vx, ir.bottom()));
        }
        for (int y = vis.top(); y <= vis.bottom() + 1; ++y) {
            double vy = std::floor(o.y() + y * m_zoom);
            p.drawLine(QPointF(ir.left(), vy), QPointF(ir.right(), vy));
        }
    }
    p.restore();
}

void CanvasView::paintEvent(QPaintEvent *e)
{
    QPainter p(viewport());
    const QRect exposed = e->rect();
    p.fillRect(exposed, kWorkspace);

    const QPointF o = imageOrigin();
    const double tw = m_doc->width() * m_zoom, th = m_doc->height() * m_zoom;
    const int n = m_tiled ? 1 : 0;
    const QRectF all(o.x() - n * tw, o.y() - n * th, tw * (2 * n + 1), th * (2 * n + 1));

    // soft drop shadow around the canvas (or the whole tile block)
    p.setPen(Qt::NoPen);
    for (int i = 1; i <= 4; ++i) {
        p.setBrush(QColor(0, 0, 0, 40 - i * 8));
        p.drawRect(all.adjusted(-i + 2, -i + 2, i + 1, i + 1).translated(1, 1));
    }
    for (int ty = -n; ty <= n; ++ty)
        for (int tx = -n; tx <= n; ++tx)
            drawTile(p, exposed, o + QPointF(tx * tw, ty * th));
    if (m_tiled) {
        // mark the editable (centre) tile
        p.save();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 160), 1));
        p.drawRect(QRectF(o, QSizeF(tw, th)).adjusted(-0.5, -0.5, 0.5, 0.5));
        QPen dash(QColor(0, 0, 0, 160), 1, Qt::DashLine);
        p.setPen(dash);
        p.drawRect(QRectF(o, QSizeF(tw, th)).adjusted(-0.5, -0.5, 0.5, 0.5));
        p.restore();
    }

    m_main->paintCanvasOverlays(p, QTransform::fromTranslate(o.x(), o.y()).scale(m_zoom, m_zoom));
    drawSelection(p);
    if (Tool *t = m_main->currentTool())
        t->paintOverlay(p, this);
}

void CanvasView::updateMip()
{
    int level = std::max(1, int(std::floor(std::log2(1.0 / m_zoom))));
    const double s = 1.0 / (1 << level);
    QSize ms(std::max(1, int(std::ceil(m_doc->width() * s))), std::max(1, int(std::ceil(m_doc->height() * s))));
    const QImage &comp = m_doc->composite();
    QRect dirty;
    if (m_mip.size() != ms || level != m_mipLevel) {
        m_mip = QImage(ms, QImage::Format_ARGB32_Premultiplied);
        m_mipLevel = level;
        dirty = m_doc->rect();
    } else {
        dirty = m_mipDirty.intersected(m_doc->rect());
    }
    m_mipDirty = QRect();
    if (dirty.isEmpty())
        return;
    // align to the mip grid so each mip pixel is rebuilt from complete source blocks
    const int k = 1 << level;
    QRect mr(QPoint(dirty.left() / k, dirty.top() / k), QPoint(dirty.right() / k, dirty.bottom() / k));
    mr = mr.intersected(m_mip.rect());
    QRect src(mr.left() * k, mr.top() * k, mr.width() * k, mr.height() * k);
    src = src.intersected(m_doc->rect());
    QImage part = comp.copy(src).scaled(mr.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPainter p(&m_mip);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(mr.topLeft(), part);
}

void CanvasView::drawSelection(QPainter &p)
{
    if (!m_doc->hasSelection())
        return;
    QTransform tr;
    QPointF o = imageOrigin();
    tr.translate(o.x(), o.y());
    tr.scale(m_zoom, m_zoom);
    QPainterPath vp = tr.map(m_doc->selection());
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::white, 1));
    p.drawPath(vp);
    QPen dash(Qt::black, 1, Qt::CustomDashLine);
    dash.setDashPattern({4, 4});
    dash.setDashOffset(m_antsOffset);
    p.setPen(dash);
    p.drawPath(vp);
    p.restore();
}

static ToolEvent makeEvent(CanvasView *v, QMouseEvent *e)
{
    ToolEvent te;
    te.viewPos = e->position();
    te.pos = v->viewToImage(e->position());
    Tool *t = v->main()->currentTool();
    if (v->tiled() && t && t->tileBehavior() == Tool::TileWrap) {
        const double w = v->document()->width(), h = v->document()->height();
        te.pos = QPointF(te.pos.x() - std::floor(te.pos.x() / w) * w, te.pos.y() - std::floor(te.pos.y() / h) * h);
    }
    te.button = e->button();
    te.buttons = e->buttons();
    te.mods = e->modifiers();
    return te;
}

void CanvasView::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    if (e->button() == Qt::MiddleButton || (m_spacePan && e->button() == Qt::LeftButton)) {
        m_panning = true;
        m_panLast = e->position().toPoint();
        viewport()->setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (m_toolDrag)
        return;                       // second button during a drag: ignore
    if (Tool *t = m_main->currentTool()) {
        m_toolDrag = true;
        t->mousePress(this, makeEvent(this, e));
    }
}

void CanvasView::mouseMoveEvent(QMouseEvent *e)
{
    m_mouseImage = viewToImage(e->position());
    emit mouseMoved(m_mouseImage);
    if (m_rulers) {
        m_hruler->update();
        m_vruler->update();
    }
    if (m_panning) {
        QPoint d = e->position().toPoint() - m_panLast;
        m_panLast = e->position().toPoint();
        scrollBy(-QPointF(d));
        return;
    }
    if (Tool *t = m_main->currentTool())
        t->mouseMove(this, makeEvent(this, e));
}

void CanvasView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_panning && (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton)) {
        m_panning = false;
        refreshCursor();
        return;
    }
    if (!m_toolDrag)
        return;
    if (e->buttons() & (Qt::LeftButton | Qt::RightButton))
        return;                       // wait until all drawing buttons are up
    m_toolDrag = false;
    if (Tool *t = m_main->currentTool())
        t->mouseRelease(this, makeEvent(this, e));
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (Tool *t = m_main->currentTool())
        t->mouseDoubleClick(this, makeEvent(this, e));
}

void CanvasView::wheelEvent(QWheelEvent *e)
{
    QPoint d = e->angleDelta();
    if (e->modifiers() & Qt::ControlModifier) {
        if (d.y() > 0)
            zoomIn(e->position());
        else if (d.y() < 0)
            zoomOut(e->position());
        e->accept();
        return;
    }
    if (e->modifiers() & Qt::ShiftModifier)
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.y());
    else {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
    }
    e->accept();
}

void CanvasView::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat() && !(m_main->currentTool() && m_main->currentTool()->wantsKeys())) {
        m_spacePan = true;
        viewport()->setCursor(Qt::OpenHandCursor);
        return;
    }
    if (m_main->handleCanvasKey(e))
        return;
    QAbstractScrollArea::keyPressEvent(e);
}

void CanvasView::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat() && m_spacePan) {
        m_spacePan = false;
        m_panning = false;
        refreshCursor();
        return;
    }
    QAbstractScrollArea::keyReleaseEvent(e);
}

void CanvasView::enterEvent(QEnterEvent *e)
{
    m_mouseInside = true;
    QAbstractScrollArea::enterEvent(e);
}

void CanvasView::leaveEvent(QEvent *e)
{
    m_mouseInside = false;
    viewport()->update();
    if (m_rulers) {
        m_hruler->update();
        m_vruler->update();
    }
    QAbstractScrollArea::leaveEvent(e);
}

bool CanvasView::viewportEvent(QEvent *e)
{
    if (e->type() == QEvent::Leave) {
        m_mouseInside = false;
        viewport()->update();
    } else if (e->type() == QEvent::Enter) {
        m_mouseInside = true;
    }
    return QAbstractScrollArea::viewportEvent(e);
}

void CanvasView::refreshCursor()
{
    if (m_spacePan) {
        viewport()->setCursor(Qt::OpenHandCursor);
        return;
    }
    if (Tool *t = m_main->currentTool())
        viewport()->setCursor(t->cursor());
    else
        viewport()->unsetCursor();
}
