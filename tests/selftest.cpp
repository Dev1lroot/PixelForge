// Headless end-to-end test: drives the real tools with synthetic input.
// Run with QT_QPA_PLATFORM=offscreen ./pixelforge_selftest [screenshot-dir]
#include "mainwindow.h"
#include "document.h"
#include "canvas.h"
#include "effects.h"
#include "ora.h"
#include "icons.h"
#include "animpanel.h"

#include <QApplication>
#include <QAction>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QKeyEvent>
#include <QElapsedTimer>
#include <QDir>
#include <QPushButton>
#include <QMenuBar>
#include <QMenu>
#include <QComboBox>
#include <QLabel>
#include <QPainter>
#include <cstdio>

static int g_fail = 0, g_pass = 0;
static QString g_shotDir;
static MainWindow *g_win;

#define CHECK(cond, msg)                                                         \
    do {                                                                         \
        if (cond) { ++g_pass; std::printf("  PASS  %s\n", msg); }                \
        else { ++g_fail; std::printf("  FAIL  %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

static void pump() { for (int i = 0; i < 3; ++i) QApplication::processEvents(); }

static void mouse(QEvent::Type t, const QPointF &img, Qt::MouseButton b, Qt::MouseButtons bs, Qt::KeyboardModifiers m = {})
{
    CanvasView *v = g_win->view();
    QPointF vp = v->imageToView(img);
    QMouseEvent e(t, vp, v->viewport()->mapToGlobal(vp), b, bs, m);
    QApplication::sendEvent(v->viewport(), &e);
}

static void drag(QPointF a, QPointF b, Qt::MouseButton btn = Qt::LeftButton, Qt::KeyboardModifiers m = {}, int steps = 12)
{
    mouse(QEvent::MouseButtonPress, a, btn, btn, m);
    for (int i = 1; i <= steps; ++i)
        mouse(QEvent::MouseMove, a + (b - a) * (double(i) / steps), Qt::NoButton, btn, m);
    mouse(QEvent::MouseButtonRelease, b, btn, Qt::NoButton, m);
    pump();
}

static void click(QPointF a, Qt::MouseButton btn = Qt::LeftButton, Qt::KeyboardModifiers m = {})
{
    mouse(QEvent::MouseButtonPress, a, btn, btn, m);
    mouse(QEvent::MouseButtonRelease, a, btn, Qt::NoButton, m);
    pump();
}

static void key(int k, const QString &text = QString(), Qt::KeyboardModifiers m = {})
{
    QWidget *target = g_win->view()->viewport();
    QKeyEvent so(QEvent::ShortcutOverride, k, m, text);
    QApplication::sendEvent(target, &so);
    if (so.isAccepted() || true) {
        QKeyEvent e(QEvent::KeyPress, k, m, text);
        QApplication::sendEvent(target, &e);
    }
    pump();
}

static QColor px(int x, int y) { return QColor::fromRgba(g_win->document()->activeImage().pixel(x, y)); }
static QColor comp(int x, int y) { return QColor::fromRgba(g_win->document()->composite().pixel(x, y)); }
static bool near(const QColor &a, const QColor &b, int tol = 12)
{
    return std::abs(a.red() - b.red()) <= tol && std::abs(a.green() - b.green()) <= tol &&
           std::abs(a.blue() - b.blue()) <= tol && std::abs(a.alpha() - b.alpha()) <= tol;
}

static void shot(const char *name)
{
    if (g_shotDir.isEmpty())
        return;
    pump();
    g_win->grab().save(g_shotDir + "/" + name + ".png");
}

// Plugins kept outside this repository can add tests: their CMakeLists.txt defines
// PIXELFORGE_LOCAL_TESTS as a file that implements localPluginTests().
#ifdef PIXELFORGE_LOCAL_TESTS
#include PIXELFORGE_LOCAL_TESTS
#endif

int main(int argc, char **argv)
{
    // keep the user's real settings untouched
    qputenv("XDG_CONFIG_HOME", QDir::temp().filePath("pixelforge-selftest-config").toUtf8());
    QApplication app(argc, argv);
    if (argc > 1)
        g_shotDir = argv[1];
    MainWindow w;
    g_win = &w;
    w.resize(1100, 760);
    auto *doc = new Document(QSize(400, 300), Qt::white);
    doc->resetHistory("New Image", Icons::get("new"));
    w.addDocument(doc, false);
    w.show();
    pump();
    w.view()->setZoom(1.0);
    pump();
    Document *d = w.document();

    std::printf("[brush]\n");
    w.colors().setPrimary(Qt::red);
    w.settings().brushWidth = 6;
    w.setActiveTool(ToolId::Brush);
    drag({50, 50}, {150, 50});
    CHECK(near(px(100, 50), Qt::red), "brush paints primary colour");
    CHECK(d->history().size() == 2 && d->history().last().name == "Paintbrush", "brush stroke adds one history item");
    w.action("undo")->trigger();
    pump();
    CHECK(near(px(100, 50), Qt::white), "undo restores pixels");
    w.action("redo")->trigger();
    pump();
    CHECK(near(px(100, 50), Qt::red), "redo re-applies stroke");
    drag({50, 80}, {150, 80}, Qt::RightButton);
    CHECK(near(px(100, 80), Qt::white), "right button paints secondary colour (white)");
    w.action("undo")->trigger();

    std::printf("[selection + move]\n");
    w.setActiveTool(ToolId::RectSelect);
    drag({20, 20}, {120, 120});
    CHECK(d->hasSelection() && d->selectionBounds() == QRect(20, 20, 100, 100), "rectangle select 100x100");
    drag({40, 40}, {60, 60}, Qt::LeftButton, Qt::ControlModifier);
    CHECK(d->selectionBounds() == QRect(20, 20, 100, 100), "ctrl-drag unions inside existing selection");
    drag({100, 100}, {140, 140}, Qt::LeftButton, Qt::ControlModifier);
    CHECK(d->selectionBounds() == QRect(20, 20, 120, 120), "ctrl-drag union grows selection");
    w.action("undo")->trigger();
    w.action("undo")->trigger();
    pump();
    CHECK(d->selectionBounds() == QRect(20, 20, 100, 100), "undo restores selection");
    w.setActiveTool(ToolId::Move);
    drag({70, 70}, {120, 100});
    CHECK(near(px(150, 80), Qt::red), "move selected pixels moves stroke by (+50,+30)");
    CHECK(px(60, 50).alpha() == 0, "lifted area leaves transparent hole");
    shot("01-move");
    w.setActiveTool(ToolId::Brush);
    CHECK(d->history().last().name == "Finish Pixels", "switching tool finishes pixels");
    // rotate with right drag
    w.setActiveTool(ToolId::Move);
    QRectF sb = d->selection().boundingRect();
    drag(sb.center() + QPointF(60, 0), sb.center() + QPointF(0, 60), Qt::RightButton);
    QRectF rb = d->selection().boundingRect();
    CHECK(std::abs(rb.width() - sb.width()) < 2 && std::abs(rb.center().x() - sb.center().x()) < 2, "right-drag rotates around centre (90deg keeps square)");
    w.action("undo")->trigger();
    pump();
    CHECK(near(px(150, 80), Qt::red) && d->history().last().name == "Move Selected Pixels",
          "undo while moving reverts the last move step (no extra Finish item)");
    w.setActiveTool(ToolId::Brush);

    std::printf("[layers]\n");
    w.action("deselect")->trigger();
    w.action("layeradd")->trigger();
    pump();
    CHECK(d->layerCount() == 2 && d->activeIndex() == 1, "add new layer becomes active");
    w.colors().setPrimary(QColor(0, 0, 255));
    w.setActiveTool(ToolId::Bucket);
    click({200, 200});
    CHECK(near(px(10, 10), QColor(0, 0, 255)), "paint bucket fills transparent layer");
    d->layer(1).opacity = 128;
    d->notifyStructureChanged();
    QColor c = comp(300, 250);
    CHECK(c.blue() > 200 && c.red() > 100 && c.red() < 160, "layer opacity 50% blends over background");
    d->layer(1).blend = BlendMode::Multiply;
    d->layer(1).opacity = 255;
    d->notifyStructureChanged();
    std::printf("    multiply: (300,250)=%s (150,80)=%s base=%s\n", qPrintable(comp(300, 250).name(QColor::HexArgb)),
                qPrintable(comp(150, 80).name(QColor::HexArgb)), qPrintable(QColor::fromRgba(d->layer(0).image.pixel(150, 80)).name(QColor::HexArgb)));
    CHECK(near(comp(300, 250), QColor(0, 0, 255)) && near(comp(150, 80), QColor(0, 0, 0), 20), "multiply blend mode");
    d->layer(1).blend = BlendMode::Negation;
    d->notifyStructureChanged();
    CHECK(comp(300, 250).isValid(), "custom blend mode composites");
    d->layer(1).blend = BlendMode::Normal;
    d->notifyStructureChanged();
    w.action("layerdup")->trigger();
    CHECK(d->layerCount() == 3 && d->layer(2).name.endsWith("copy"), "duplicate layer");
    w.action("layermerge")->trigger();
    CHECK(d->layerCount() == 2, "merge layer down");
    w.action("layerdown")->trigger();
    CHECK(d->activeIndex() == 0 && d->layer(0).name != "Background", "move layer down");
    w.action("layerup")->trigger();
    w.action("layerdelete")->trigger();
    pump();
    CHECK(d->layerCount() == 1, "delete layer");

    std::printf("[gradient]\n");
    w.colors().setPrimary(Qt::black);
    w.colors().setSecondary(Qt::white);
    w.setActiveTool(ToolId::Gradient);
    drag({0, 150}, {400, 150});
    CHECK(px(2, 10).red() < 20 && px(397, 10).red() > 235 && std::abs(px(200, 10).red() - 128) < 15, "linear gradient black to white");
    w.settings().gradientType = GradientType::Radial;
    w.settings().notify();
    CHECK(px(2, 150).red() < 20 && px(2, 10).red() > 70, "gradient re-renders live when type changes (radial)");
    shot("02-gradient");
    w.setActiveTool(ToolId::Brush);

    std::printf("[shapes, line, text]\n");
    w.colors().setPrimary(QColor(200, 0, 0));
    w.colors().setSecondary(QColor(0, 200, 0));
    w.settings().shapeType = ShapeType::FilledOutline;
    w.settings().brushWidth = 4;
    w.setActiveTool(ToolId::Rectangle);
    drag({50, 50}, {150, 120});
    CHECK(near(px(100, 85), QColor(0, 200, 0)) && near(px(50, 85), QColor(200, 0, 0), 30), "rectangle: outline primary, fill secondary");
    w.setActiveTool(ToolId::Ellipse);
    drag({200, 50}, {300, 150}, Qt::LeftButton, Qt::ShiftModifier);
    CHECK(near(px(250, 100), QColor(0, 200, 0)), "ellipse with shift");
    w.setActiveTool(ToolId::Line);
    drag({20, 200}, {380, 200});
    CHECK(near(px(200, 200), QColor(200, 0, 0)), "line drawn");
    // bend the curve by dragging on it
    drag({200, 200}, {200, 260});
    CHECK(!near(px(200, 200), QColor(200, 0, 0)) || px(200, 240) != px(200, 200), "dragging the line bends it");
    key(Qt::Key_Return);
    CHECK(!w.tool(ToolId::Line)->isEditing(), "enter finishes line");
    w.setActiveTool(ToolId::Text);
    click({30, 230});
    for (QChar ch : QString("Hello"))
        key(ch.unicode(), QString(ch));
    CHECK(w.tool(ToolId::Text)->isEditing(), "text tool editing");
    key(Qt::Key_Backspace);
    key(Qt::Key_Escape);
    CHECK(d->history().last().name == "Text", "escape commits text");
    shot("03-shapes");
    int before = d->history().size();
    key(Qt::Key_B, "b");
    CHECK(w.currentTool()->id() == ToolId::Brush, "B selects paintbrush");
    key(Qt::Key_S, "s");
    key(Qt::Key_S, "s");
    CHECK(w.currentTool()->id() == ToolId::Lasso, "S cycles selection tools");
    CHECK(d->history().size() == before, "tool shortcuts don't touch history");

    std::printf("[wand, picker, clone, recolor, eraser, pencil]\n");
    w.setActiveTool(ToolId::MagicWand);
    click({100, 85});
    CHECK(d->hasSelection() && d->selectionBounds().width() > 80 && d->selectionBounds().width() < 110, "magic wand selects rectangle interior");
    w.action("deselect")->trigger();
    w.setActiveTool(ToolId::ColorPicker);
    click({100, 85});
    CHECK(near(w.colors().primary, QColor(0, 200, 0)), "color picker sets primary");
    w.setActiveTool(ToolId::CloneStamp);
    w.settings().brushWidth = 10;
    click({100, 85}, Qt::LeftButton, Qt::ControlModifier);
    drag({350, 250}, {360, 260});
    CHECK(near(px(355, 255), QColor(0, 200, 0)), "clone stamp copies from source");
    w.colors().setPrimary(QColor(0, 0, 255));
    w.colors().setSecondary(QColor(0, 200, 0));
    w.setActiveTool(ToolId::Recolor);
    drag({90, 85}, {110, 85});
    CHECK(near(px(100, 85), QColor(0, 0, 255), 20), "recolor replaces secondary with primary");
    w.setActiveTool(ToolId::Eraser);
    drag({90, 100}, {110, 100});
    CHECK(px(100, 100).alpha() == 0, "eraser makes transparent");
    w.setActiveTool(ToolId::Pencil);
    drag({10, 290}, {60, 290});
    CHECK(near(px(30, 290), QColor(0, 0, 255)), "pencil draws aliased 1px");

    std::printf("[image ops]\n");
    w.action("rotcw")->trigger();
    CHECK(d->size() == QSize(300, 400), "rotate 90 swaps size");
    w.action("undo")->trigger();
    CHECK(d->size() == QSize(400, 300), "undo rotate");
    w.action("fliph")->trigger();
    w.action("undo")->trigger();
    w.setActiveTool(ToolId::RectSelect);
    drag({10, 10}, {210, 110});
    w.action("crop")->trigger();
    CHECK(d->size() == QSize(200, 100), "crop to selection");
    w.action("undo")->trigger();
    w.action("selectall")->trigger();
    w.action("copy")->trigger();
    w.action("deselect")->trigger();
    w.action("pastelayer")->trigger();
    pump();
    CHECK(d->layerCount() == 2 && w.currentTool()->id() == ToolId::Move, "paste into new layer activates move tool");
    w.setActiveTool(ToolId::Brush);
    w.action("flatten")->trigger();
    CHECK(d->layerCount() == 1, "flatten");

    std::printf("[effects]\n");
    for (const EffectDef &def : effectRegistry()) {
        QVariantMap p;
        for (const EffectParam &ep : def.params)
            p[ep.key] = ep.type == EffectParam::Curve ? QVariant(QVariantList{QPointF(0, 0), QPointF(128, 200), QPointF(255, 255)}) : QVariant(ep.def);
        p["_primary"] = QColor(Qt::black);
        p["_secondary"] = QColor(Qt::white);
        QImage backup = d->activeImage();
        QElapsedTimer t;
        t.start();
        runEffect(d, def, p, backup);
        qint64 ms = t.elapsed();
        bool changed = d->activeImage() != backup;
        d->activeImage() = backup;
        char buf[200];
        std::snprintf(buf, sizeof buf, "%-22s %5lld ms%s", def.name.toUtf8().constData(), (long long)ms, changed ? "" : "  (no change)");
        CHECK(true, buf);
    }
    d->invalidate();
    w.action("fx:Invert Colors")->trigger();
    CHECK(d->history().last().name == "Invert Colors", "adjustment from menu adds history");

    std::printf("[openraster round trip]\n");
    w.action("layeradd")->trigger();
    d->layer(1).blend = BlendMode::Screen;
    d->layer(1).opacity = 100;
    d->layer(1).image.fill(QColor(10, 20, 30, 200));
    QString path = QDir::temp().filePath("pixelforge_selftest.ora");
    CHECK(Ora::save(path, d->layers(), d->size(), d->dpi, d->flattened()), "save .ora");
    QVector<Layer> layers;
    QSize size;
    double dpi = 0;
    CHECK(Ora::load(path, layers, size, dpi), "load .ora");
    CHECK(layers.size() == 2 && size == d->size() && layers[1].blend == BlendMode::Screen &&
          std::abs(layers[1].opacity - 100) <= 1 && layers[0].image.pixel(5, 5) == d->layer(0).image.pixel(5, 5),
          "ora preserves layers, blend, opacity, pixels");

    std::printf("[history]\n");
    int n = d->history().size();
    d->jumpTo(0);
    CHECK(d->layerCount() == 1 && near(px(100, 50), Qt::white), "jump to first history state");
    d->jumpTo(n - 1);
    CHECK(d->layerCount() == 2, "jump back to last state");
    shot("04-final");

    // visual extras
    if (!g_shotDir.isEmpty()) {
        w.action("rulers")->trigger();
        w.action("grid")->trigger();
        w.view()->setZoom(8.0);
        for (QPushButton *b : w.findChildren<QPushButton *>())
            if (b->text().startsWith("More"))
                b->click();
        w.setActiveTool(ToolId::MagicWand);
        pump();
        shot("05-rulers-grid-colors");
        QMenuBar *mb = w.findChild<QMenuBar *>();
        for (QAction *a : mb->actions()) {
            if (!a->menu() || !a->text().contains("ff"))
                continue;
            QMenu *m = a->menu();
            m->popup(QPoint(300, 40));
            pump();
            m->grab().save(g_shotDir + "/06-effects-menu.png");
            for (QAction *sa : m->actions())
                if (sa->menu() && sa->text().contains("Render")) {
                    sa->menu()->popup(QPoint(500, 60));
                    pump();
                    sa->menu()->grab().save(g_shotDir + "/07-render-menu.png");
                    sa->menu()->hide();
                }
            m->hide();
        }
        for (const EffectDef &def : effectRegistry())
            if (def.name == "Curves" || def.name == "Motion Blur") {
                EffectDialog dlg(&w, d, def, {}, &w);
                dlg.show();
                pump();
                dlg.grab().save(g_shotDir + "/08-dialog-" + def.name.left(5) + ".png");
                dlg.reject();
            }
    }

    std::printf("[tile view]\n");
    {
        auto *td = new Document(QSize(100, 80), Qt::white);
        td->resetHistory("New Image", Icons::get("new"));
        w.addDocument(td, false);
        pump();
        w.view()->setZoom(2.0);
        w.action("tileview")->trigger();
        pump();
        CHECK(w.view()->tiled() && w.settings().tileMode, "tile preview toggles on");
        QPointF v0 = w.view()->imageToView(QPointF(0, 0));
        QPointF v1 = w.view()->imageToView(QPointF(100, 0));
        CHECK(std::abs((v1.x() - v0.x()) - 200) < 0.5, "main tile keeps its size");
        w.colors().setPrimary(Qt::black);
        w.settings().brushWidth = 4;
        w.setActiveTool(ToolId::Brush);
        drag({90, 20}, {110, 20});
        CHECK(near(px(95, 20), Qt::black) && near(px(5, 20), Qt::black), "stroke across the right edge wraps to the left");
        drag({150, 40}, {160, 40});                  // entirely on the right-hand neighbour tile
        CHECK(near(px(55, 40), Qt::black), "painting on a neighbour tile lands in the canvas");
        {
            // a change must schedule repaints on the neighbour copies too, not only the centre tile
            struct Spy : QObject {
                QRegion painted;
                bool eventFilter(QObject *, QEvent *e) override
                {
                    if (e->type() == QEvent::Paint)
                        painted += static_cast<QPaintEvent *>(e)->region();
                    return false;
                }
            } spy;
            pump();
            w.view()->viewport()->installEventFilter(&spy);
            td->invalidate(QRect(40, 30, 4, 4));
            pump();
            w.view()->viewport()->removeEventFilter(&spy);
            QPointF centre = w.view()->imageToView(QPointF(42, 32));
            QPointF right = w.view()->imageToView(QPointF(142, 32));
            QPointF below = w.view()->imageToView(QPointF(42, 112));
            CHECK(spy.painted.contains(centre.toPoint()) && spy.painted.contains(right.toPoint()) &&
                      spy.painted.contains(below.toPoint()),
                  "pixel changes repaint all tile copies immediately");
        }
        w.colors().setPrimary(QColor(255, 0, 0));
        w.setActiveTool(ToolId::Bucket);
        click({130, 70});                            // neighbour tile -> wrapped to (30,70)
        CHECK(near(px(30, 70), QColor(255, 0, 0)), "bucket click on a neighbour tile fills the wrapped point");
        w.setActiveTool(ToolId::Ellipse);
        w.settings().shapeType = ShapeType::Filled;
        drag({-15, 30}, {15, 60});
        CHECK(near(px(2, 45), QColor(255, 0, 0)) && near(px(97, 45), QColor(255, 0, 0)), "shape across the left edge wraps");
        shot("09-tile-view");
        w.action("tileview")->trigger();
        CHECK(!w.view()->tiled(), "tile preview toggles off");
        w.setActiveTool(ToolId::Brush);
    }

    std::printf("[normal maps]\n");
    {
        const EffectDef *nm = nullptr, *flip = nullptr, *norm = nullptr;
        for (const EffectDef &e : effectRegistry()) {
            if (e.name == "Normal Map") nm = &e;
            if (e.name.startsWith("Flip Normal Green")) flip = &e;
            if (e.name == "Normalize Normal Map") norm = &e;
        }
        CHECK(nm && flip && norm, "maps effects registered");
        auto *nd = new Document(QSize(64, 64), Qt::white);
        nd->resetHistory("New Image", Icons::get("new"));
        w.addDocument(nd, false);
        QVariantMap p{{"strength", 4.0}, {"smooth", 0}, {"source", 0}, {"invert", false}, {"format", 0}, {"wrap", false}};
        runEffect(nd, *nm, p, nd->activeImage());
        QColor flat = px(32, 32);
        CHECK(std::abs(flat.red() - 128) <= 1 && std::abs(flat.green() - 128) <= 1 && flat.blue() >= 254, "flat height gives (128,128,255)");
        QImage ramp(64, 64, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                ramp.setPixel(x, y, qRgb(x * 4, x * 4, x * 4));
        runEffect(nd, *nm, p, ramp);
        CHECK(px(32, 32).red() < 110 && std::abs(px(32, 32).green() - 128) <= 2, "height rising to the right tilts normal left (red < 128)");
        QImage vramp(64, 64, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                vramp.setPixel(x, y, qRgb(y * 4, y * 4, y * 4));
        runEffect(nd, *nm, p, vramp);
        int glGreen = px(32, 32).green();
        p["format"] = 1;
        runEffect(nd, *nm, p, vramp);
        CHECK(glGreen > 140 && px(32, 32).green() < 116, "OpenGL vs DirectX green convention");
        runEffect(nd, *flip, {}, nd->activeImage());
        CHECK(std::abs(px(32, 32).green() - glGreen) <= 1, "flip green converts DirectX to OpenGL");
        p["format"] = 0;
        p["wrap"] = true;
        runEffect(nd, *nm, p, ramp);
        CHECK(px(0, 32).red() > 140, "seamless mode sees the wrap-around step at the edge");
    }

    std::printf("[animation panel]\n");
    {
        AnimationPanel *ap = w.animationPanel();
        auto *sd = new Document(QSize(16, 64), Qt::white);
        sd->resetHistory("New Image", Icons::get("new"));
        w.addDocument(sd, false);
        for (int f = 0; f < 4; ++f)
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    sd->activeImage().setPixel(x, f * 16 + y, qRgb(f * 60, 0, 0));
        sd->invalidate();
        ap->setMode(AnimationPanel::VerticalStrip);
        CHECK(ap->frameCount() == 4, "vertical strip: 16x64 auto-detects 4 square frames");
        CHECK(ap->frame(2).size() == QSize(16, 16) && qRed(ap->frame(2).pixel(5, 5)) == 120, "frame 3 is the third square");
        ap->setMode(AnimationPanel::ScrollVertical);
        CHECK(ap->frameCount() == 64 && qRed(ap->frame(16).pixel(0, 0)) == 60, "vertical scroll wraps by step");
        ap->setMode(AnimationPanel::LayerFrames);
        w.action("layeradd")->trigger();
        w.action("layeradd")->trigger();
        CHECK(ap->frameCount() == 3, "layers as frames counts layers");
        ap->setMode(AnimationPanel::HorizontalStrip);
        CHECK(ap->frameCount() == 1, "horizontal strip of a tall image has one frame");
        w.action("winanim")->setChecked(true);
        ap->setMode(AnimationPanel::VerticalStrip);
    }

#ifdef PIXELFORGE_HAVE_EXAMPLES
    std::printf("[example plugins]\n");
    {
        CHECK(w.findChild<QAction *>("example_hello") != nullptr, "hello plugin added its Help menu action");
        CHECK(w.findChild<QAction *>("example_exportlayers") != nullptr, "export layers plugin added its File menu action");
        QAction *hist = w.action("plugin_histogram");
        CHECK(hist != nullptr, "histogram plugin registered its window");
        if (hist) {
            auto *hd = new Document(QSize(10, 10), Qt::white);
            hd->resetHistory("New Image", Icons::get("new"));
            w.addDocument(hd, false);
            hist->setChecked(true);
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < 200)
                QApplication::processEvents();
            bool found = false;
            for (QLabel *l : w.findChildren<QLabel *>())
                found |= l->isVisible() && l->text() == "100 opaque pixels";
            CHECK(found, "histogram window counts the pixels of the current image");
            hist->setChecked(false);
        }
    }
#endif

#ifdef PIXELFORGE_LOCAL_TESTS
    localPluginTests(w);
#endif

    std::printf("[performance 4000x3000]\n");
    {
        auto *big = new Document(QSize(4000, 3000), Qt::white);
        big->resetHistory("New Image", Icons::get("new"));
        w.addDocument(big, false);
        pump();
        w.view()->setZoom(0.25);
        pump();
        auto timeIt = [](const char *label, auto fn) {
            QElapsedTimer t;
            t.start();
            fn();
            char buf[160];
            std::snprintf(buf, sizeof buf, "%-34s %6lld ms", label, (long long)t.elapsed());
            CHECK(true, buf);
        };
        w.settings().brushWidth = 40;
        w.setActiveTool(ToolId::Brush);
        timeIt("brush stroke (24 moves)", [] { drag({100, 100}, {3900, 2900}, Qt::LeftButton, {}, 24); });
        w.setActiveTool(ToolId::Rectangle);
        timeIt("rectangle drag (24 moves)", [] { drag({200, 200}, {3000, 2000}, Qt::LeftButton, {}, 24); });
        w.setActiveTool(ToolId::Gradient);
        timeIt("gradient drag (12 moves)", [] { drag({0, 0}, {4000, 3000}, Qt::LeftButton, {}, 12); });
        w.setActiveTool(ToolId::Brush);
        timeIt("repaint view at 25%", [&] { w.view()->viewport()->repaint(); });
        timeIt("repaint view at 25% (cached)", [&] { w.view()->viewport()->repaint(); });
        w.view()->setZoom(1.0);
        timeIt("repaint view at 100%", [&] { w.view()->viewport()->repaint(); });
        for (const EffectDef &def : effectRegistry())
            if (def.name == "Gaussian Blur" || def.name == "Sharpen" || def.name == "Hue / Saturation") {
                QVariantMap p;
                for (const EffectParam &ep : def.params)
                    p[ep.key] = ep.def;
                QImage backup = big->activeImage();
                timeIt(qPrintable(def.name), [&] { runEffect(big, def, p, backup); });
            }
        timeIt("undo", [&] { w.action("undo")->trigger(); });
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
