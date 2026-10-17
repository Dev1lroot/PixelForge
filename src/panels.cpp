#include "panels.h"
#include "mainwindow.h"
#include "document.h"
#include "tools.h"
#include "icons.h"

#include <QPainter>
#include <QToolButton>
#include <QButtonGroup>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QMouseEvent>
#include <QAction>
#include <QToolBar>
#include <QScrollBar>
#include <QApplication>

// ============================================================ FloatingPanel

FloatingPanel::FloatingPanel(const QString &title, QWidget *workspace) : QFrame(workspace), m_title(title)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setMouseTracking(true);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(Border, Border + TitleHeight, Border, Border);
    m_layout->setSpacing(0);

    m_close = new QToolButton(this);
    m_close->setFixedSize(15, 13);
    m_close->setCursor(Qt::ArrowCursor);
    m_close->setFocusPolicy(Qt::NoFocus);
    m_close->setStyleSheet(
        "QToolButton { border: 1px solid #7a1d12; border-radius: 2px;"
        " background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #f0a090, stop:0.45 #d8553e, stop:1 #b8301c); }"
        "QToolButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #ffb8a8, stop:0.45 #f06a50, stop:1 #d03a20); }"
        "QToolButton:pressed { background: #902010; }");
    m_close->setIcon([] {
        QPixmap pm(26, 22);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(9, 7), QPointF(17, 15));
        p.drawLine(QPointF(17, 7), QPointF(9, 15));
        pm.setDevicePixelRatio(2);
        return QIcon(pm);
    }());
    m_close->setIconSize(QSize(13, 11));
    connect(m_close, &QToolButton::clicked, this, &FloatingPanel::closeRequested);

    m_effect = new QGraphicsOpacityEffect(this);
    m_effect->setOpacity(0.78);
    m_effect->setEnabled(false);
    setGraphicsEffect(m_effect);

    m_hoverCheck.setInterval(250);
    connect(&m_hoverCheck, &QTimer::timeout, this, &FloatingPanel::updateOpacity);
    m_hoverCheck.start();
}

void FloatingPanel::setContent(QWidget *w)
{
    m_content = w;
    m_layout->addWidget(w);
}

void FloatingPanel::setTranslucency(bool enabled)
{
    m_translucent = enabled;
    updateOpacity();
}

void FloatingPanel::updateOpacity()
{
    if (!isVisible())
        return;
    bool inside = rect().contains(mapFromGlobal(QCursor::pos())) || m_drag != None;
    // translucent only while the pointer is elsewhere and the panel overlaps the canvas
    bool want = m_translucent && !inside && QApplication::activePopupWidget() == nullptr;
    if (m_effect->isEnabled() != want)
        m_effect->setEnabled(want);
}

void FloatingPanel::enterEvent(QEnterEvent *e)
{
    updateOpacity();
    QFrame::enterEvent(e);
}

void FloatingPanel::leaveEvent(QEvent *e)
{
    updateOpacity();
    QFrame::leaveEvent(e);
}

void FloatingPanel::resizeEvent(QResizeEvent *e)
{
    m_close->move(width() - Border - m_close->width() - 1, Border + (TitleHeight - m_close->height()) / 2);
    QFrame::resizeEvent(e);
}

void FloatingPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    QRect r = rect();
    // dark outer frame
    QLinearGradient frame(0, 0, 0, height());
    frame.setColorAt(0, QColor(0x58, 0x63, 0x74));
    frame.setColorAt(1, QColor(0x3a, 0x43, 0x50));
    p.fillRect(r, frame);
    p.setPen(QColor(0x1e, 0x24, 0x2c));
    p.drawRect(r.adjusted(0, 0, -1, -1));
    p.setPen(QColor(255, 255, 255, 50));
    p.drawRect(r.adjusted(1, 1, -2, -2));
    // caption
    QRect cap(Border, Border, width() - 2 * Border, TitleHeight);
    QLinearGradient cg(0, cap.top(), 0, cap.bottom());
    cg.setColorAt(0, QColor(0xfb, 0xfc, 0xfe));
    cg.setColorAt(1, QColor(0xe2, 0xe7, 0xef));
    p.fillRect(cap, cg);
    p.setPen(QColor(0x22, 0x22, 0x22));
    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);
    p.drawText(cap.adjusted(4, 0, -18, 0), Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(f).elidedText(m_title, Qt::ElideRight, cap.width() - 21));
    // client background
    QRect client(Border, Border + TitleHeight, width() - 2 * Border, height() - 2 * Border - TitleHeight);
    p.fillRect(client, palette().window());
    p.setPen(QColor(0x9a, 0xa4, 0xb4));
    p.drawLine(client.topLeft(), client.topRight());
    if (m_resizable) {
        p.setPen(QColor(150, 155, 165));
        QPoint br = rect().bottomRight() - QPoint(2, 2);
        for (int i = 0; i < 3; ++i)
            p.drawLine(br - QPoint(3 + i * 3, 0), br - QPoint(0, 3 + i * 3));
    }
}

void FloatingPanel::mousePressEvent(QMouseEvent *e)
{
    raise();
    if (e->button() != Qt::LeftButton)
        return;
    QPoint pos = e->position().toPoint();
    if (m_resizable && pos.x() > width() - 10 && pos.y() > height() - 10)
        m_drag = Resize;
    else if (pos.y() < Border + TitleHeight)
        m_drag = Move;
    else
        return;
    m_dragStart = e->globalPosition().toPoint();
    m_geomStart = geometry();
}

void FloatingPanel::mouseMoveEvent(QMouseEvent *e)
{
    QPoint pos = e->position().toPoint();
    if (m_drag == None) {
        bool corner = m_resizable && pos.x() > width() - 10 && pos.y() > height() - 10;
        setCursor(corner ? Qt::SizeFDiagCursor : Qt::ArrowCursor);
        return;
    }
    QPoint d = e->globalPosition().toPoint() - m_dragStart;
    if (m_drag == Move) {
        QRect g = m_geomStart.translated(d);
        // snap to workspace edges
        QWidget *ws = parentWidget();
        const int snap = 8;
        if (std::abs(g.left()) < snap) g.moveLeft(0);
        if (std::abs(g.top()) < snap) g.moveTop(0);
        if (std::abs(ws->width() - g.right() - 1) < snap) g.moveRight(ws->width() - 1);
        if (std::abs(ws->height() - g.bottom() - 1) < snap) g.moveBottom(ws->height() - 1);
        move(g.topLeft());
        keepInside();
    } else {
        QSize s = m_geomStart.size() + QSize(d.x(), d.y());
        s = s.expandedTo(minimumSizeHint()).expandedTo(QSize(120, 80));
        resize(s);
    }
}

void FloatingPanel::mouseReleaseEvent(QMouseEvent *)
{
    if (m_drag != None)
        rememberAnchor();
    m_drag = None;
}

void FloatingPanel::keepInside()
{
    QWidget *ws = parentWidget();
    if (!ws)
        return;
    QPoint p = pos();
    p.setX(std::clamp(p.x(), 0, std::max(0, ws->width() - width())));
    p.setY(std::clamp(p.y(), 0, std::max(0, ws->height() - height())));
    move(p);
}

void FloatingPanel::rememberAnchor()
{
    QWidget *ws = parentWidget();
    Qt::Alignment a;
    QRect g = geometry();
    int cx = g.center().x(), cy = g.center().y();
    a |= cx < ws->width() / 2 ? Qt::AlignLeft : Qt::AlignRight;
    a |= cy < ws->height() / 2 ? Qt::AlignTop : Qt::AlignBottom;
    m_anchor = a;
    m_anchorOffset = QPoint((a & Qt::AlignLeft) ? g.left() : ws->width() - g.right(),
                            (a & Qt::AlignTop) ? g.top() : ws->height() - g.bottom());
}

void FloatingPanel::placeAnchored()
{
    QWidget *ws = parentWidget();
    QRect g = geometry();
    int x = (m_anchor & Qt::AlignLeft) ? m_anchorOffset.x() : ws->width() - m_anchorOffset.x() - g.width() + 1;
    int y = (m_anchor & Qt::AlignTop) ? m_anchorOffset.y() : ws->height() - m_anchorOffset.y() - g.height() + 1;
    move(x, y);
    keepInside();
}

void FloatingPanel::setHomeGeometry(const QRect &r, Qt::Alignment a)
{
    m_home = r;
    m_homeAnchor = a;
}

void FloatingPanel::resetToHome()
{
    QWidget *ws = parentWidget();
    resize(m_home.size());
    int x = (m_homeAnchor & Qt::AlignLeft) ? m_home.x() : ws->width() - m_home.x() - m_home.width();
    int y = (m_homeAnchor & Qt::AlignTop) ? m_home.y() : ws->height() - m_home.y() - m_home.height();
    move(x, y);
    keepInside();
    rememberAnchor();
}

// ============================================================ ToolsPanel

ToolsPanel::ToolsPanel(MainWindow *main) : QWidget(main)
{
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(3, 3, 3, 3);
    grid->setSpacing(1);
    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    for (int i = 0; i < int(ToolId::Count); ++i) {
        Tool *t = main->tool(ToolId(i));
        auto *b = new QToolButton(this);
        b->setIcon(t->icon());
        b->setIconSize(QSize(16, 16));
        b->setFixedSize(27, 24);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(QString("%1 (%2)").arg(t->name()).arg(t->shortcut()));
        b->setObjectName("toolButton");
        m_group->addButton(b, i);
        grid->addWidget(b, i / 2, i % 2);
    }
    connect(m_group, &QButtonGroup::idClicked, this, [main](int id) { main->setActiveTool(ToolId(id)); });
}

void ToolsPanel::setCurrent(int id)
{
    if (auto *b = m_group->button(id))
        b->setChecked(true);
}

// ============================================================ HistoryPanel

static QToolBar *panelToolbar(QWidget *parent, const QList<QAction *> &actions)
{
    auto *tb = new QToolBar(parent);
    tb->setIconSize(QSize(16, 16));
    tb->setObjectName("panelToolbar");
    for (QAction *a : actions)
        tb->addAction(a);
    return tb;
}

HistoryPanel::HistoryPanel(MainWindow *main, QAction *first, QAction *undo, QAction *redo, QAction *last)
    : QWidget(main), m_main(main)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(2, 2, 2, 0);
    v->setSpacing(0);
    m_list = new HistoryList(this);
    m_list->setIconSize(QSize(16, 16));
    m_list->setObjectName("historyList");
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setUniformItemSizes(true);
    v->addWidget(m_list, 1);
    v->addWidget(panelToolbar(this, {first, undo, redo, last}));
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_updating || !m_doc || row < 0)
            return;
        m_main->prepareHistoryNavigation();
        m_doc->jumpTo(row);
    });
}

void HistoryPanel::setDocument(Document *doc)
{
    if (m_doc)
        disconnect(m_doc, nullptr, this, nullptr);
    m_doc = doc;
    if (doc)
        connect(doc, &Document::historyChanged, this, &HistoryPanel::rebuild);
    rebuild();
}

void HistoryPanel::rebuild()
{
    m_updating = true;
    m_list->clear();
    if (m_doc) {
        const auto &h = m_doc->history();
        int cur = m_doc->historyIndex();
        for (int i = 0; i < h.size(); ++i) {
            auto *it = new QListWidgetItem(h[i].icon, h[i].name);
            if (i > cur) {
                it->setForeground(QColor(150, 150, 150));
                it->setIcon(QIcon(h[i].icon.pixmap(16, QIcon::Disabled)));
            }
            m_list->addItem(it);
        }
        m_list->setCurrentRow(cur);
        m_list->scrollToItem(m_list->item(cur));
    }
    m_updating = false;
}

// ============================================================ LayersPanel

static const int kThumb = 40;

QRect LayerDelegate::checkRect(const QRect &r)
{
    return QRect(r.right() - 20, r.center().y() - 7, 14, 14);
}

QSize LayerDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return QSize(150, kThumb + 10);
}

void LayerDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const
{
    p->save();
    QRect r = opt.rect.adjusted(1, 1, -1, -1);
    bool sel = opt.state & QStyle::State_Selected;
    if (sel) {
        QLinearGradient g(0, r.top(), 0, r.bottom());
        g.setColorAt(0, QColor(0xdc, 0xeb, 0xfc));
        g.setColorAt(1, QColor(0xc1, 0xdb, 0xfc));
        p->setPen(QColor(0x7d, 0xa2, 0xce));
        p->setBrush(g);
        p->drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
    }
    // thumbnail with checkerboard
    QImage thumb = idx.data(Qt::DecorationRole).value<QImage>();
    QSize ts = thumb.size().scaled(QSize(kThumb * 4 / 3, kThumb), Qt::KeepAspectRatio);
    QRect tr(r.left() + 5 + (kThumb * 4 / 3 - ts.width()) / 2, r.center().y() - ts.height() / 2, ts.width(), ts.height());
    p->fillRect(tr, QBrush(Icons::checkerboard(4)));
    p->drawImage(tr, thumb);
    p->setPen(QColor(0x80, 0x80, 0x80));
    p->setBrush(Qt::NoBrush);
    p->drawRect(tr.adjusted(-1, -1, 0, 0));
    // name
    QFont f = opt.font;
    f.setItalic(sel);
    p->setFont(f);
    p->setPen(QColor(0x10, 0x10, 0x10));
    QRect textRect(r.left() + kThumb * 4 / 3 + 12, r.top(), r.width() - kThumb * 4 / 3 - 40, r.height());
    p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap, idx.data(Qt::DisplayRole).toString());
    // visibility checkbox
    QRect cr = checkRect(opt.rect);
    p->setRenderHint(QPainter::Antialiasing, false);
    p->setPen(QColor(0x70, 0x70, 0x70));
    QLinearGradient cg(0, cr.top(), 0, cr.bottom());
    cg.setColorAt(0, QColor(0xf0, 0xf0, 0xf0));
    cg.setColorAt(1, Qt::white);
    p->setBrush(cg);
    p->drawRect(cr.adjusted(0, 0, -1, -1));
    if (idx.data(Qt::CheckStateRole).toInt() == Qt::Checked) {
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setPen(QPen(QColor(0x20, 0x30, 0x60), 2));
        p->drawPolyline(QPolygonF() << QPointF(cr.left() + 3, cr.top() + 7) << QPointF(cr.left() + 6, cr.top() + 10)
                                    << QPointF(cr.left() + 11, cr.top() + 3));
    }
    p->restore();
}

LayerList::LayerList(QWidget *parent) : QListWidget(parent)
{
    setItemDelegate(new LayerDelegate(this));
    setFocusPolicy(Qt::NoFocus);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setObjectName("layerList");
}

void LayerList::mousePressEvent(QMouseEvent *e)
{
    QModelIndex idx = indexAt(e->position().toPoint());
    if (idx.isValid() && LayerDelegate::checkRect(visualRect(idx)).adjusted(-3, -3, 3, 3).contains(e->position().toPoint())) {
        emit visibilityClicked(idx.row());
        return;
    }
    QListWidget::mousePressEvent(e);
}

void LayerList::mouseDoubleClickEvent(QMouseEvent *e)
{
    QModelIndex idx = indexAt(e->position().toPoint());
    if (idx.isValid() && !LayerDelegate::checkRect(visualRect(idx)).adjusted(-3, -3, 3, 3).contains(e->position().toPoint()))
        emit propertiesRequested(idx.row());
}

LayersPanel::LayersPanel(MainWindow *main, const QList<QAction *> &actions) : QWidget(main), m_main(main)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(2, 2, 2, 0);
    v->setSpacing(0);
    m_list = new LayerList(this);
    v->addWidget(m_list, 1);
    v->addWidget(panelToolbar(this, actions));

    m_thumbTimer.setSingleShot(true);
    m_thumbTimer.setInterval(250);
    connect(&m_thumbTimer, &QTimer::timeout, this, &LayersPanel::refreshThumbs);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_updating || !m_doc || row < 0)
            return;
        m_main->commitTool();
        m_doc->setActiveIndex(m_doc->layerCount() - 1 - row);
    });
    connect(m_list, &LayerList::visibilityClicked, this, [this](int row) {
        if (!m_doc)
            return;
        m_main->commitTool();
        int i = m_doc->layerCount() - 1 - row;
        Layer &l = m_doc->layer(i);
        l.visible = !l.visible;
        m_doc->notifyStructureChanged();
        m_doc->pushHistory(l.visible ? tr("Layer Shown") : tr("Layer Hidden"), Icons::get("layerprops"));
    });
    connect(m_list, &LayerList::propertiesRequested, this, [this](int row) {
        if (!m_doc)
            return;
        m_doc->setActiveIndex(m_doc->layerCount() - 1 - row);
        m_main->showLayerProperties();
    });
}

void LayersPanel::setDocument(Document *doc)
{
    if (m_doc)
        disconnect(m_doc, nullptr, this, nullptr);
    m_doc = doc;
    if (doc) {
        connect(doc, &Document::layersChanged, this, &LayersPanel::rebuild);
        connect(doc, &Document::imageChanged, this, [this] { m_thumbTimer.start(); });
    }
    rebuild();
}

void LayersPanel::rebuild()
{
    m_updating = true;
    m_list->clear();
    if (m_doc) {
        int n = m_doc->layerCount();
        for (int row = 0; row < n; ++row) {
            const Layer &l = m_doc->layer(n - 1 - row);
            auto *it = new QListWidgetItem(l.name);
            it->setData(Qt::DecorationRole,
                        l.image.scaled(kThumb * 4 / 3, kThumb, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            it->setData(Qt::CheckStateRole, l.visible ? Qt::Checked : Qt::Unchecked);
            m_list->addItem(it);
        }
        m_list->setCurrentRow(n - 1 - m_doc->activeIndex());
    }
    m_updating = false;
}

void LayersPanel::refreshThumbs()
{
    if (!m_doc)
        return;
    int n = m_doc->layerCount();
    if (m_list->count() != n) {
        rebuild();
        return;
    }
    for (int row = 0; row < n; ++row) {
        const Layer &l = m_doc->layer(n - 1 - row);
        m_list->item(row)->setData(Qt::DecorationRole,
                                   l.image.scaled(kThumb * 4 / 3, kThumb, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}
