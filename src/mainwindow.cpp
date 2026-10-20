#include "mainwindow.h"
#include "document.h"
#include "canvas.h"
#include "panels.h"
#include "colorpanel.h"
#include "effects.h"
#include "dialogs.h"
#include "icons.h"
#include "ora.h"
#include "animpanel.h"
#include "pluginhost.h"

#include <QApplication>
#include <QStackedWidget>
#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QComboBox>
#include <QFontComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QImageReader>
#include <QImageWriter>
#include <QClipboard>
#include <QMimeData>
#include <QPainter>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QSettings>
#include <QFileInfo>
#include <QStandardPaths>
#include <QPrinter>
#include <QPrintDialog>
#include <QActionGroup>
#include <QLineEdit>
#include <QAbstractSpinBox>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTimer>
#include <QPushButton>
#include <QListWidget>
#include <QRegularExpression>
#include <QUrl>
#include <cmath>

static const char *kAppName = "PixelForge";

// ============================================================ small widgets

void Workspace::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0xC7, 0xCE, 0xD9));
}

ToleranceSlider::ToleranceSlider(QWidget *parent) : QWidget(parent)
{
    setFixedSize(130, 18);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Tolerance: how similar colors must be to be included"));
}

void ToleranceSlider::setValue(int v)
{
    v = std::clamp(v, 0, 100);
    if (v == m_value)
        return;
    m_value = v;
    update();
}

void ToleranceSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    QRect r = rect().adjusted(0, 1, -1, -2);
    p.fillRect(r, Qt::white);
    int w = int(r.width() * m_value / 100.0);
    QLinearGradient g(0, r.top(), 0, r.bottom());
    g.setColorAt(0, QColor(0x7e, 0xb4, 0xea));
    g.setColorAt(1, QColor(0x3f, 0x82, 0xd2));
    p.fillRect(QRect(r.left(), r.top(), w, r.height()), g);
    p.setPen(QColor(0x7f, 0x9d, 0xb9));
    p.drawRect(r);
    p.setPen(Qt::black);
    QFont f = font();
    f.setPixelSize(11);
    p.setFont(f);
    p.drawText(r, Qt::AlignCenter, QString("%1%").arg(m_value));
}

void ToleranceSlider::setFromX(int x)
{
    int v = std::clamp(int(std::round(x * 100.0 / (width() - 1))), 0, 100);
    if (v != m_value) {
        m_value = v;
        update();
        emit valueChanged(v);
    }
}

void ToleranceSlider::mousePressEvent(QMouseEvent *e) { setFromX(e->position().toPoint().x()); }
void ToleranceSlider::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons())
        setFromX(e->position().toPoint().x());
}

// ---------------------------------------------------------------- ImageTabs

static const int kTabW = 74;

ImageTabs::ImageTabs(MainWindow *main) : QWidget(main), m_main(main)
{
    setMouseTracking(true);
    setMinimumWidth(kTabW + 18);
}

QSize ImageTabs::sizeHint() const
{
    return QSize(std::max(1, int(m_main->documents().size())) * kTabW + 20, 70);
}

QRect ImageTabs::boxRect(int i) const
{
    return QRect(2 + i * kTabW, 3, kTabW - 4, height() - 6);
}

QRect ImageTabs::closeRect(int i) const
{
    QRect b = boxRect(i);
    return QRect(b.right() - 14, b.top() + 2, 12, 12);
}

QRect ImageTabs::arrowRect() const
{
    return QRect(width() - 16, 2, 14, 16);
}

void ImageTabs::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const auto &docs = m_main->documents();
    for (int i = 0; i < docs.size(); ++i) {
        QRect b = boxRect(i);
        if (b.right() > width() - 18)
            break;
        bool cur = i == m_main->currentIndex();
        if (cur || i == m_hover) {
            QLinearGradient g(0, b.top(), 0, b.bottom());
            g.setColorAt(0, cur ? QColor(0xe0, 0xee, 0xfd) : QColor(0xf2, 0xf7, 0xfe));
            g.setColorAt(1, cur ? QColor(0xb8, 0xd6, 0xfb) : QColor(0xdc, 0xea, 0xfc));
            p.setPen(cur ? QColor(0x5b, 0x8d, 0xd4) : QColor(0xa0, 0xc0, 0xe8));
            p.setBrush(g);
            p.drawRoundedRect(QRectF(b).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        }
        QImage th = docs[i]->thumbnail(std::min(b.width() - 10, b.height() - 10));
        QRect tr(b.center().x() - th.width() / 2, b.center().y() - th.height() / 2, th.width(), th.height());
        p.fillRect(tr, QBrush(Icons::checkerboard(3)));
        p.drawImage(tr, th);
        p.setPen(QColor(0x60, 0x60, 0x60));
        p.setBrush(Qt::NoBrush);
        p.drawRect(tr.adjusted(-1, -1, 0, 0));
        if (docs[i]->isDirty())
            p.drawPixmap(QRect(b.left() + 1, b.top() + 1, 12, 12), Icons::pixmap("new", 12),
                         QRect(0, 0, 14, 14));
        if (i == m_hover) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(200, 60, 40));
            p.drawRoundedRect(closeRect(i), 2, 2);
            p.setPen(QPen(Qt::white, 1.5));
            QRect c = closeRect(i).adjusted(3, 3, -3, -3);
            p.drawLine(c.topLeft(), c.bottomRight());
            p.drawLine(c.topRight(), c.bottomLeft());
        }
    }
    // dropdown arrow
    QRect a = arrowRect();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(40, 40, 40));
    p.setBrush(Qt::NoBrush);
    QPolygonF t;
    t << QPointF(a.left() + 3, a.top() + 6) << QPointF(a.right() - 3, a.top() + 6) << QPointF(a.center().x() + 0.5, a.top() + 11);
    p.drawPolygon(t);
    p.drawLine(QPointF(a.left() + 3, a.top() + 3), QPointF(a.right() - 3, a.top() + 3));
}

void ImageTabs::mousePressEvent(QMouseEvent *e)
{
    QPoint pos = e->position().toPoint();
    if (arrowRect().contains(pos)) {
        QMenu m;
        const auto &docs = m_main->documents();
        for (int i = 0; i < docs.size(); ++i) {
            QAction *a = m.addAction(QIcon(QPixmap::fromImage(docs[i]->thumbnail(16))), docs[i]->displayName());
            a->setCheckable(true);
            a->setChecked(i == m_main->currentIndex());
            connect(a, &QAction::triggered, this, [this, i] { emit activated(i); });
        }
        m.exec(mapToGlobal(arrowRect().bottomLeft()));
        return;
    }
    for (int i = 0; i < m_main->documents().size(); ++i) {
        if (!boxRect(i).contains(pos))
            continue;
        if (e->button() == Qt::MiddleButton || (i == m_hover && closeRect(i).contains(pos)))
            emit closeRequested(i);
        else
            emit activated(i);
        return;
    }
}

void ImageTabs::mouseMoveEvent(QMouseEvent *e)
{
    int h = -1;
    for (int i = 0; i < m_main->documents().size(); ++i)
        if (boxRect(i).contains(e->position().toPoint()))
            h = i;
    if (h != m_hover) {
        m_hover = h;
        update();
        if (h >= 0)
            setToolTip(m_main->documents()[h]->displayName());
    }
}

void ImageTabs::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

// ============================================================ MainWindow

MainWindow::MainWindow()
{
    setWindowIcon(Icons::get("brush"));
    setAcceptDrops(true);
    m_tools = createTools(this);
    QSettings s(kAppName, kAppName);
    m_lastDir = s.value("lastDir", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString();
    m_settings.units = Units(s.value("units", 0).toInt());

    createActions();
    applyStyle();

    m_workspace = new Workspace(this);
    m_stack = new QStackedWidget(m_workspace);
    setCentralWidget(m_workspace);
    setMenuWidget(createTopArea());
    createPanels();
    createStatusBar();
    connect(m_workspace, &Workspace::resized, this, [this] {
        m_stack->setGeometry(m_workspace->rect());
        layoutPanels(false);
    });

    connect(&m_settings, &ToolSettings::changed, this, [this] {
        updateOptionsBar();
        if (view())
            view()->updateOverlay();
    });
    qApp->installEventFilter(this);
    m_pluginHost = new PluginHost(this);
    m_pluginHost->loadAll();

    if (!restoreGeometry(s.value("geometry").toByteArray()) || width() < 640 || height() < 480)
        resize(1100, 760);
    setActiveTool(ToolId::Brush);
    updateActions();
    updateTitle();
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
}

Document *MainWindow::document() const { return m_current >= 0 ? m_docs[m_current] : nullptr; }
CanvasView *MainWindow::view() const { return m_current >= 0 ? m_views[m_current] : nullptr; }

QAction *MainWindow::act(const QString &id, const QString &text, const QString &icon, const QKeySequence &ks,
                         void (MainWindow::*slot)())
{
    auto *a = new QAction(text, this);
    if (!icon.isEmpty())
        a->setIcon(Icons::get(icon));
    if (!ks.isEmpty())
        a->setShortcut(ks);
    if (slot)
        connect(a, &QAction::triggered, this, slot);
    QString tip = text;
    tip.remove('&');
    tip.remove("...");
    if (!ks.isEmpty())
        tip += QString(" (%1)").arg(ks.toString(QKeySequence::NativeText));
    a->setToolTip(tip);
    m_actions.insert(id, a);
    addAction(a);
    return a;
}

void MainWindow::createActions()
{
    // File
    act("new", tr("&New..."), "new", QKeySequence::New, &MainWindow::fileNew);
    act("open", tr("&Open..."), "open", QKeySequence::Open, &MainWindow::fileOpen);
    act("save", tr("&Save"), "save", QKeySequence::Save);
    connect(m_actions["save"], &QAction::triggered, this, [this] { fileSave(); });
    act("saveas", tr("Save &As..."), "save", QKeySequence("Ctrl+Shift+S"));
    connect(m_actions["saveas"], &QAction::triggered, this, [this] { fileSaveAs(); });
    act("close", tr("&Close"), "close", QKeySequence("Ctrl+W"), &MainWindow::fileClose);
    act("print", tr("&Print..."), "print", QKeySequence::Print, &MainWindow::filePrint);
    act("exit", tr("E&xit"), QString(), QKeySequence("Ctrl+Q"));
    connect(m_actions["exit"], &QAction::triggered, this, &QWidget::close);

    // Edit
    act("undo", tr("&Undo"), "undo", QKeySequence::Undo, &MainWindow::editUndo);
    act("redo", tr("&Redo"), "redo", QKeySequence("Ctrl+Y"), &MainWindow::editRedo);
    m_actions["redo"]->setShortcuts({QKeySequence("Ctrl+Y"), QKeySequence("Ctrl+Shift+Z")});
    act("cut", tr("Cu&t"), "cut", QKeySequence::Cut, &MainWindow::editCut);
    act("copy", tr("&Copy"), "copy", QKeySequence::Copy, &MainWindow::editCopy);
    act("copymerged", tr("Copy &Merged"), "copy", QKeySequence("Ctrl+Shift+C"), &MainWindow::editCopyMerged);
    act("paste", tr("&Paste"), "paste", QKeySequence::Paste, &MainWindow::editPaste);
    act("pastelayer", tr("Paste Into New &Layer"), "paste", QKeySequence("Ctrl+Shift+V"), &MainWindow::editPasteNewLayer);
    act("pasteimage", tr("Paste Into New &Image"), "paste", QKeySequence("Ctrl+Alt+V"), &MainWindow::editPasteNewImage);
    act("selectall", tr("Select &All"), "selectall", QKeySequence::SelectAll, &MainWindow::editSelectAll);
    act("deselect", tr("&Deselect"), "deselect", QKeySequence("Ctrl+D"), &MainWindow::editDeselect);
    act("invertsel", tr("&Invert Selection"), "invertselection", QKeySequence("Ctrl+I"), &MainWindow::editInvertSelection);
    act("erasesel", tr("&Erase Selection"), "erase", QKeySequence::Delete, &MainWindow::editEraseSelection);
    act("fillsel", tr("&Fill Selection"), "fill", QKeySequence(Qt::Key_Backspace), &MainWindow::editFillSelection);

    // View
    act("zoomin", tr("Zoom &In"), "zoomin", QKeySequence("Ctrl++"), &MainWindow::viewZoomIn);
    m_actions["zoomin"]->setShortcuts({QKeySequence("Ctrl++"), QKeySequence("Ctrl+=")});
    act("zoomout", tr("Zoom &Out"), "zoomout", QKeySequence("Ctrl+-"), &MainWindow::viewZoomOut);
    act("zoomwindow", tr("Zoom to &Window"), QString(), QKeySequence("Ctrl+B"), &MainWindow::viewZoomWindow);
    act("zoomsel", tr("Zoom to &Selection"), QString(), QKeySequence("Ctrl+Shift+B"), &MainWindow::viewZoomSelection);
    act("actualsize", tr("&Actual Size"), QString(), QKeySequence("Ctrl+0"), &MainWindow::viewActualSize);
    m_actions["actualsize"]->setShortcuts({QKeySequence("Ctrl+0"), QKeySequence("Ctrl+Shift+A")});
    QAction *grid = act("grid", tr("Pixel &Grid"), "grid");
    grid->setCheckable(true);
    connect(grid, &QAction::toggled, this, [this](bool on) {
        for (CanvasView *v : m_views)
            v->setShowGrid(on);
    });
    QAction *rulers = act("rulers", tr("&Rulers"), "rulers");
    rulers->setCheckable(true);
    connect(rulers, &QAction::toggled, this, [this](bool on) {
        for (CanvasView *v : m_views)
            v->setShowRulers(on);
    });

    QAction *tile = act("tileview", tr("&Tile Preview"), "tile", QKeySequence("Ctrl+Shift+T"));
    tile->setCheckable(true);
    tile->setToolTip(tr("Tile Preview: repeat the image around the canvas; painting wraps across edges (Ctrl+Shift+T)"));
    connect(tile, &QAction::toggled, this, [this](bool on) {
        m_settings.tileMode = on;
        for (CanvasView *v : m_views)
            v->setTiled(on);
        m_settings.notify();
    });

    // Image
    act("crop", tr("Crop to Selection"), "crop", QKeySequence("Ctrl+Shift+X"), &MainWindow::imageCrop);
    act("resize", tr("&Resize..."), "resize", QKeySequence("Ctrl+R"), &MainWindow::imageResize);
    act("canvassize", tr("Canvas &Size..."), "canvassize", QKeySequence("Ctrl+Shift+R"), &MainWindow::imageCanvasSize);
    act("fliph", tr("Flip &Horizontal"), "fliph", QKeySequence(), &MainWindow::imageFlipH);
    act("flipv", tr("Flip &Vertical"), "flipv", QKeySequence(), &MainWindow::imageFlipV);
    act("rotcw", tr("Rotate 90° &Clockwise"), "rotatecw", QKeySequence("Ctrl+H"), &MainWindow::imageRotateCW);
    act("rotccw", tr("Rotate 90° Co&unter-clockwise"), "rotateccw", QKeySequence("Ctrl+G"), &MainWindow::imageRotateCCW);
    act("rot180", tr("Rotate &180°"), "rotate180", QKeySequence("Ctrl+J"), &MainWindow::imageRotate180);
    act("flatten", tr("&Flatten"), "flatten", QKeySequence("Ctrl+Shift+F"), &MainWindow::imageFlatten);

    // Layers
    act("layeradd", tr("&Add New Layer"), "layeradd", QKeySequence("Ctrl+Shift+N"), &MainWindow::layerAdd);
    act("layerdelete", tr("&Delete Layer"), "layerdelete", QKeySequence("Ctrl+Shift+Del"), &MainWindow::layerDelete);
    act("layerdup", tr("D&uplicate Layer"), "layerduplicate", QKeySequence("Ctrl+Shift+D"), &MainWindow::layerDuplicate);
    act("layermerge", tr("&Merge Layer Down"), "layermerge", QKeySequence("Ctrl+M"), &MainWindow::layerMergeDown);
    act("layerimport", tr("&Import From File..."), "importlayer", QKeySequence(), &MainWindow::layerImport);
    act("layerfliph", tr("Flip &Horizontal"), "fliph", QKeySequence(), &MainWindow::layerFlipH);
    act("layerflipv", tr("Flip &Vertical"), "flipv", QKeySequence(), &MainWindow::layerFlipV);
    act("layerrotate", tr("&Rotate / Zoom..."), "rotatezoom", QKeySequence("Ctrl+Shift+Z"), &MainWindow::layerRotateZoom);
    act("layerprops", tr("Layer &Properties..."), "layerprops", QKeySequence(Qt::Key_F4), &MainWindow::showLayerProperties);
    act("layernormal", tr("Create &Normal Map Layer..."), "normalmap", QKeySequence(), &MainWindow::layerCreateNormalMap);
    act("layerup", tr("Move Layer &Up"), "layerup", QKeySequence(), &MainWindow::layerMoveUp);
    act("layerdown", tr("Move Layer &Down"), "layerdown", QKeySequence(), &MainWindow::layerMoveDown);
    // Ctrl+Shift+Z is taken by Rotate/Zoom, like the classic editor; keep Ctrl+Y for redo
    m_actions["redo"]->setShortcuts({QKeySequence("Ctrl+Y")});

    act("repeateffect", tr("Repeat Effect"), "effects", QKeySequence("Ctrl+F"), &MainWindow::repeatEffect);

    // History navigation
    act("histfirst", tr("Rewind to Beginning"), "first");
    connect(m_actions["histfirst"], &QAction::triggered, this, [this] {
        if (!document()) return;
        prepareHistoryNavigation();
        document()->jumpTo(0);
    });
    act("histlast", tr("Fast-forward to End"), "last");
    connect(m_actions["histlast"], &QAction::triggered, this, [this] {
        if (!document()) return;
        prepareHistoryNavigation();
        document()->jumpTo(document()->history().size() - 1);
    });

    // Window
    act("resetwindows", tr("&Reset Window Locations"), QString(), QKeySequence(), &MainWindow::resetPanels);
    QAction *transl = act("translucent", tr("&Translucent Windows"));
    transl->setCheckable(true);
    transl->setChecked(QSettings(kAppName, kAppName).value("translucent", true).toBool());
    act("nexttab", tr("&Next Image"), QString(), QKeySequence("Ctrl+Tab"));
    connect(m_actions["nexttab"], &QAction::triggered, this, [this] {
        if (m_docs.size() > 1) setCurrentDocument((m_current + 1) % m_docs.size());
    });
    act("prevtab", tr("&Previous Image"), QString(), QKeySequence("Ctrl+Shift+Tab"));
    connect(m_actions["prevtab"], &QAction::triggered, this, [this] {
        if (m_docs.size() > 1) setCurrentDocument((m_current - 1 + m_docs.size()) % m_docs.size());
    });

    act("help", tr("&Help Topics"), "help", QKeySequence::HelpContents, &MainWindow::showHelp);
    act("plugins", tr("&Plugins..."), "plugin", QKeySequence(), &MainWindow::showPlugins);
    act("about", tr("&About PixelForge"), "help", QKeySequence(), &MainWindow::showAbout);

    // swap colours (X), like the classic editor
    act("swapcolors", tr("Swap Colors"), "swap", QKeySequence(Qt::Key_X));
    connect(m_actions["swapcolors"], &QAction::triggered, this, [this] { m_colors.swap(); });
}

void MainWindow::createMenus()
{
    // created inside createTopArea (needs a free-standing menubar)
}

QWidget *MainWindow::createTopArea()
{
    auto *top = new QWidget;
    top->setObjectName("topArea");
    auto *h = new QHBoxLayout(top);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    auto *left = new QVBoxLayout;
    left->setSpacing(0);
    left->setContentsMargins(0, 0, 0, 0);

    auto *mb = new QMenuBar;
    mb->setNativeMenuBar(false);
    auto A = [this](const char *id) { return m_actions[id]; };

    QMenu *file = mb->addMenu(tr("&File"));
    file->addActions({A("new"), A("open")});
    file->addSeparator();
    file->addActions({A("close")});
    file->addSeparator();
    file->addActions({A("save"), A("saveas")});
    file->addSeparator();
    file->addAction(A("print"));
    file->addSeparator();
    file->addAction(A("exit"));

    QMenu *edit = mb->addMenu(tr("&Edit"));
    edit->addActions({A("undo"), A("redo")});
    edit->addSeparator();
    edit->addActions({A("cut"), A("copy"), A("copymerged"), A("paste"), A("pastelayer"), A("pasteimage")});
    edit->addSeparator();
    edit->addActions({A("selectall"), A("deselect")});
    edit->addSeparator();
    edit->addActions({A("erasesel"), A("fillsel"), A("invertsel")});

    QMenu *viewM = mb->addMenu(tr("&View"));
    viewM->addActions({A("zoomin"), A("zoomout"), A("zoomwindow"), A("zoomsel"), A("actualsize")});
    viewM->addSeparator();
    viewM->addActions({A("grid"), A("rulers"), A("tileview")});
    QMenu *units = viewM->addMenu(tr("&Units"));
    auto *ug = new QActionGroup(this);
    const QStringList unitNames = {tr("&Pixels"), tr("&Inches"), tr("&Centimeters")};
    for (int i = 0; i < 3; ++i) {
        QAction *a = units->addAction(unitNames[i]);
        a->setCheckable(true);
        a->setChecked(int(m_settings.units) == i);
        ug->addAction(a);
        connect(a, &QAction::triggered, this, [this, i] { m_unitsCombo->setCurrentIndex(i); });
    }

    QMenu *img = mb->addMenu(tr("&Image"));
    img->addActions({A("crop"), A("resize"), A("canvassize")});
    img->addSeparator();
    img->addActions({A("fliph"), A("flipv")});
    img->addSeparator();
    img->addActions({A("rotcw"), A("rotccw"), A("rot180")});
    img->addSeparator();
    img->addAction(A("flatten"));

    QMenu *layers = mb->addMenu(tr("&Layers"));
    layers->addActions({A("layeradd"), A("layerdelete"), A("layerdup"), A("layermerge"), A("layerimport")});
    layers->addSeparator();
    layers->addActions({A("layerfliph"), A("layerflipv"), A("layerrotate")});
    layers->addAction(A("layernormal"));
    layers->addSeparator();
    layers->addActions({A("layerup"), A("layerdown")});
    layers->addSeparator();
    layers->addAction(A("layerprops"));

    m_menuAdjust = mb->addMenu(tr("&Adjustments"));
    m_menuEffects = mb->addMenu(tr("Effe&cts"));
    m_menuEffects->addAction(A("repeateffect"));
    m_menuEffects->addSeparator();
    QHash<QString, QMenu *> subs;
    const auto &reg = effectRegistry();
    for (const EffectDef &d : reg) {
        QMenu *target = m_menuAdjust;
        if (!d.adjustment) {
            if (!subs.contains(d.submenu)) {
                QMenu *sm = m_menuEffects->addMenu(Icons::get("fx_" + d.submenu.toLower()), tr(d.submenu.toUtf8().constData()));
                subs.insert(d.submenu, sm);
            }
            target = subs[d.submenu];
            if (d.submenu == "Maps" && target->isEmpty()) {
                target->addAction(A("layernormal"));
                target->addSeparator();
            }
        }
        QAction *a = target->addAction(Icons::get(d.icon), d.params.isEmpty() ? d.name : d.name + "...");
        if (!d.shortcut.isEmpty())
            a->setShortcut(d.shortcut);
        const EffectDef *dp = &d;
        connect(a, &QAction::triggered, this, [this, dp] { runEffectDef(*dp); });
        m_actions.insert("fx:" + d.name, a);
        addAction(a);
    }

    QMenu *win = mb->addMenu(tr("&Window"));
    win->addAction(A("resetwindows"));
    win->addAction(A("translucent"));
    win->addSeparator();
    // panel toggles are added after panels exist (see createPanels)
    m_actions.insert("_windowmenu", win->menuAction());
    win->addSeparator();
    win->addActions({A("nexttab"), A("prevtab")});

    QMenu *help = mb->addMenu(tr("&Help"));
    help->addAction(A("help"));
    help->addAction(A("plugins"));
    help->addSeparator();
    help->addAction(A("about"));
    m_menus = {{"File", file}, {"Edit", edit}, {"View", viewM}, {"Image", img}, {"Layers", layers},
               {"Adjustments", m_menuAdjust}, {"Effects", m_menuEffects}, {"Window", win}, {"Help", help}};

    left->addWidget(mb);
    m_mainBar = createMainToolbar();
    left->addWidget(m_mainBar);
    m_optBar = createOptionsBar();
    left->addWidget(m_optBar);
    h->addLayout(left, 1);

    m_tabs = new ImageTabs(this);
    connect(m_tabs, &ImageTabs::activated, this, [this](int i) { setCurrentDocument(i); });
    connect(m_tabs, &ImageTabs::closeRequested, this, [this](int i) { closeDocument(i); });
    h->addWidget(m_tabs);
    return top;
}

QToolBar *MainWindow::createMainToolbar()
{
    auto *tb = new QToolBar;
    tb->setIconSize(QSize(16, 16));
    tb->setMovable(false);
    auto A = [this](const char *id) { return m_actions[id]; };
    tb->addActions({A("new"), A("open"), A("save"), A("print")});
    tb->addSeparator();
    tb->addActions({A("cut"), A("copy"), A("paste"), A("crop"), A("deselect")});
    tb->addSeparator();
    tb->addActions({A("undo"), A("redo")});
    tb->addSeparator();
    tb->addAction(A("zoomout"));
    m_zoomCombo = new QComboBox;
    m_zoomCombo->setEditable(true);
    m_zoomCombo->setFixedWidth(84);
    m_zoomCombo->setInsertPolicy(QComboBox::NoInsert);
    m_zoomCombo->addItem(tr("Window"));
    auto presets = CanvasView::zoomPresets();
    for (int i = presets.size() - 1; i >= 0; --i)
        m_zoomCombo->addItem(QString("%1%").arg(presets[i] * 100, 0, 'g', 4));
    connect(m_zoomCombo, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        if (!view()) return;
        if (i == 0) view()->zoomToWindow();
        else {
            QString t = m_zoomCombo->itemText(i);
            t.remove('%');
            view()->setZoom(t.toDouble() / 100.0);
        }
    });
    connect(m_zoomCombo->lineEdit(), &QLineEdit::returnPressed, this, [this] {
        if (!view()) return;
        QString t = m_zoomCombo->currentText();
        t.remove('%');
        bool ok;
        double z = t.trimmed().toDouble(&ok);
        if (ok && z > 0)
            view()->setZoom(z / 100.0);
        updateZoomCombo();
        view()->setFocus();
    });
    tb->addWidget(m_zoomCombo);
    tb->addAction(A("zoomin"));
    tb->addSeparator();
    tb->addActions({A("grid"), A("rulers"), A("tileview")});
    tb->addSeparator();
    auto *ul = new QLabel(tr(" Units: "));
    tb->addWidget(ul);
    m_unitsCombo = new QComboBox;
    m_unitsCombo->addItems({tr("Pixels"), tr("Inches"), tr("Centimeters")});
    m_unitsCombo->setCurrentIndex(int(m_settings.units));
    m_unitsCombo->setFixedWidth(100);
    connect(m_unitsCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_settings.units = Units(i);
        for (CanvasView *v : m_views)
            v->setShowRulers(v->showRulers());
        updateStatus();
    });
    tb->addWidget(m_unitsCombo);
    return tb;
}

static QToolButton *dropButton(QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setPopupMode(QToolButton::InstantPopup);
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    b->setIconSize(QSize(16, 16));
    return b;
}

QToolBar *MainWindow::createOptionsBar()
{
    auto *tb = new QToolBar;
    tb->setIconSize(QSize(16, 16));
    tb->setMovable(false);
    tb->addWidget(new QLabel(tr(" Tool: ")));
    m_toolPicker = dropButton(tb);
    auto *toolMenu = new QMenu(m_toolPicker);
    for (int i = 0; i < int(ToolId::Count); ++i) {
        Tool *t = m_tools[i];
        QAction *a = toolMenu->addAction(t->icon(), t->name());
        connect(a, &QAction::triggered, this, [this, i] { setActiveTool(ToolId(i)); });
    }
    m_toolPicker->setMenu(toolMenu);
    tb->addWidget(m_toolPicker);
    tb->addSeparator();

    auto add = [&](quint32 opt, QWidget *w) { m_optActions[opt].append(tb->addWidget(w)); };
    auto addAct = [&](quint32 opt, QAction *a) { tb->addAction(a); m_optActions[opt].append(a); };
    auto sep = [&](quint32 opt) { m_optActions[opt].append(tb->addSeparator()); };
    auto label = [](const QString &t) { return new QLabel(t); };

    // selection mode
    add(OptSelectionMode, label(tr("Selection Mode: ")));
    m_selModeButton = dropButton(tb);
    {
        auto *m = new QMenu(m_selModeButton);
        const QStringList names = {tr("Replace"), tr("Add (union) (Ctrl + Left Click)"), tr("Subtract (Right Click)"),
                                   tr("Intersect (Alt + Left Click)"), tr("Invert (\"xor\") (Ctrl + Right Click)")};
        const QStringList icons = {"selreplace", "selunion", "selexclude", "selintersect", "selxor"};
        for (int i = 0; i < 5; ++i) {
            QAction *a = m->addAction(Icons::get(icons[i]), names[i]);
            connect(a, &QAction::triggered, this, [this, i] {
                m_settings.selectionMode = SelectionMode(i);
                m_settings.notify();
            });
        }
        m_selModeButton->setMenu(m);
    }
    add(OptSelectionMode, m_selModeButton);
    sep(OptSelectionMode);

    // flood mode
    add(OptFloodMode, label(tr("Flood Mode: ")));
    m_floodButton = dropButton(tb);
    {
        auto *m = new QMenu(m_floodButton);
        connect(m->addAction(Icons::get("bucket"), tr("Contiguous")), &QAction::triggered, this, [this] {
            m_settings.globalFill = false;
            m_settings.notify();
        });
        connect(m->addAction(Icons::get("fill"), tr("Global")), &QAction::triggered, this, [this] {
            m_settings.globalFill = true;
            m_settings.notify();
        });
        m_floodButton->setMenu(m);
    }
    add(OptFloodMode, m_floodButton);
    sep(OptFloodMode);

    // brush width
    add(OptBrushWidth, label(tr("Brush width: ")));
    auto *minus = new QToolButton;
    minus->setText(QString::fromUtf8("−"));
    minus->setAutoRaise(true);
    minus->setFixedSize(18, 18);
    add(OptBrushWidth, minus);
    m_brushWidth = new QComboBox;
    m_brushWidth->setEditable(true);
    m_brushWidth->setFixedWidth(64);
    for (int w : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70,
                  75, 80, 85, 90, 95, 100, 125, 150, 175, 200, 225, 250, 275, 300, 325, 350, 375, 400, 425, 450,
                  475, 500})
        m_brushWidth->addItem(QString::number(w));
    m_brushWidth->setValidator(new QIntValidator(1, 2000, m_brushWidth));
    add(OptBrushWidth, m_brushWidth);
    auto *plus = new QToolButton;
    plus->setText("+");
    plus->setAutoRaise(true);
    plus->setFixedSize(18, 18);
    add(OptBrushWidth, plus);
    connect(m_brushWidth, &QComboBox::currentTextChanged, this, [this](const QString &t) {
        bool ok;
        int v = t.toInt(&ok);
        if (ok && v > 0 && v != m_settings.brushWidth) {
            m_settings.brushWidth = v;
            m_settings.notify();
        }
    });
    auto step = [this](int dir) {
        static const int steps[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 15, 20, 25, 30, 40, 50, 60, 70, 80, 90, 100,
                                    125, 150, 200, 250, 300, 400, 500, 750, 1000, 2000};
        int cur = m_settings.brushWidth, next = cur;
        if (dir > 0) {
            for (int s : steps) if (s > cur) { next = s; break; }
        } else {
            for (int i = int(std::size(steps)) - 1; i >= 0; --i) if (steps[i] < cur) { next = steps[i]; break; }
        }
        m_settings.brushWidth = next;
        m_settings.notify();
    };
    connect(minus, &QToolButton::clicked, this, [step] { step(-1); });
    connect(plus, &QToolButton::clicked, this, [step] { step(1); });
    sep(OptBrushWidth);

    // brush hardness
    add(OptHardness, label(tr("Hardness: ")));
    m_hardness = new ToleranceSlider;
    m_hardness->setToolTip(tr("Hardness: 100% paints a hard edge, lower values fade the brush out towards its rim"));
    m_hardness->setValue(m_settings.hardness);
    connect(m_hardness, &ToleranceSlider::valueChanged, this, [this](int v) {
        m_settings.hardness = v;
        m_settings.notify();
    });
    add(OptHardness, m_hardness);
    sep(OptHardness);

    // rounded rectangle radius
    add(OptRadius, label(tr("Radius: ")));
    m_radiusSpin = new QSpinBox;
    m_radiusSpin->setRange(0, 1000);
    m_radiusSpin->setValue(m_settings.radius);
    connect(m_radiusSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        m_settings.radius = v;
        m_settings.notify();
    });
    add(OptRadius, m_radiusSpin);
    sep(OptRadius);

    // font
    add(OptFont, label(tr("Font: ")));
    m_fontCombo = new QFontComboBox;
    m_fontCombo->setFixedWidth(160);
    m_fontCombo->setCurrentFont(QFont(m_settings.fontFamily));
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, [this](const QFont &f) {
        m_settings.fontFamily = f.family();
        m_settings.notify();
    });
    add(OptFont, m_fontCombo);
    m_fontSizeCombo = new QComboBox;
    m_fontSizeCombo->setEditable(true);
    m_fontSizeCombo->setFixedWidth(58);
    for (int s : {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72, 96, 144, 192, 288})
        m_fontSizeCombo->addItem(QString::number(s));
    m_fontSizeCombo->setValidator(new QIntValidator(1, 2000, m_fontSizeCombo));
    m_fontSizeCombo->setCurrentText(QString::number(m_settings.fontSize));
    connect(m_fontSizeCombo, &QComboBox::currentTextChanged, this, [this](const QString &t) {
        bool ok;
        int v = t.toInt(&ok);
        if (ok && v > 0 && v != m_settings.fontSize) {
            m_settings.fontSize = v;
            m_settings.notify();
        }
    });
    add(OptFont, m_fontSizeCombo);
    auto toggle = [this, &addAct](const QString &icon, const QString &tip, bool ToolSettings::*member) {
        QAction *a = new QAction(Icons::get(icon), tip, this);
        a->setCheckable(true);
        connect(a, &QAction::toggled, this, [this, member](bool on) {
            m_settings.*member = on;
            m_settings.notify();
        });
        addAct(OptFont, a);
        return a;
    };
    m_bold = toggle("bold", tr("Bold"), &ToolSettings::bold);
    m_italic = toggle("italic", tr("Italic"), &ToolSettings::italic);
    m_underline = toggle("underline", tr("Underline"), &ToolSettings::underline);
    m_strike = toggle("strike", tr("Strikeout"), &ToolSettings::strike);
    sep(OptFont);
    {
        auto *grp = new QActionGroup(this);
        const QStringList ic = {"alignleft", "aligncenter", "alignright"};
        const QStringList tips = {tr("Left Align"), tr("Center Align"), tr("Right Align")};
        for (int i = 0; i < 3; ++i) {
            m_align[i] = new QAction(Icons::get(ic[i]), tips[i], this);
            m_align[i]->setCheckable(true);
            grp->addAction(m_align[i]);
            connect(m_align[i], &QAction::triggered, this, [this, i] {
                m_settings.textAlign = i;
                m_settings.notify();
            });
            addAct(OptFont, m_align[i]);
        }
        m_align[0]->setChecked(true);
    }
    sep(OptFont);

    // shape type
    add(OptShapeType, label(tr("Shape type: ")));
    m_shapeButton = dropButton(tb);
    {
        auto *m = new QMenu(m_shapeButton);
        const QStringList names = {tr("Draw Shape Outline"), tr("Draw Filled Shape"), tr("Draw Filled Shape With Outline")};
        const QStringList icons = {"shapeoutline", "shapefill", "shapeboth"};
        for (int i = 0; i < 3; ++i)
            connect(m->addAction(Icons::get(icons[i]), names[i]), &QAction::triggered, this, [this, i] {
                m_settings.shapeType = ShapeType(i);
                m_settings.notify();
            });
        m_shapeButton->setMenu(m);
    }
    add(OptShapeType, m_shapeButton);
    sep(OptShapeType);

    // gradient
    {
        auto *grp = new QActionGroup(this);
        const QStringList ic = {"gradlinear", "gradreflected", "graddiamond", "gradradial", "gradconical"};
        const QStringList tips = {tr("Linear"), tr("Linear (Reflected)"), tr("Linear (Diamond)"), tr("Radial"), tr("Conical")};
        for (int i = 0; i < 5; ++i) {
            m_gradTypes[i] = new QAction(Icons::get(ic[i]), tips[i], this);
            m_gradTypes[i]->setCheckable(true);
            grp->addAction(m_gradTypes[i]);
            connect(m_gradTypes[i], &QAction::triggered, this, [this, i] {
                m_settings.gradientType = GradientType(i);
                m_settings.notify();
            });
            addAct(OptGradient, m_gradTypes[i]);
        }
        m_gradTypes[0]->setChecked(true);
        sep(OptGradient);
        auto *cg = new QActionGroup(this);
        m_gradColor = new QAction(Icons::get("gradcolor"), tr("Color Mode"), this);
        m_gradTransp = new QAction(Icons::get("gradtransparency"), tr("Transparency Mode"), this);
        for (QAction *a : {m_gradColor, m_gradTransp}) {
            a->setCheckable(true);
            cg->addAction(a);
            addAct(OptGradient, a);
        }
        m_gradColor->setChecked(true);
        connect(m_gradColor, &QAction::triggered, this, [this] { m_settings.gradientTransparency = false; m_settings.notify(); });
        connect(m_gradTransp, &QAction::triggered, this, [this] { m_settings.gradientTransparency = true; m_settings.notify(); });
        sep(OptGradient);
    }

    // fill
    add(OptFill, label(tr("Fill: ")));
    m_fillCombo = new QComboBox;
    for (int i = 0; i < fillStyleCount(); ++i) {
        QPixmap pm(24, 14);
        pm.fill(Qt::white);
        QPainter p(&pm);
        p.fillRect(pm.rect(), QBrush(Qt::black, fillStyleBrush(i)));
        p.setPen(Qt::gray);
        p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
        p.end();
        m_fillCombo->addItem(QIcon(pm), fillStyleName(i));
    }
    m_fillCombo->setIconSize(QSize(24, 14));
    m_fillCombo->setFixedWidth(150);
    connect(m_fillCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_settings.fillStyle = i;
        m_settings.notify();
    });
    add(OptFill, m_fillCombo);
    sep(OptFill);

    // tolerance
    add(OptTolerance, label(tr("Tolerance: ")));
    m_tolerance = new ToleranceSlider;
    connect(m_tolerance, &ToleranceSlider::valueChanged, this, [this](int v) {
        m_settings.tolerance = v;
        m_settings.notify();
    });
    add(OptTolerance, m_tolerance);
    sep(OptTolerance);

    // colour picker sampling
    add(OptSampling, label(tr("Sampling: ")));
    m_samplingCombo = new QComboBox;
    m_samplingCombo->addItems({tr("Single Pixel"), tr("3 x 3 Region"), tr("5 x 5 Region"), tr("7 x 7 Region"), tr("9 x 9 Region")});
    connect(m_samplingCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_settings.sampling = i;
        m_settings.notify();
    });
    add(OptSampling, m_samplingCombo);
    m_sampleLayerCombo = new QComboBox;
    m_sampleLayerCombo->addItems({tr("Layer"), tr("Image")});
    connect(m_sampleLayerCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_settings.sampleImage = i == 1;
        m_settings.notify();
    });
    add(OptSampling, m_sampleLayerCombo);
    add(OptSampling, label(tr(" After select: ")));
    m_afterPickCombo = new QComboBox;
    m_afterPickCombo->addItems({tr("Do not switch tool"), tr("Switch to previous tool"), tr("Switch to Pencil tool")});
    connect(m_afterPickCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_settings.afterPick = AfterPick(i);
        m_settings.notify();
    });
    add(OptSampling, m_afterPickCombo);
    sep(OptSampling);

    // line style
    add(OptLineStyle, label(tr("Dash: ")));
    m_lineStyleCombo = new QComboBox;
    for (int i = 0; i < 5; ++i) {
        QPixmap pm(50, 12);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        static const Qt::PenStyle st[] = {Qt::SolidLine, Qt::DashLine, Qt::DotLine, Qt::DashDotLine, Qt::DashDotDotLine};
        p.setPen(QPen(Qt::black, 2, st[i], Qt::FlatCap));
        p.drawLine(2, 6, 48, 6);
        p.end();
        m_lineStyleCombo->addItem(QIcon(pm), QString());
    }
    m_lineStyleCombo->setIconSize(QSize(50, 12));
    connect(m_lineStyleCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_settings.lineStyle = i;
        m_settings.notify();
    });
    add(OptLineStyle, m_lineStyleCombo);
    sep(OptLineStyle);

    // resampling quality for move tool
    add(OptResample, label(tr("Quality: ")));
    m_resampleCombo = new QComboBox;
    m_resampleCombo->addItems({tr("Smooth"), tr("Nearest Neighbor")});
    connect(m_resampleCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_settings.smoothResample = i == 0;
        m_settings.notify();
    });
    add(OptResample, m_resampleCombo);
    sep(OptResample);

    // antialiasing & blending
    m_aaButton = dropButton(tb);
    {
        auto *m = new QMenu(m_aaButton);
        connect(m->addAction(Icons::get("aa_on"), tr("Antialiasing Enabled")), &QAction::triggered, this,
                [this] { m_settings.antialias = true; m_settings.notify(); });
        connect(m->addAction(Icons::get("aa_off"), tr("Antialiasing Disabled")), &QAction::triggered, this,
                [this] { m_settings.antialias = false; m_settings.notify(); });
        m_aaButton->setMenu(m);
    }
    add(OptAntialias, m_aaButton);
    m_blendButton = dropButton(tb);
    {
        auto *m = new QMenu(m_blendButton);
        connect(m->addAction(Icons::get("blend_on"), tr("Normal Blending")), &QAction::triggered, this,
                [this] { m_settings.alphaBlend = true; m_settings.notify(); });
        connect(m->addAction(Icons::get("blend_off"), tr("Overwrite")), &QAction::triggered, this,
                [this] { m_settings.alphaBlend = false; m_settings.notify(); });
        m_blendButton->setMenu(m);
    }
    add(OptAlphaBlend, m_blendButton);
    return tb;
}

void MainWindow::createPanels()
{
    m_toolsPanel = new ToolsPanel(this);
    m_toolsWin = new FloatingPanel(tr("Tools"), m_workspace);
    m_toolsWin->setContent(m_toolsPanel);

    m_colorsPanel = new ColorsPanel(&m_colors, this);
    m_colorsWin = new FloatingPanel(tr("Colors"), m_workspace);
    m_colorsWin->setContent(m_colorsPanel);
    connect(m_colorsPanel, &ColorsPanel::sizeChangedRequest, this, [this] {
        m_colorsWin->adjustSize();
        m_colorsWin->resize(m_colorsWin->sizeHint());
        m_colorsWin->placeAnchored();
    });

    m_historyPanel = new HistoryPanel(this, m_actions["histfirst"], m_actions["undo"], m_actions["redo"], m_actions["histlast"]);
    m_historyWin = new FloatingPanel(tr("History"), m_workspace);
    m_historyWin->setContent(m_historyPanel);
    m_historyWin->setResizable(true);

    m_layersPanel = new LayersPanel(this, {m_actions["layeradd"], m_actions["layerdelete"], m_actions["layerdup"],
                                           m_actions["layermerge"], m_actions["layerup"], m_actions["layerdown"],
                                           m_actions["layerprops"]});
    m_layersWin = new FloatingPanel(tr("Layers"), m_workspace);
    m_layersWin->setContent(m_layersPanel);
    m_layersWin->setResizable(true);

    m_panelSeparator = m_actions["_windowmenu"]->menu()->insertSeparator(m_actions["nexttab"]);
    registerPanel(m_toolsWin, "wintools", tr("&Tools"), QKeySequence(Qt::Key_F5), Icons::get("tools"), true);
    registerPanel(m_historyWin, "winhistory", tr("&History"), QKeySequence(Qt::Key_F6), Icons::get("history"), true);
    registerPanel(m_layersWin, "winlayers", tr("&Layers"), QKeySequence(Qt::Key_F7), Icons::get("layers"), true);
    registerPanel(m_colorsWin, "wincolors", tr("&Colors"), QKeySequence(Qt::Key_F8), Icons::get("colors"), true);

    m_animPanel = new AnimationPanel(this);
    addFloatingPanel("winanim", tr("&Animation"), m_animPanel, QKeySequence(Qt::Key_F9), Icons::get("animation"), false,
                     QSize(230, 330));
    connect(m_actions["translucent"], &QAction::toggled, this, [this](bool on) {
        for (FloatingPanel *p : m_panels)
            p->setTranslucency(on);
    });
}

void MainWindow::registerPanel(FloatingPanel *w, const QString &id, const QString &name, const QKeySequence &ks,
                               const QIcon &icon, bool defaultVisible)
{
    QSettings s(kAppName, kAppName);
    QAction *a = new QAction(icon, name, this);
    a->setShortcut(ks);
    a->setCheckable(true);
    a->setChecked(s.value("panel/" + id, defaultVisible).toBool());
    w->setVisible(a->isChecked());
    connect(a, &QAction::toggled, w, [w](bool on) {
        w->setVisible(on);
        if (on)
            w->raise();
    });
    connect(w, &FloatingPanel::closeRequested, a, [a] { a->setChecked(false); });
    m_actions["_windowmenu"]->menu()->insertAction(m_panelSeparator, a);
    m_actions.insert(id, a);
    addAction(a);
    w->setTranslucency(m_actions["translucent"]->isChecked());
    m_panels.append(w);
    m_panelIds.append(id);
}

FloatingPanel *MainWindow::addFloatingPanel(const QString &id, const QString &title, QWidget *content,
                                            const QKeySequence &ks, const QIcon &icon, bool defaultVisible,
                                            const QSize &size)
{
    auto *w = new FloatingPanel(QString(title).remove('&'), m_workspace);
    w->setContent(content);
    w->setResizable(true);
    // extra windows line up to the left of the History window
    int x = 212;
    for (int i = 4; i < m_panels.size(); ++i)
        x += m_panels[i]->width() + 6;
    w->setHomeGeometry(QRect(QPoint(x, 6), size), Qt::AlignRight | Qt::AlignTop);
    w->resize(size);
    registerPanel(w, id, title, ks, icon, defaultVisible);
    if (m_panelsPlaced)
        w->resetToHome();
    return w;
}

void MainWindow::layoutPanels(bool initial)
{
    if (!m_workspace->isVisible())
        return;
    if (initial || !m_panelsPlaced) {
        m_toolsWin->adjustSize();
        m_colorsWin->adjustSize();
        QSize ts = m_toolsWin->sizeHint();
        QSize cs = m_colorsWin->sizeHint();
        m_toolsWin->setHomeGeometry(QRect(QPoint(6, 6), ts), Qt::AlignLeft | Qt::AlignTop);
        m_colorsWin->setHomeGeometry(QRect(QPoint(6, 6), cs), Qt::AlignLeft | Qt::AlignBottom);
        m_historyWin->setHomeGeometry(QRect(QPoint(6, 6), QSize(200, 200)), Qt::AlignRight | Qt::AlignTop);
        m_layersWin->setHomeGeometry(QRect(QPoint(6, 6), QSize(200, 220)), Qt::AlignRight | Qt::AlignBottom);
        for (FloatingPanel *p : m_panels) {
            p->resetToHome();
            p->raise();
        }
        m_panelsPlaced = true;
        return;
    }
    for (FloatingPanel *p : m_panels)
        p->placeAnchored();
}

void MainWindow::resetPanels()
{
    for (const char *id : {"wintools", "winhistory", "winlayers", "wincolors"})
        m_actions[id]->setChecked(true);
    layoutPanels(true);
}

void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    if (!m_panelsPlaced)
        QTimer::singleShot(0, this, [this] {
            m_stack->setGeometry(m_workspace->rect());
            layoutPanels(true);
        });
}

void MainWindow::createStatusBar()
{
    QStatusBar *sb = statusBar();
    sb->setSizeGripEnabled(true);
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(2, 0, 2, 0);
    h->setSpacing(4);
    m_statusIcon = new QLabel;
    m_statusIcon->setPixmap(Icons::pixmap("help", 16));
    m_statusText = new QLabel;
    m_statusText->setMinimumWidth(100);
    m_statusText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    h->addWidget(m_statusIcon);
    h->addWidget(m_statusText, 1);
    sb->addWidget(w, 1);

    auto field = [sb](const QString &icon, int width) {
        auto *f = new QWidget;
        auto *fh = new QHBoxLayout(f);
        fh->setContentsMargins(6, 0, 6, 0);
        fh->setSpacing(4);
        auto *ic = new QLabel;
        ic->setPixmap(Icons::pixmap(icon, 16));
        auto *l = new QLabel;
        l->setMinimumWidth(width);
        fh->addWidget(ic);
        fh->addWidget(l);
        sb->addPermanentWidget(f);
        return l;
    };
    m_selSize = field("rectselect", 90);
    m_selSize->setToolTip(tr("Selection size"));
    m_cursorPos = field("move", 90);
    m_cursorPos->setToolTip(tr("Cursor position"));
    m_zoomLabel = field("zoom", 48);
    m_zoomLabel->setToolTip(tr("Zoom"));
}

void MainWindow::applyStyle()
{
    qApp->setStyle("Fusion");
    QPalette pal;
    pal.setColor(QPalette::Window, QColor(0xf0, 0xf0, 0xf0));
    pal.setColor(QPalette::WindowText, QColor(0x10, 0x10, 0x10));
    pal.setColor(QPalette::Base, Qt::white);
    pal.setColor(QPalette::AlternateBase, QColor(0xf5, 0xf7, 0xfa));
    pal.setColor(QPalette::Text, QColor(0x10, 0x10, 0x10));
    pal.setColor(QPalette::Button, QColor(0xf0, 0xf0, 0xf0));
    pal.setColor(QPalette::ButtonText, QColor(0x10, 0x10, 0x10));
    pal.setColor(QPalette::Highlight, QColor(0x33, 0x99, 0xff));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xe1));
    pal.setColor(QPalette::ToolTipText, Qt::black);
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x9a, 0x9a, 0x9a));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x9a, 0x9a, 0x9a));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x9a, 0x9a, 0x9a));
    qApp->setPalette(pal);
    QFont f = qApp->font();
    if (f.pointSizeF() > 10)
        f.setPointSizeF(9.5);
    qApp->setFont(f);

    qApp->setStyleSheet(R"(
        #topArea { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #ffffff, stop:0.5 #f3f6fb, stop:1 #dbe4f1);
                   border-bottom: 1px solid #a6b5ca; }
        QMenuBar { background: transparent; spacing: 1px; }
        QMenuBar::item { padding: 3px 8px; background: transparent; border: 1px solid transparent; border-radius: 2px; }
        QMenuBar::item:selected { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #f4f9fe, stop:1 #d5e7fb); border-color: #a7c6ec; }
        QMenuBar::item:pressed { background: #cfe3fb; border-color: #84acdd; }
        QMenu { background: #fbfbfb; border: 1px solid #979797; padding: 2px; }
        QMenu::item { padding: 4px 28px 4px 26px; border: 1px solid transparent; border-radius: 2px; }
        QMenu::item:selected { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #eef5fd, stop:1 #d6e7fb); border-color: #aecff7; color: #000; }
        QMenu::item:disabled { color: #9a9a9a; }
        QMenu::separator { height: 1px; background: #e0e0e0; margin: 3px 4px 3px 26px; }
        QMenu::icon { padding-left: 4px; }
        QToolBar { background: transparent; border: none; spacing: 1px; padding: 1px 3px; }
        QToolBar::separator { width: 1px; background: #b4c2d6; margin: 4px 5px; }
        QToolBar QLabel { color: #1a1a1a; }
        QToolButton { border: 1px solid transparent; border-radius: 2px; padding: 2px; background: transparent; }
        QToolButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #fbfdff, stop:1 #d9ebfd); border-color: #8fb4e0; }
        QToolButton:pressed, QToolButton:checked { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #e2eefb, stop:1 #bcd7f5); border-color: #6c93c4; }
        QToolButton:disabled { background: transparent; border-color: transparent; }
        QToolButton[popupMode="2"] { padding-right: 10px; }
        QToolButton::menu-indicator { subcontrol-position: right center; width: 7px; }
        QStatusBar { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #f7f8fa, stop:1 #dfe5ee); border-top: 1px solid #a9b6c8; }
        QStatusBar::item { border: none; border-left: 1px solid #b9c4d4; }
        #historyList, #layerList { border: 1px solid #a9b4c4; background: white; }
        #historyList::item { padding: 1px; }
        #historyList::item:selected { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #dcebfc, stop:1 #c1dbfc); color: black; border: 1px solid #7da2ce; }
        #panelToolbar { background: transparent; padding: 1px 0px; spacing: 0px; }
        #panelToolbar QToolButton { padding: 1px; margin: 0px; }
    )");
}

// ============================================================ documents

void MainWindow::addDocument(Document *doc, bool fit)
{
    doc->setParent(this);
    auto *v = new CanvasView(doc, this);
    v->setShowGrid(m_actions["grid"]->isChecked());
    v->setShowRulers(m_actions["rulers"]->isChecked());
    v->setTiled(m_actions["tileview"]->isChecked());
    m_docs.append(doc);
    m_views.append(v);
    m_stack->addWidget(v);
    connect(v, &CanvasView::zoomChanged, this, [this, v] {
        if (v == view()) {
            updateZoomCombo();
            updateTitle();
            updateStatus();
        }
    });
    connect(v, &CanvasView::mouseMoved, this, [this, v](const QPointF &p) {
        if (v != view())
            return;
        m_cursorPos->setText(QString("%1, %2").arg(formatLength(std::floor(p.x()))).arg(formatLength(std::floor(p.y()))));
    });
    connect(doc, &Document::historyChanged, this, [this, doc] {
        if (doc == document())
            updateActions();
        m_tabs->refresh();
    });
    connect(doc, &Document::dirtyChanged, this, [this, doc] {
        if (doc == document())
            updateTitle();
        m_tabs->refresh();
    });
    connect(doc, &Document::selectionChanged, this, [this, doc] {
        if (doc == document()) {
            updateStatus();
            updateActions();
        }
    });
    connect(doc, &Document::sizeChanged, this, [this, doc] {
        if (doc == document())
            updateStatus();
    });
    connect(doc, &Document::layersChanged, this, [this, doc] {
        if (doc == document())
            updateActions();
    });
    connect(doc, &Document::stateRestored, this, [this, doc] {
        if (doc == document())
            if (Tool *t = currentTool())
                t->reset();
    });
    auto *thumbTimer = new QTimer(doc);
    thumbTimer->setSingleShot(true);
    thumbTimer->setInterval(400);
    connect(thumbTimer, &QTimer::timeout, m_tabs, &ImageTabs::refresh);
    connect(doc, &Document::imageChanged, thumbTimer, [thumbTimer] { thumbTimer->start(); });

    setCurrentDocument(m_docs.size() - 1);
    m_tabs->updateGeometry();
    if (fit) {
        QTimer::singleShot(0, v, [v] {
            QSize vp = v->viewport()->size();
            Document *d = v->document();
            if (d->width() > vp.width() - 10 || d->height() > vp.height() - 10)
                v->zoomToWindow();
            else
                v->setZoom(1.0);
        });
    }
}

void MainWindow::setCurrentDocument(int i)
{
    if (i == m_current && i >= 0)
        return;
    if (Tool *t = currentTool()) {
        t->commit();
        t->reset();
    }
    m_current = i;
    if (i >= 0) {
        m_stack->setCurrentWidget(m_views[i]);
        m_views[i]->refreshCursor();
        m_views[i]->setFocus();
    }
    m_historyPanel->setDocument(document());
    m_animPanel->setDocument(document());
    if (m_pluginHost)
        m_pluginHost->setDocument(document());
    m_layersPanel->setDocument(document());
    m_tabs->refresh();
    updateTitle();
    updateActions();
    updateZoomCombo();
    updateStatus();
}

void MainWindow::updateTitle()
{
    Document *d = document();
    if (!d) {
        setWindowTitle(kAppName);
        return;
    }
    setWindowTitle(QString("%1%2 (%3%) - %4")
                       .arg(d->displayName(), d->isDirty() ? "*" : "")
                       .arg(int(std::round(view()->zoom() * 100)))
                       .arg(kAppName));
}

void MainWindow::updateActions()
{
    Document *d = document();
    bool has = d != nullptr;
    bool sel = has && d->hasSelection();
    for (const char *id : {"save", "saveas", "close", "print", "copy", "copymerged", "cut", "paste", "pastelayer",
                           "selectall", "invertsel", "erasesel", "fillsel", "zoomin", "zoomout", "zoomwindow",
                           "actualsize", "resize", "canvassize", "fliph", "flipv", "rotcw", "rotccw", "rot180",
                           "flatten", "layeradd", "layerdup", "layerimport", "layerfliph", "layerflipv",
                           "layerrotate", "layerprops", "layernormal", "histfirst", "histlast", "repeateffect"})
        m_actions[id]->setEnabled(has);
    m_actions["crop"]->setEnabled(sel);
    m_actions["deselect"]->setEnabled(sel);
    m_actions["zoomsel"]->setEnabled(sel);
    m_actions["undo"]->setEnabled(has && d->canUndo());
    m_actions["redo"]->setEnabled(has && d->canRedo());
    m_actions["histfirst"]->setEnabled(has && d->canUndo());
    m_actions["histlast"]->setEnabled(has && d->canRedo());
    m_actions["layerdelete"]->setEnabled(has && d->layerCount() > 1);
    m_actions["layermerge"]->setEnabled(has && d->activeIndex() > 0);
    m_actions["layerup"]->setEnabled(has && d->activeIndex() < d->layerCount() - 1);
    m_actions["layerdown"]->setEnabled(has && d->activeIndex() > 0);
    m_actions["flatten"]->setEnabled(has && d->layerCount() > 1);
    m_actions["repeateffect"]->setEnabled(has && m_lastEffect);
    m_actions["pasteimage"]->setEnabled(true);
    for (auto it = m_actions.begin(); it != m_actions.end(); ++it)
        if (it.key().startsWith("fx:"))
            it.value()->setEnabled(has);
    if (m_lastEffect)
        m_actions["repeateffect"]->setText(tr("Repeat %1").arg(m_lastEffect->name));
}

void MainWindow::updateZoomCombo()
{
    if (!view()) {
        m_zoomCombo->setEditText(QString());
        m_zoomLabel->setText(QString());
        return;
    }
    QString t = QString("%1%").arg(std::round(view()->zoom() * 10000) / 100.0, 0, 'g', 5);
    m_zoomCombo->setEditText(t);
    m_zoomLabel->setText(t);
}

QString MainWindow::formatLength(double px) const
{
    Document *d = document();
    double dpi = d ? d->dpi : 96.0;
    switch (m_settings.units) {
    case Units::Inches: return QString::number(px / dpi, 'f', 2);
    case Units::Centimeters: return QString::number(px / dpi * 2.54, 'f', 2);
    default: return QString::number(qint64(px));
    }
}

void MainWindow::updateStatus()
{
    Document *d = document();
    if (!d) {
        m_selSize->clear();
        m_cursorPos->clear();
        return;
    }
    QRect b = d->hasSelection() ? d->selection().boundingRect().toAlignedRect() : d->rect();
    m_selSize->setText(QString("%1 x %2").arg(formatLength(b.width()), formatLength(b.height())));
    updateZoomCombo();
}

void MainWindow::setToolStatus(const QString &s)
{
    m_statusText->setText(s);
    m_statusText->setToolTip(s);
}

// ============================================================ tools

void MainWindow::setActiveTool(ToolId id)
{
    if (!m_tools.isEmpty() && id != m_toolId && currentTool())
        currentTool()->deactivate();
    if (id != m_toolId)
        m_prevTool = m_toolId;
    m_toolId = id;
    Tool *t = currentTool();
    t->activate();
    m_toolsPanel->setCurrent(int(id));
    m_toolPicker->setIcon(t->icon());
    m_toolPicker->setToolTip(t->name());
    m_statusIcon->setPixmap(Icons::pixmap(t->iconName(), 16));
    setToolStatus(t->helpText());
    updateOptionsBar();
    for (CanvasView *v : m_views) {
        v->refreshCursor();
        v->updateOverlay();
    }
}

void MainWindow::switchToPreviousTool()
{
    setActiveTool(m_prevTool);
}

void MainWindow::prepareHistoryNavigation()
{
    if (Tool *t = currentTool())
        t->prepareForHistory();
}

void MainWindow::commitTool(Tool *except)
{
    Tool *t = currentTool();
    if (t && t != except)
        t->commit();
}

void MainWindow::updateOptionsBar()
{
    if (m_tools.isEmpty() || !m_optBar)
        return;
    quint32 opts = currentTool()->options();
    for (auto it = m_optActions.begin(); it != m_optActions.end(); ++it)
        for (QAction *a : it.value())
            a->setVisible(opts & it.key());
    const ToolSettings &s = m_settings;
    {
        QSignalBlocker b(m_brushWidth);
        if (m_brushWidth->currentText() != QString::number(s.brushWidth))
            m_brushWidth->setEditText(QString::number(s.brushWidth));
    }
    m_aaButton->setIcon(Icons::get(s.antialias ? "aa_on" : "aa_off"));
    m_aaButton->setToolTip(s.antialias ? tr("Antialiasing Enabled") : tr("Antialiasing Disabled"));
    m_blendButton->setIcon(Icons::get(s.alphaBlend ? "blend_on" : "blend_off"));
    m_blendButton->setToolTip(s.alphaBlend ? tr("Normal Blending") : tr("Overwrite"));
    static const char *selIcons[] = {"selreplace", "selunion", "selexclude", "selintersect", "selxor"};
    m_selModeButton->setIcon(Icons::get(selIcons[int(s.selectionMode)]));
    m_floodButton->setIcon(Icons::get(s.globalFill ? "fill" : "bucket"));
    m_floodButton->setToolTip(s.globalFill ? tr("Global") : tr("Contiguous"));
    static const char *shapeIcons[] = {"shapeoutline", "shapefill", "shapeboth"};
    m_shapeButton->setIcon(Icons::get(shapeIcons[int(s.shapeType)]));
    m_tolerance->setValue(s.tolerance);
    m_hardness->setValue(s.hardness);
    {
        QSignalBlocker b(m_fillCombo);
        m_fillCombo->setCurrentIndex(s.fillStyle);
    }
    m_gradTypes[int(s.gradientType)]->setChecked(true);
    (s.gradientTransparency ? m_gradTransp : m_gradColor)->setChecked(true);
}

static bool isTextInput(QWidget *w)
{
    if (!w)
        return false;
    if (qobject_cast<QLineEdit *>(w) || qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QTextEdit *>(w) ||
        qobject_cast<QPlainTextEdit *>(w))
        return true;
    if (auto *c = qobject_cast<QComboBox *>(w))
        return c->isEditable();
    return false;
}

bool MainWindow::handleCanvasKey(QKeyEvent *e)
{
    Tool *t = currentTool();
    if (!t)
        return false;
    if (document() && t->keyPress(e))
        return true;
    if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false;
    QString txt = e->text().toUpper();
    if (txt.size() != 1 || !txt[0].isLetter())
        return false;
    QChar c = txt[0];
    QVector<ToolId> cycle;
    for (int i = 0; i < int(ToolId::Count); ++i)
        if (m_tools[i]->shortcut() == c)
            cycle << ToolId(i);
    if (cycle.isEmpty())
        return false;
    int idx = cycle.indexOf(m_toolId);
    ToolId next = idx < 0 ? cycle.first() : cycle[(idx + 1) % cycle.size()];
    if (e->modifiers() & Qt::ShiftModifier)
        next = idx < 0 ? cycle.last() : cycle[(idx - 1 + cycle.size()) % cycle.size()];
    setActiveTool(next);
    return true;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *e)
{
    if (e->type() != QEvent::ShortcutOverride && e->type() != QEvent::KeyPress)
        return QMainWindow::eventFilter(obj, e);
    auto *w = qobject_cast<QWidget *>(obj);
    if (!w || w->window() != this || QApplication::activeModalWidget() || QApplication::activePopupWidget())
        return QMainWindow::eventFilter(obj, e);
    if (isTextInput(QApplication::focusWidget()))
        return QMainWindow::eventFilter(obj, e);
    auto *ke = static_cast<QKeyEvent *>(e);
    Tool *t = currentTool();
    if (e->type() == QEvent::ShortcutOverride) {
        // while a tool is capturing typed text, plain keys must not fire menu shortcuts
        if (t && t->wantsKeys() && (!(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) ||
                                    ke->matches(QKeySequence::Paste))) {
            e->accept();
            return true;
        }
        // Enter / arrows go to tools before shortcuts
        if (t && t->isEditing() && (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)) {
            e->accept();
            return true;
        }
        // single-letter tool shortcuts take priority over X etc. only for tool letters
        return QMainWindow::eventFilter(obj, e);
    }
    // KeyPress
    if (ke->key() == Qt::Key_Space)
        return QMainWindow::eventFilter(obj, e);
    if (obj != view() && obj != (view() ? view()->viewport() : nullptr) && w != this && !qobject_cast<QToolButton *>(w) &&
        !qobject_cast<QListWidget *>(w) && !w->inherits("FloatingPanel") && !w->inherits("QMainWindow"))
        return QMainWindow::eventFilter(obj, e);
    if (handleCanvasKey(ke))
        return true;
    return QMainWindow::eventFilter(obj, e);
}

// ============================================================ file

void MainWindow::fileNew()
{
    QSize init(800, 600);
    if (const QMimeData *md = QApplication::clipboard()->mimeData(); md && md->hasImage()) {
        QImage img = qvariant_cast<QImage>(md->imageData());
        if (!img.isNull())
            init = img.size();
    }
    NewImageDialog dlg(init, 96, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    commitTool();
    auto *doc = new Document(dlg.imageSize(), Qt::white);
    doc->dpi = dlg.dpi();
    doc->resetHistory(tr("New Image"), Icons::get("new"));
    addDocument(doc);
}

static QString openFilter()
{
    QStringList exts;
    for (const QByteArray &f : QImageReader::supportedImageFormats())
        exts << "*." + QString::fromLatin1(f);
    exts << "*.ora";
    return QObject::tr("All images (%1);;OpenRaster (*.ora);;All files (*)").arg(exts.join(' '));
}

void MainWindow::fileOpen()
{
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Open"), m_lastDir, openFilter());
    for (const QString &f : files)
        openFile(f);
}

void MainWindow::openFile(const QString &path)
{
    QFileInfo fi(path);
    for (int i = 0; i < m_docs.size(); ++i)
        if (QFileInfo(m_docs[i]->filePath) == fi) {
            setCurrentDocument(i);
            return;
        }
    m_lastDir = fi.absolutePath();
    Document *doc = nullptr;
    if (fi.suffix().compare("ora", Qt::CaseInsensitive) == 0) {
        QVector<Layer> layers;
        QSize size;
        double dpi = 96;
        QString err;
        if (!Ora::load(path, layers, size, dpi, &err)) {
            QMessageBox::critical(this, kAppName, tr("Could not open %1:\n%2").arg(fi.fileName(), err));
            return;
        }
        doc = new Document(layers, size);
        doc->dpi = dpi;
    } else {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QImage img = reader.read();
        if (img.isNull()) {
            QMessageBox::critical(this, kAppName, tr("Could not open %1:\n%2").arg(fi.fileName(), reader.errorString()));
            return;
        }
        Layer l;
        l.image = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        l.name = tr("Background");
        doc = new Document({l}, img.size());
        if (img.dotsPerMeterX() > 0)
            doc->dpi = img.dotsPerMeterX() * 0.0254;
    }
    doc->filePath = fi.absoluteFilePath();
    doc->resetHistory(tr("Open Image"), Icons::get("open"));
    // replace a pristine untitled document, as the classic editor does
    if (m_docs.size() == 1 && m_docs[0]->filePath.isEmpty() && !m_docs[0]->isDirty() && m_docs[0]->history().size() == 1) {
        Document *old = m_docs[0];
        addDocument(doc);
        int idx = m_docs.indexOf(old);
        if (idx >= 0) {
            m_stack->removeWidget(m_views[idx]);
            delete m_views[idx];
            m_views.remove(idx);
            m_docs.remove(idx);
            delete old;
            m_current = m_docs.indexOf(doc);
            m_tabs->refresh();
        }
    } else {
        addDocument(doc);
    }
}

bool MainWindow::fileSave()
{
    Document *d = document();
    if (!d)
        return false;
    commitTool();
    if (d->filePath.isEmpty())
        return fileSaveAs();
    return saveDocument(d, d->filePath);
}

bool MainWindow::fileSaveAs()
{
    Document *d = document();
    if (!d)
        return false;
    commitTool();
    QStringList filters = {tr("OpenRaster (*.ora)"), tr("PNG (*.png)")};
    auto fmts = QImageWriter::supportedImageFormats();
    if (fmts.contains("jpg")) filters << tr("JPEG (*.jpg *.jpeg *.jpe *.jfif)");
    if (fmts.contains("bmp")) filters << tr("BMP (*.bmp *.dib)");
    if (fmts.contains("tif")) filters << tr("TIFF (*.tif *.tiff)");
    if (fmts.contains("webp")) filters << tr("WebP (*.webp)");
    if (fmts.contains("gif")) filters << tr("GIF (*.gif)");
    if (fmts.contains("ico")) filters << tr("Icon (*.ico)");
    if (fmts.contains("tga")) filters << tr("TGA (*.tga)");
    QString base = d->filePath.isEmpty() ? m_lastDir + "/" + tr("Untitled") + (d->layerCount() > 1 ? ".ora" : ".png")
                                         : d->filePath;
    QString selected = d->layerCount() > 1 ? filters[0] : filters[1];
    if (!d->filePath.isEmpty()) {
        QString ext = QFileInfo(d->filePath).suffix().toLower();
        for (const QString &f : filters)
            if (f.contains("*." + ext)) selected = f;
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Save As"), base, filters.join(";;"), &selected);
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty()) {
        QRegularExpression re("\\*\\.(\\w+)");
        auto m = re.match(selected);
        path += "." + (m.hasMatch() ? m.captured(1) : QString("png"));
    }
    return saveDocument(d, path);
}

bool MainWindow::saveDocument(Document *d, const QString &path)
{
    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();
    m_lastDir = fi.absolutePath();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = false;
    QString err;
    if (ext == "ora") {
        ok = Ora::save(path, d->layers(), d->size(), d->dpi, d->flattened(), &err);
    } else {
        QApplication::restoreOverrideCursor();
        if (d->layerCount() > 1) {
            auto r = QMessageBox::question(this, tr("Flatten Image"),
                                           tr("Saving to this file type requires flattening the image.\n\n"
                                              "The layers will be merged into one. You can undo this afterwards."),
                                           QMessageBox::Ok | QMessageBox::Cancel);
            if (r != QMessageBox::Ok)
                return false;
            imageFlatten();
        }
        int quality = -1;
        if (ext == "jpg" || ext == "jpeg" || ext == "jpe" || ext == "jfif" || ext == "webp") {
            SaveOptionsDialog dlg(ext == "webp" ? "WebP" : "JPEG", this);
            if (dlg.exec() != QDialog::Accepted)
                return false;
            quality = dlg.quality();
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QImage img = d->flattened().convertToFormat(QImage::Format_ARGB32);
        if (ext == "jpg" || ext == "jpeg" || ext == "jpe" || ext == "jfif" || ext == "bmp") {
            // formats without alpha: composite over white
            QImage flat(img.size(), QImage::Format_RGB32);
            flat.fill(Qt::white);
            QPainter p(&flat);
            p.drawImage(0, 0, img);
            p.end();
            img = flat;
        }
        img.setDotsPerMeterX(int(d->dpi / 0.0254));
        img.setDotsPerMeterY(int(d->dpi / 0.0254));
        QImageWriter w(path);
        if (quality > 0)
            w.setQuality(quality);
        ok = w.write(img);
        if (!ok)
            err = w.errorString();
    }
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::critical(this, kAppName, tr("Could not save %1:\n%2").arg(fi.fileName(), err));
        return false;
    }
    d->filePath = fi.absoluteFilePath();
    d->markSaved();
    updateTitle();
    m_tabs->refresh();
    return true;
}

void MainWindow::fileClose()
{
    if (m_current >= 0)
        closeDocument(m_current);
}

bool MainWindow::closeDocument(int index)
{
    Document *d = m_docs[index];
    if (index == m_current)
        commitTool();
    if (d->isDirty()) {
        setCurrentDocument(index);
        auto r = QMessageBox::warning(this, tr("Unsaved Changes"),
                                      tr("Save changes to \"%1\" before closing?").arg(d->displayName()),
                                      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (r == QMessageBox::Cancel)
            return false;
        if (r == QMessageBox::Save && !fileSave())
            return false;
    }
    if (Tool *t = currentTool())
        t->reset();
    CanvasView *v = m_views[index];
    m_stack->removeWidget(v);
    m_views.remove(index);
    m_docs.remove(index);
    m_current = -1;
    int next = m_docs.isEmpty() ? -1 : std::min(index, int(m_docs.size()) - 1);
    setCurrentDocument(next);
    v->deleteLater();
    d->deleteLater();
    m_tabs->updateGeometry();
    return true;
}

void MainWindow::filePrint()
{
    Document *d = document();
    if (!d)
        return;
    commitTool();
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QPainter p(&printer);
    QImage img = d->flattened();
    QRect page = p.viewport();
    QSize s = img.size();
    double inchW = s.width() / d->dpi, inchH = s.height() / d->dpi;
    QSizeF target(inchW * printer.resolution(), inchH * printer.resolution());
    if (target.width() > page.width() || target.height() > page.height())
        target.scale(page.size(), Qt::KeepAspectRatio);
    p.drawImage(QRectF(QPointF(0, 0), target), img);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    commitTool();
    while (!m_docs.isEmpty()) {
        int i = m_docs.size() - 1;
        for (int k = 0; k < m_docs.size(); ++k)
            if (m_docs[k]->isDirty()) i = k;
        if (!closeDocument(i)) {
            e->ignore();
            return;
        }
    }
    QSettings s(kAppName, kAppName);
    if (width() >= 640 && height() >= 480)
        s.setValue("geometry", saveGeometry());
    s.setValue("lastDir", m_lastDir);
    s.setValue("units", int(m_settings.units));
    s.setValue("translucent", m_actions["translucent"]->isChecked());
    for (const QString &id : m_panelIds)
        s.setValue("panel/" + id, m_actions[id]->isChecked());
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls() || e->mimeData()->hasImage())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasUrls()) {
        for (const QUrl &u : e->mimeData()->urls())
            if (u.isLocalFile())
                openFile(u.toLocalFile());
    } else if (e->mimeData()->hasImage()) {
        QImage img = qvariant_cast<QImage>(e->mimeData()->imageData());
        if (!img.isNull()) {
            if (document())
                pasteImage(img, true);
            else {
                Layer l;
                l.image = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                l.name = tr("Background");
                auto *doc = new Document({l}, img.size());
                doc->resetHistory(tr("New Image"), Icons::get("new"));
                addDocument(doc);
            }
        }
    }
}

// ============================================================ edit

void MainWindow::editUndo()
{
    if (!document()) return;
    prepareHistoryNavigation();
    document()->undo();
}

void MainWindow::editRedo()
{
    if (!document()) return;
    prepareHistoryNavigation();
    document()->redo();
}

QImage MainWindow::selectionImage(const QImage &src) const
{
    Document *d = document();
    QRect b = d->selectionBounds();
    if (b.isEmpty())
        return QImage();
    QImage out = src.copy(b);
    if (d->hasSelection()) {
        QPainter p(&out);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawImage(QPoint(0, 0), d->selectionMask(), b);
    }
    return out.convertToFormat(QImage::Format_ARGB32);
}

void MainWindow::editCopy()
{
    if (!document()) return;
    commitTool();
    QImage img = selectionImage(document()->activeImage());
    if (!img.isNull())
        QApplication::clipboard()->setImage(img);
}

void MainWindow::editCopyMerged()
{
    if (!document()) return;
    commitTool();
    QImage img = selectionImage(document()->flattened());
    if (!img.isNull())
        QApplication::clipboard()->setImage(img);
}

void MainWindow::editCut()
{
    Document *d = document();
    if (!d) return;
    commitTool();
    QImage img = selectionImage(d->activeImage());
    if (img.isNull())
        return;
    QApplication::clipboard()->setImage(img);
    {
        QPainter p(&d->activeImage());
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.drawImage(0, 0, d->selectionMask());
    }
    d->invalidate(d->selectionBounds());
    d->clearSelection();
    d->pushHistory(tr("Cut"), Icons::get("cut"));
}

bool MainWindow::pasteImage(const QImage &imgIn, bool newLayer)
{
    Document *d = document();
    if (!d || imgIn.isNull())
        return false;
    commitTool();
    QImage img = imgIn.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (img.width() > d->width() || img.height() > d->height()) {
        QMessageBox box(QMessageBox::Question, tr("Image larger than canvas"),
                        tr("The image being pasted is larger than the canvas size. What would you like to do?"),
                        QMessageBox::NoButton, this);
        QPushButton *expand = box.addButton(tr("Expand canvas"), QMessageBox::AcceptRole);
        QPushButton *keep = box.addButton(tr("Keep canvas size"), QMessageBox::RejectRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == expand) {
            QSize ns = d->size().expandedTo(img.size());
            QVector<Layer> layers = d->layers();
            for (int i = 0; i < layers.size(); ++i) {
                QImage n(ns, QImage::Format_ARGB32_Premultiplied);
                n.fill(i == 0 ? m_colors.secondary : QColor(Qt::transparent));
                QPainter p(&n);
                p.setCompositionMode(QPainter::CompositionMode_Source);
                p.drawImage(0, 0, layers[i].image);
                p.end();
                layers[i].image = n;
            }
            d->clearSelection();
            d->setLayers(layers, ns, d->activeIndex());
            d->pushHistory(tr("Expand Canvas"), Icons::get("canvassize"));
        } else if (box.clickedButton() != keep) {
            return false;
        }
    }
    if (newLayer) {
        QVector<Layer> layers = d->layers();
        Layer l;
        l.image = d->blankLayerImage();
        l.name = d->nextLayerName();
        layers.insert(d->activeIndex() + 1, l);
        d->setLayers(layers, d->size(), d->activeIndex() + 1);
        d->pushHistory(tr("Add New Layer"), Icons::get("layeradd"));
    }
    // paste at the top-left of the visible canvas area
    QPoint pos(0, 0);
    if (view()) {
        QRectF vis = view()->visibleImageRect();
        pos = QPoint(int(std::floor(vis.left())), int(std::floor(vis.top())));
        if (pos.x() + img.width() > d->width()) pos.setX(std::max(0, d->width() - img.width()));
        if (pos.y() + img.height() > d->height()) pos.setY(std::max(0, d->height() - img.height()));
    }
    setActiveTool(ToolId::Move);
    movetoolBeginPaste(tool(ToolId::Move), img, pos);
    return true;
}

void MainWindow::editPaste()
{
    pasteImage(QApplication::clipboard()->image(), false);
}

void MainWindow::editPasteNewLayer()
{
    pasteImage(QApplication::clipboard()->image(), true);
}

void MainWindow::editPasteNewImage()
{
    QImage img = QApplication::clipboard()->image();
    if (img.isNull())
        return;
    commitTool();
    auto *doc = new Document(img.size(), Qt::transparent);
    doc->resetHistory(tr("New Image"), Icons::get("new"));
    addDocument(doc);
    pasteImage(img, false);
}

void MainWindow::editSelectAll()
{
    Document *d = document();
    if (!d) return;
    commitTool();
    QPainterPath p;
    p.addRect(d->rect());
    d->setSelection(p);
    d->pushHistory(tr("Select All"), Icons::get("selectall"));
}

void MainWindow::editDeselect()
{
    Document *d = document();
    if (!d || !d->hasSelection()) return;
    commitTool();
    d->clearSelection();
    d->pushHistory(tr("Deselect"), Icons::get("deselect"));
}

void MainWindow::editInvertSelection()
{
    Document *d = document();
    if (!d) return;
    commitTool();
    QPainterPath all;
    all.addRect(d->rect());
    d->setSelection(d->hasSelection() ? all.subtracted(d->selection()) : all);
    d->pushHistory(tr("Invert Selection"), Icons::get("invertselection"));
}

void MainWindow::editEraseSelection()
{
    Document *d = document();
    if (!d) return;
    commitTool();
    QPainter p(&d->activeImage());
    p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    p.drawImage(0, 0, d->selectionMask());
    p.end();
    d->invalidate(d->selectionBounds());
    d->clearSelection();
    d->pushHistory(tr("Erase Selection"), Icons::get("erase"));
}

void MainWindow::editFillSelection()
{
    Document *d = document();
    if (!d) return;
    commitTool();
    QImage fill(d->size(), QImage::Format_ARGB32_Premultiplied);
    fill.fill(m_colors.primary);
    {
        QPainter p(&fill);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawImage(0, 0, d->selectionMask());
    }
    QPainter p(&d->activeImage());
    p.drawImage(0, 0, fill);
    p.end();
    d->invalidate(d->selectionBounds());
    d->pushHistory(tr("Fill Selection"), Icons::get("fill"));
}

// ============================================================ view

void MainWindow::viewZoomIn() { if (view()) view()->zoomIn(); }
void MainWindow::viewZoomOut() { if (view()) view()->zoomOut(); }
void MainWindow::viewZoomWindow() { if (view()) view()->zoomToWindow(); }
void MainWindow::viewActualSize() { if (view()) view()->actualSize(); }
void MainWindow::viewZoomSelection()
{
    if (view() && document()->hasSelection())
        view()->zoomToRect(document()->selection().boundingRect());
}

// ============================================================ image

void MainWindow::imageCrop()
{
    Document *d = document();
    if (!d || !d->hasSelection()) return;
    commitTool();
    QRect b = d->selectionBounds();
    if (b.isEmpty())
        return;
    QPainterPath sel = d->selection().translated(-b.topLeft());
    bool rectangular = d->selection().elementCount() <= 5 &&
                       QPainterPath(d->selection()).boundingRect() == QRectF(d->selection().boundingRect()) &&
                       [&] { QPainterPath r; r.addRect(d->selection().boundingRect()); return r.subtracted(d->selection()).isEmpty(); }();
    QVector<Layer> layers = d->layers();
    for (Layer &l : layers) {
        QImage n = l.image.copy(b);
        if (!rectangular) {
            QImage masked(n.size(), QImage::Format_ARGB32_Premultiplied);
            masked.fill(Qt::transparent);
            QPainter p(&masked);
            p.setRenderHint(QPainter::Antialiasing);
            p.setClipPath(sel);
            p.drawImage(0, 0, n);
            p.end();
            n = masked;
        }
        l.image = n;
    }
    d->clearSelection();
    d->setLayers(layers, b.size(), d->activeIndex());
    d->pushHistory(tr("Crop to Selection"), Icons::get("crop"));
}

void MainWindow::imageResize()
{
    Document *d = document();
    if (!d) return;
    commitTool();
    ResizeDialog dlg(d->size(), d->dpi, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QSize ns = dlg.imageSize();
    d->dpi = dlg.dpi();
    if (ns == d->size())
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QVector<Layer> layers = d->layers();
    for (Layer &l : layers)
        l.image = l.image.scaled(ns, Qt::IgnoreAspectRatio, dlg.mode()).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    d->clearSelection();
    d->setLayers(layers, ns, d->activeIndex());
    d->pushHistory(tr("Resize Image"), Icons::get("resize"));
    QApplication::restoreOverrideCursor();
}

void MainWindow::imageCanvasSize()
{
    Document *d = document();
    if (!d) return;
    commitTool();
    CanvasSizeDialog dlg(d->size(), d->dpi, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QSize ns = dlg.imageSize();
    if (ns == d->size())
        return;
    Qt::Alignment a = dlg.anchor();
    int dx = (a & Qt::AlignLeft) ? 0 : (a & Qt::AlignRight) ? ns.width() - d->width() : (ns.width() - d->width()) / 2;
    int dy = (a & Qt::AlignTop) ? 0 : (a & Qt::AlignBottom) ? ns.height() - d->height() : (ns.height() - d->height()) / 2;
    QVector<Layer> layers = d->layers();
    for (int i = 0; i < layers.size(); ++i) {
        QImage n(ns, QImage::Format_ARGB32_Premultiplied);
        n.fill(i == 0 ? m_colors.secondary : QColor(Qt::transparent));
        QPainter p(&n);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(dx, dy, layers[i].image);
        p.end();
        layers[i].image = n;
    }
    QPainterPath sel = d->hasSelection() ? d->selection().translated(dx, dy) : QPainterPath();
    d->setLayers(layers, ns, d->activeIndex());
    if (sel.isEmpty()) d->clearSelection(); else d->setSelection(sel);
    d->pushHistory(tr("Resize Canvas"), Icons::get("canvassize"));
}

void MainWindow::transformAllLayers(const QTransform &t, const QSize &ns, const QString &name, const QString &icon)
{
    Document *d = document();
    if (!d) return;
    commitTool();
    QVector<Layer> layers = d->layers();
    for (Layer &l : layers)
        l.image = l.image.transformed(t).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainterPath sel;
    if (d->hasSelection()) {
        // map selection with the same transform, re-anchored to the new origin
        QRectF src(d->rect());
        QRectF mapped = t.mapRect(src);
        QTransform t2 = t * QTransform::fromTranslate(-mapped.left(), -mapped.top());
        sel = t2.map(d->selection());
    }
    d->setLayers(layers, ns, d->activeIndex());
    if (sel.isEmpty()) d->clearSelection(); else d->setSelection(sel);
    d->pushHistory(name, Icons::get(icon));
}

void MainWindow::imageFlipH()
{
    if (!document()) return;
    transformAllLayers(QTransform().scale(-1, 1), document()->size(), tr("Flip Image Horizontal"), "fliph");
}
void MainWindow::imageFlipV()
{
    if (!document()) return;
    transformAllLayers(QTransform().scale(1, -1), document()->size(), tr("Flip Image Vertical"), "flipv");
}
void MainWindow::imageRotateCW()
{
    if (!document()) return;
    transformAllLayers(QTransform().rotate(90), document()->size().transposed(), tr("Rotate 90° Clockwise"), "rotatecw");
}
void MainWindow::imageRotateCCW()
{
    if (!document()) return;
    transformAllLayers(QTransform().rotate(-90), document()->size().transposed(), tr("Rotate 90° Counter-clockwise"), "rotateccw");
}
void MainWindow::imageRotate180()
{
    if (!document()) return;
    transformAllLayers(QTransform().rotate(180), document()->size(), tr("Rotate 180°"), "rotate180");
}

void MainWindow::imageFlatten()
{
    Document *d = document();
    if (!d || d->layerCount() < 2) return;
    commitTool();
    Layer l;
    l.image = d->flattened();
    l.name = d->layer(0).name;
    d->setLayers({l}, d->size(), 0);
    d->pushHistory(tr("Flatten"), Icons::get("flatten"));
}

// ============================================================ layers

void MainWindow::layerAdd()
{
    Document *d = document();
    if (!d) return;
    commitTool();
    QVector<Layer> layers = d->layers();
    Layer l;
    l.image = d->blankLayerImage();
    l.name = d->nextLayerName();
    layers.insert(d->activeIndex() + 1, l);
    d->setLayers(layers, d->size(), d->activeIndex() + 1);
    d->pushHistory(tr("Add New Layer"), Icons::get("layeradd"));
}

void MainWindow::layerDelete()
{
    Document *d = document();
    if (!d || d->layerCount() < 2) return;
    commitTool();
    QVector<Layer> layers = d->layers();
    int i = d->activeIndex();
    layers.remove(i);
    d->setLayers(layers, d->size(), std::max(0, i - 1));
    d->pushHistory(tr("Delete Layer"), Icons::get("layerdelete"));
}

void MainWindow::layerDuplicate()
{
    Document *d = document();
    if (!d) return;
    commitTool();
    QVector<Layer> layers = d->layers();
    int i = d->activeIndex();
    Layer l = layers[i];
    l.name = tr("%1 copy").arg(l.name);
    layers.insert(i + 1, l);
    d->setLayers(layers, d->size(), i + 1);
    d->pushHistory(tr("Duplicate Layer"), Icons::get("layerduplicate"));
}

void MainWindow::layerMergeDown()
{
    Document *d = document();
    if (!d || d->activeIndex() < 1) return;
    commitTool();
    QVector<Layer> layers = d->layers();
    int i = d->activeIndex();
    blendLayer(layers[i - 1].image, layers[i], d->rect());
    layers.remove(i);
    d->setLayers(layers, d->size(), i - 1);
    d->pushHistory(tr("Merge Layer Down"), Icons::get("layermerge"));
}

void MainWindow::layerImport()
{
    Document *d = document();
    if (!d) return;
    QString fn = QFileDialog::getOpenFileName(this, tr("Import From File"), m_lastDir, openFilter());
    if (fn.isEmpty())
        return;
    QImageReader reader(fn);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        QMessageBox::critical(this, kAppName, tr("Could not open %1").arg(fn));
        return;
    }
    commitTool();
    QVector<Layer> layers = d->layers();
    Layer l;
    l.image = d->blankLayerImage();
    {
        QPainter p(&l.image);
        p.drawImage(0, 0, img);
    }
    l.name = QFileInfo(fn).completeBaseName();
    layers.insert(d->activeIndex() + 1, l);
    d->setLayers(layers, d->size(), d->activeIndex() + 1);
    d->pushHistory(tr("Import From File"), Icons::get("importlayer"));
}

void MainWindow::layerFlipH()
{
    Document *d = document();
    if (!d) return;
    commitTool();
    d->activeImage() = d->activeImage().flipped(Qt::Horizontal);
    d->invalidate();
    d->notifyLayersChanged();
    d->pushHistory(tr("Flip Layer Horizontal"), Icons::get("fliph"));
}

void MainWindow::layerFlipV()
{
    Document *d = document();
    if (!d) return;
    commitTool();
    d->activeImage() = d->activeImage().flipped(Qt::Vertical);
    d->invalidate();
    d->notifyLayersChanged();
    d->pushHistory(tr("Flip Layer Vertical"), Icons::get("flipv"));
}

void MainWindow::layerRotateZoom()
{
    Document *d = document();
    if (!d) return;
    commitTool();
    RotateZoomDialog dlg(d, this);
    if (dlg.exec() == QDialog::Accepted)
        d->pushHistory(tr("Rotate / Zoom Layer"), Icons::get("rotatezoom"));
}

void MainWindow::layerCreateNormalMap()
{
    Document *d = document();
    if (!d) return;
    commitTool();
    const EffectDef *def = nullptr;
    for (const EffectDef &e : effectRegistry())
        if (e.name == "Normal Map")
            def = &e;
    if (!def)
        return;
    const QVector<Layer> before = d->layers();
    const int active = d->activeIndex();
    // height source: the visible image, so painted detail on any layer contributes
    QVector<Layer> layers = before;
    Layer l;
    l.image = d->flattened();
    l.name = "normal";
    layers.insert(active + 1, l);
    d->setLayers(layers, d->size(), active + 1);
    EffectDialog dlg(this, d, *def, m_effectValues.value(def->name), this);
    dlg.setWindowTitle(tr("Create Normal Map Layer"));
    if (dlg.exec() != QDialog::Accepted) {
        d->setLayers(before, d->size(), active);
        return;
    }
    m_effectValues[def->name] = dlg.values();
    d->pushHistory(tr("Create Normal Map Layer"), Icons::get("normalmap"));
}

void MainWindow::showLayerProperties()
{
    Document *d = document();
    if (!d) return;
    commitTool();
    LayerPropertiesDialog dlg(d, this);
    if (dlg.exec() == QDialog::Accepted && dlg.changed())
        d->pushHistory(tr("Layer Properties"), Icons::get("layerprops"));
}

void MainWindow::layerMoveUp()
{
    Document *d = document();
    if (!d || d->activeIndex() >= d->layerCount() - 1) return;
    commitTool();
    QVector<Layer> layers = d->layers();
    int i = d->activeIndex();
    std::swap(layers[i], layers[i + 1]);
    d->setLayers(layers, d->size(), i + 1);
    d->pushHistory(tr("Move Layer Up"), Icons::get("layerup"));
}

void MainWindow::layerMoveDown()
{
    Document *d = document();
    if (!d || d->activeIndex() < 1) return;
    commitTool();
    QVector<Layer> layers = d->layers();
    int i = d->activeIndex();
    std::swap(layers[i], layers[i - 1]);
    d->setLayers(layers, d->size(), i - 1);
    d->pushHistory(tr("Move Layer Down"), Icons::get("layerdown"));
}

// ============================================================ effects

void MainWindow::runEffectDef(const EffectDef &def, bool repeat)
{
    Document *d = document();
    if (!d) return;
    commitTool();
    QVariantMap values = m_effectValues.value(def.name);
    if (!def.params.isEmpty() && !repeat) {
        EffectDialog dlg(this, d, def, values, this);
        if (dlg.exec() != QDialog::Accepted)
            return;
        values = dlg.values();
    } else {
        for (const EffectParam &p : def.params)
            if (!values.contains(p.key))
                values[p.key] = p.type == EffectParam::Curve ? QVariant(QVariantList{QPointF(0, 0), QPointF(255, 255)}) : QVariant(p.def);
        values["_primary"] = m_colors.primary;
        values["_secondary"] = m_colors.secondary;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        runEffect(d, def, values, d->activeImage());
        QApplication::restoreOverrideCursor();
    }
    m_effectValues[def.name] = values;
    if (!def.adjustment)
        m_lastEffect = &def;
    d->pushHistory(def.name, Icons::get(def.icon));
    updateActions();
}

void MainWindow::repeatEffect()
{
    if (m_lastEffect)
        runEffectDef(*m_lastEffect, true);
}

// ============================================================ help

void MainWindow::showHelp()
{
    QString html = tr(
        "<h3>Keyboard shortcuts</h3>"
        "<table cellspacing=4>"
        "<tr><td><b>S</b></td><td>Rectangle Select / Lasso / Ellipse Select / Magic Wand</td></tr>"
        "<tr><td><b>M</b></td><td>Move Selected Pixels / Move Selection</td></tr>"
        "<tr><td><b>Z</b></td><td>Zoom</td></tr><tr><td><b>H</b></td><td>Pan (or hold Space)</td></tr>"
        "<tr><td><b>F</b></td><td>Paint Bucket</td></tr><tr><td><b>G</b></td><td>Gradient</td></tr>"
        "<tr><td><b>B</b></td><td>Paintbrush</td></tr><tr><td><b>E</b></td><td>Eraser</td></tr>"
        "<tr><td><b>P</b></td><td>Pencil</td></tr><tr><td><b>K</b></td><td>Color Picker</td></tr>"
        "<tr><td><b>L</b></td><td>Clone Stamp</td></tr><tr><td><b>R</b></td><td>Recolor</td></tr>"
        "<tr><td><b>T</b></td><td>Text</td></tr><tr><td><b>O</b></td><td>Line/Curve and shapes</td></tr>"
        "<tr><td><b>X</b></td><td>Swap primary/secondary colors</td></tr>"
        "<tr><td><b>Shift+letter</b></td><td>Cycle tools backwards</td></tr>"
        "<tr><td><b>F5–F8</b></td><td>Toggle Tools, History, Layers, Colors windows</td></tr>"
        "<tr><td><b>Ctrl+Wheel</b></td><td>Zoom around the mouse pointer</td></tr>"
        "<tr><td><b>Middle drag</b></td><td>Pan</td></tr>"
        "</table>"
        "<h3>Selections</h3><p>Ctrl + drag adds, right drag subtracts, Ctrl + right drag inverts (xor), "
        "Alt + drag intersects.</p>"
        "<h3>Move Selected Pixels</h3><p>Drag to move, drag handles to scale (Shift keeps proportions), "
        "right-drag to rotate (Shift snaps to 15°). Arrow keys nudge. Switching tools finishes the move.</p>");
    QMessageBox box(this);
    box.setWindowTitle(tr("Help"));
    box.setTextFormat(Qt::RichText);
    box.setText(html);
    box.exec();
}

void MainWindow::addPluginAction(const QString &menu, QAction *action)
{
    QMenu *m = m_menus.value(menu, m_menus.value("Window"));
    if (menu == "Help")
        m->insertAction(m_actions["about"], action);
    else
        m->addAction(action);
    addAction(action);
}

void MainWindow::showPlugins()
{
    QString html = tr("<h3>Plugins</h3>");
    if (m_pluginHost->plugins().isEmpty())
        html += tr("<p>No plugins installed.</p>");
    for (const LoadedPlugin &p : m_pluginHost->plugins()) {
        if (p.error.isEmpty())
            html += QString("<p><b>%1</b> %2<br>%3<br><small>%4</small></p>")
                        .arg(p.name.toHtmlEscaped(), p.version.toHtmlEscaped(), p.description.toHtmlEscaped(),
                             p.file.toHtmlEscaped());
        else
            html += QString("<p><b>%1</b><br><span style='color:#b00'>%2</span></p>")
                        .arg(p.file.toHtmlEscaped(), p.error.toHtmlEscaped());
    }
    html += tr("<p>Search paths:<br><small>%1</small></p>").arg(PluginHost::searchPaths().join("<br>").toHtmlEscaped());
    QMessageBox box(this);
    box.setWindowTitle(tr("Plugins"));
    box.setTextFormat(Qt::RichText);
    box.setText(html);
    box.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About PixelForge"),
                       tr("<h2>PixelForge 1.1</h2>"
                          "<p>A free image editor for Linux with a classic, familiar workflow: "
                          "floating tool windows, layers with blend modes, unlimited history, "
                          "adjustments and effects.</p>"
                          "<p>Layered documents are saved as OpenRaster (.ora), an open format "
                          "shared with GIMP, Krita and MyPaint.</p>"
                          "<p>Built with Qt %1.</p>").arg(qVersion()));
}
