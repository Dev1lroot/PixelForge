// Headless end-to-end test: drives the real tools with synthetic input.
// Run with QT_QPA_PLATFORM=offscreen ./pixelforge_selftest [screenshot-dir]
#include "mainwindow.h"
#include "document.h"
#include "canvas.h"
#include "effects.h"
#include "ora.h"
#include "icons.h"
#include "animpanel.h"
#include "colorpanel.h"
#include "dialogs.h"

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
#include <QCheckBox>
#include <QSplitter>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QScrollBar>
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

#ifdef PIXELFORGE_HAVE_MODELVIEW
#include "meshrender.h"
#include "convert.h"
#include <QFile>
#include <QTextStream>

static void writeText(const QString &path, const QByteArray &text)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(text);
}

static void modelViewTests(MainWindow &w)
{
    using namespace ModelView;
    std::printf("[3d model preview]\n");
    QDir dir(QDir::temp().filePath("pixelforge-modelview-test"));
    dir.mkpath(".");
    // two unit quads side by side facing +z, one material each
    writeText(dir.filePath("quads.mtl"), "newmtl Left\nKd 1 1 1\nnewmtl Right\nKd 1 1 1\n");
    writeText(dir.filePath("quads.obj"),
              "mtllib quads.mtl\n"
              "v -2 -1 0\nv 0 -1 0\nv 0 1 0\nv -2 1 0\nv 0 -1 0\nv 2 -1 0\nv 2 1 0\nv 0 1 0\n"
              "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
              "usemtl Left\nf 1/1 2/2 3/3 4/4\nusemtl Right\nf -4/1 -3/2 -2/3 -1/4\n");
    QString err;
    Model m = loadModel(dir.filePath("quads.obj"), &err);
    CHECK(m.triangleCount() == 4 && m.materials.size() == 2 && m.parts.size() == 2 && m.hasUV, "OBJ: quads, materials and UVs");
    CHECK(m.uvSets.size() == 1 && m.uvData.size() == 1, "OBJ: one UV map");
    CHECK(!m.vertices.isEmpty() && m.vertices[0].normal.z() > 0.99f, "OBJ: missing normals are generated");

    QImage halves(8, 8, QImage::Format_ARGB32);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            halves.setPixel(x, y, y < 4 ? qRgb(0, 255, 0) : qRgb(255, 0, 0));
    QImage blue(8, 8, QImage::Format_ARGB32);
    blue.fill(qRgb(0, 0, 255));
    QVector<MaterialTextures> tex(2);
    tex[0].base = halves;
    tex[1].base = blue;
    RenderParams rp;
    rp.yaw = rp.pitch = 0;
    rp.lighting = false;
    QImage out(200, 160, QImage::Format_RGB32);
    renderModel(m, tex, rp, out);
    const QRgb upperLeft = out.pixel(70, 70), lowerLeft = out.pixel(70, 90), right = out.pixel(130, 80);
    CHECK(qGreen(upperLeft) > 200 && qRed(upperLeft) < 50, "texture top row maps to the top of the model (v up)");
    CHECK(qRed(lowerLeft) > 200 && qGreen(lowerLeft) < 50, "texture bottom row at the bottom");
    CHECK(qBlue(right) > 200 && qRed(right) < 50, "second material uses its own slot");
    rp.yaw = M_PI;
    rp.twoSided = false;
    renderModel(m, tex, rp, out);
    CHECK(qBlue(out.pixel(130, 80)) < 120 && qGreen(out.pixel(70, 70)) < 120, "back faces are culled when not two-sided");

    QByteArray buf;
    for (float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f})
        buf.append(reinterpret_cast<const char *>(&f), 4);
    writeText(dir.filePath("tri.gltf"),
              "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
              "\"nodes\":[{\"mesh\":0,\"translation\":[5,0,0]}],"
              "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":0}]}],"
              "\"materials\":[{\"name\":\"Skin\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0,0,1]}}],"
              "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}],"
              "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36}],"
              "\"buffers\":[{\"byteLength\":36,\"uri\":\"data:application/octet-stream;base64," + buf.toBase64() + "\"}]}");
    Model g = loadModel(dir.filePath("tri.gltf"), &err);
    CHECK(g.triangleCount() == 1 && !g.hasUV && g.materials.value(0).name == "Skin" && g.materials[0].color == QColor(255, 0, 0),
          "glTF: embedded buffer, material name and colour");
    CHECK(!g.vertices.isEmpty() && std::abs(g.vertices[1].pos.x() - 6) < 1e-4f, "glTF: node transforms are applied");

    writeText(dir.filePath("tri.stl"), "solid t\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\n"
                                       "endloop\nendfacet\nendsolid t\n");
    CHECK(loadModel(dir.filePath("tri.stl")).triangleCount() == 1, "STL (ASCII)");
    writeText(dir.filePath("quad.ply"), "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\n"
                                        "property float z\nproperty float s\nproperty float t\nelement face 1\n"
                                        "property list uchar int vertex_indices\nend_header\n"
                                        "0 0 0 0 0\n1 0 0 1 0\n1 1 0 1 1\n0 1 0 0 1\n4 0 1 2 3\n");
    Model ply = loadModel(dir.filePath("quad.ply"));
    CHECK(ply.triangleCount() == 2 && ply.hasUV, "PLY (ASCII) with texture coordinates");
    CHECK(loadModel(dir.filePath("missing.obj"), &err).isEmpty() && !err.isEmpty(), "missing file reports an error");

    // ASCII FBX 7: a quad with per-corner UVs and a material on a translated, Z-up model
    writeText(dir.filePath("quad.fbx"),
              "; FBX 7.4.0 project file\nFBXHeaderExtension:  {\n\tFBXVersion: 7400\n}\n"
              "GlobalSettings:  {\n\tProperties70:  {\n\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",2\n"
              "\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
              "\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n\t}\n}\n"
              "Objects:  {\n"
              "\tGeometry: 100, \"Geometry::Quad\", \"Mesh\" {\n"
              "\t\tVertices: *12 {\n\t\t\ta: 0,0,0,1,0,0,1,0,1,\n0,0,1\n\t\t}\n"
              "\t\tPolygonVertexIndex: *4 {\n\t\t\ta: 0,1,2,-4\n\t\t}\n"
              "\t\tLayerElementUV: 0 {\n\t\t\tName: \"Main\"\n\t\t\tMappingInformationType: \"ByPolygonVertex\"\n"
              "\t\t\tReferenceInformationType: \"IndexToDirect\"\n\t\t\tUV: *8 {\n\t\t\t\ta: 0,0,1,0,1,1,0,1\n\t\t\t}\n"
              "\t\t\tUVIndex: *4 {\n\t\t\t\ta: 0,1,2,3\n\t\t\t}\n\t\t}\n"
              "\t\tLayerElementUV: 1 {\n\t\t\tName: \"Lightmap\"\n\t\t\tMappingInformationType: \"ByPolygonVertex\"\n"
              "\t\t\tReferenceInformationType: \"Direct\"\n\t\t\tUV: *8 {\n\t\t\t\ta: 0,0,0.5,0,0.5,0.5,0,0.5\n\t\t\t}\n\t\t}\n"
              "\t\tLayerElementMaterial: 0 {\n\t\t\tMappingInformationType: \"AllSame\"\n"
              "\t\t\tReferenceInformationType: \"IndexToDirect\"\n\t\t\tMaterials: *1 {\n\t\t\t\ta: 0\n\t\t\t}\n\t\t}\n"
              "\t}\n"
              "\tModel: 200, \"Model::Quad\", \"Mesh\" {\n\t\tProperties70:  {\n"
              "\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",10,0,0\n\t\t}\n\t}\n"
              "\tMaterial: 300, \"Material::Wood\", \"\" {\n\t\tProperties70:  {\n"
              "\t\t\tP: \"DiffuseColor\", \"Color\", \"\", \"A\",0,1,0\n\t\t}\n\t}\n"
              "}\n"
              "Connections:  {\n\tC: \"OO\",100,200\n\tC: \"OO\",300,200\n\tC: \"OO\",200,0\n}\n");
    Model fbx = loadModel(dir.filePath("quad.fbx"), &err);
    CHECK(fbx.triangleCount() == 2 && fbx.hasUV && fbx.materials.value(0).name == "Wood" &&
              fbx.materials[0].color == QColor(0, 255, 0),
          "FBX (ASCII): polygons, UVs and connected material");
    float maxY = -1e9f, minX = 1e9f;
    for (const Vertex &v : fbx.vertices) {
        maxY = std::max(maxY, v.pos.y());
        minX = std::min(minX, v.pos.x());
    }
    CHECK(std::abs(maxY - 1) < 1e-4f && std::abs(minX - 10) < 1e-4f, "FBX: Z-up file converted to Y-up, model translation applied");
    CHECK(!fbx.vertices.isEmpty() && std::abs(fbx.vertices[0].normal.z()) > 0.99f, "FBX: normals generated when missing");
    CHECK(fbx.uvSets == QStringList({"Main", "Lightmap"}), "FBX: every UV layer becomes a named UV map");
    float maxU = 0;
    for (const Vertex &v : fbx.vertices)
        maxU = std::max(maxU, v.u);
    fbx.selectUVSet(1);
    float maxU2 = 0;
    for (const Vertex &v : fbx.vertices)
        maxU2 = std::max(maxU2, v.u);
    CHECK(std::abs(maxU - 1) < 1e-5f && std::abs(maxU2 - 0.5f) < 1e-5f && fbx.uvSet == 1, "selecting a UV map switches the vertex UVs");

    // converter middleware, driven by a stand-in "assimp" that copies a prepared GLB
    QByteArray json = "{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
                      "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}],"
                      "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36}],\"buffers\":[{\"byteLength\":36}]}";
    while (json.size() % 4)
        json += ' ';
    QByteArray glb = "glTF";
    auto u32 = [](QByteArray &b, quint32 v) { b.append(reinterpret_cast<const char *>(&v), 4); };
    u32(glb, 2);
    u32(glb, quint32(12 + 8 + json.size() + 8 + buf.size()));
    u32(glb, quint32(json.size()));
    glb += "JSON" + json;
    u32(glb, quint32(buf.size()));
    glb += QByteArray("BIN\0", 4) + buf;
    writeText(dir.filePath("tri.glb"), glb);
    CHECK(loadModel(dir.filePath("tri.glb")).triangleCount() == 1, "GLB container");
    const QString tool = dir.filePath("fake-assimp.sh");
    writeText(tool, ("#!/bin/sh\n[ -f \"" + dir.filePath("tool-broken") + "\" ] && { echo 'cannot read this'; exit 1; }\n"
                     "cp \"" + dir.filePath("tri.glb") + "\" \"$3\"\n").toUtf8());
    QFile::setPermissions(tool, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    qputenv("PIXELFORGE_ASSIMP", tool.toUtf8());
    QFile::remove(dir.filePath("tool-broken"));
    writeText(dir.filePath("thing.lwo"), "not really a LightWave file");
    QFile::remove(conversionCacheFile(dir.filePath("thing.lwo")));
    CHECK(convertibleSuffixes().contains("lwo"), "installed converters add their formats");
    const QString converted = Conversion::convert(dir.filePath("thing.lwo"), &err);
    CHECK(!converted.isEmpty() && loadModel(converted).triangleCount() == 1, "foreign format is converted to GLB and loads");
    writeText(dir.filePath("tool-broken"), "1");
    CHECK(Conversion::convert(dir.filePath("thing.lwo"), &err) == converted, "unchanged files are served from the conversion cache");
    writeText(dir.filePath("other.ms3d"), "x");
    QFile::remove(conversionCacheFile(dir.filePath("other.ms3d")));
    CHECK(Conversion::convert(dir.filePath("other.ms3d"), &err).isEmpty() && err.contains("cannot read this"),
          "converter failures report the tool's message");
    QFile::remove(dir.filePath("tool-broken"));

    // the plugin window: slots follow the document and the preview updates while painting
    QAction *act = w.action("plugin_modelview");
    CHECK(act != nullptr, "model preview plugin registered its window");
    QWidget *panel = w.findChild<QWidget *>("modelview_panel");
    if (!act || !panel)
        return;
    auto *md = new Document(QSize(8, 8), Qt::red);
    md->resetHistory("New Image", Icons::get("new"));
    w.addDocument(md, false);
    act->setChecked(true);
    bool opened = false;
    QMetaObject::invokeMethod(panel, "openModel", Q_RETURN_ARG(bool, opened), Q_ARG(QString, dir.filePath("quads.obj")), Q_ARG(bool, false));
    CHECK(opened, "panel opens the model");
    // the slot comboboxes (the UV map selector stays hidden for single-UV models)
    auto slotCombos = [panel] {
        QList<QComboBox *> out;
        for (QComboBox *c : panel->findChildren<QComboBox *>())
            if (c->isVisibleTo(panel))
                out << c;
        return out;
    };
    pump();                                           // new slot rows are shown by a queued event
    QList<QComboBox *> combos = slotCombos();
    CHECK(combos.size() == 4, "one colour and one normal slot per material");
    CHECK(combos.size() == 4 && combos[0]->currentText() == "(none)" && combos[2]->currentText() == "(none)" &&
              combos[0]->findText("Background") >= 0 && combos[0]->findText("(material color)") >= 0,
          "slots default to (none) and list the layers");
    QWidget *view = nullptr;
    for (QWidget *c : panel->findChildren<QWidget *>())
        if (c->inherits("ModelView::View"))
            view = c;
    auto centre = [view] {
        QImage img = view->grab().toImage();
        return QColor(img.pixel(img.width() / 2 - img.width() / 8, img.height() / 2));
    };
    auto wait = [] {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 250)
            QApplication::processEvents();
    };
    if (view && combos.size() == 4) {
        view->resize(240, 200);
        wait();
        const QColor hidden = centre();
        CHECK(std::abs(hidden.red() - hidden.blue()) < 30, "materials set to (none) are not drawn");
        combos[0]->setCurrentIndex(combos[0]->findText("(whole image)"));      // Left: whole image
        combos[2]->setCurrentIndex(combos[2]->findText("Background"));         // Right: the Background layer
        wait();
        QColor before = centre();
        CHECK(before.red() > before.blue() + 40, "preview shows the red image on the model");
        md->activeImage().fill(QColor(0, 0, 255));
        md->invalidate();
        wait();
        QColor after = centre();
        CHECK(after.blue() > after.red() + 40, "preview follows edits in real time");

        // UV layout on the canvas: the quads' diagonal edge runs through the image centre
        QCheckBox *uvs = nullptr;
        for (QCheckBox *c : panel->findChildren<QCheckBox *>())
            if (c->text() == "UVs on canvas")
                uvs = c;
        CHECK(uvs != nullptr && panel->findChild<QSplitter *>() != nullptr, "UV overlay toggle and resizable splitter exist");
        if (uvs) {
            w.view()->setZoom(24);
            auto canvasAt = [&w](QPointF img) {
                pump();
                const QImage g = w.view()->viewport()->grab().toImage();
                const QPoint v = (w.view()->imageToView(img) * g.devicePixelRatio()).toPoint();
                QColor best(g.pixel(v));                    // greenest pixel around: lines are 1px, antialiased
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        if (QColor c(g.pixel(v + QPoint(dx, dy))); c.green() > best.green())
                            best = c;
                return best;
            };
            uvs->setChecked(false);
            const QColor plain = canvasAt({4, 4});
            uvs->setChecked(true);
            const QColor lined = canvasAt({4, 4});
            CHECK(plain.green() < 40 && lined.green() > 70, "UV edges are drawn over the image");
            CHECK(canvasAt({2, 4}).green() < 40, "...and only along the UV edges");
            shot("11-uv-overlay");
            // both quads share the 0..1 UV square: hide Left, so only the layer-bound Right remains
            combos[0]->setCurrentIndex(combos[0]->findText("(none)"));
            CHECK(canvasAt({4, 4}).green() > 70, "UVs of a material bound to the active layer are shown");
            w.action("layeradd")->trigger();                                    // a new layer becomes active
            wait();
            CHECK(canvasAt({4, 4}).green() < 40, "...and hidden while another layer is being edited");
            md->setActiveIndex(0);
            wait();
            CHECK(canvasAt({4, 4}).green() > 70, "...and back when its layer is active again");
            combos[2]->setCurrentIndex(combos[2]->findText("(none)"));
            CHECK(canvasAt({4, 4}).green() < 40, "hidden materials show no UVs");
            combos[0]->setCurrentIndex(combos[0]->findText("(whole image)"));
            w.action("layeradd")->trigger();
            wait();
            CHECK(canvasAt({4, 4}).green() > 70, "whole-image materials show their UVs on every layer");
            uvs->setChecked(false);
            CHECK(canvasAt({4, 4}).green() < 40, "UV overlay toggles off");
        }
        shot("10-model-preview");
    }
    // UV Map flood mode: two separate islands, (0.1..0.4)^2 and (0.6..0.9)^2 in UV space
    writeText(dir.filePath("islands.obj"),
              "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 2 0 0\nv 3 0 0\nv 3 1 0\nv 2 1 0\n"
              "vt 0.1 0.1\nvt 0.4 0.1\nvt 0.4 0.4\nvt 0.1 0.4\nvt 0.6 0.6\nvt 0.9 0.6\nvt 0.9 0.9\nvt 0.6 0.9\n"
              "f 1/1 2/2 3/3 4/4\nf 5/5 6/6 7/7 8/8\n");
    {
        auto *ud = new Document(QSize(40, 40), Qt::white);
        ud->resetHistory("New Image", Icons::get("new"));
        w.addDocument(ud, false);
        w.view()->setZoom(4);
        pump();
        QMetaObject::invokeMethod(panel, "openModel", Q_RETURN_ARG(bool, opened), Q_ARG(QString, dir.filePath("islands.obj")), Q_ARG(bool, false));
        pump();
        QList<QComboBox *> c = slotCombos();
        w.settings().floodMode = FloodMode::UVMap;
        w.setActiveTool(ToolId::MagicWand);
        click({10, 30});
        CHECK(!ud->hasSelection(), "UV Map mode does nothing while no material uses the image");
        if (!c.isEmpty())
            c[0]->setCurrentIndex(c[0]->findText("(whole image)"));
        click({10, 30});                                   // u 0.25, v 0.25 -> inside the first island
        CHECK(ud->hasSelection() && ud->selectionBounds() == QRect(4, 24, 12, 12), "magic wand selects a whole UV island");
        w.action("deselect")->trigger();
        w.colors().setPrimary(QColor(255, 0, 0));
        w.setActiveTool(ToolId::Bucket);
        click({20, 20});                                   // between the islands
        auto at = [ud](int x, int y) { return QColor(ud->activeImage().pixel(x, y)); };
        CHECK(at(20, 20) == QColor(255, 0, 0) && at(0, 0) == QColor(255, 0, 0) && at(39, 39) == QColor(255, 0, 0),
              "bucket fills the gap space between UV islands");
        CHECK(at(10, 30) == Qt::white && at(30, 10) == Qt::white, "...without touching the islands");
        w.colors().setPrimary(QColor(0, 160, 0));
        click({30, 10});                                   // second island
        CHECK(at(30, 10) == QColor(0, 160, 0) && at(25, 5) == QColor(0, 160, 0) && at(10, 30) == Qt::white &&
                  at(20, 20) == QColor(255, 0, 0),
              "bucket fills exactly one UV island");
        w.settings().floodMode = FloodMode::Contiguous;
        w.setActiveTool(ToolId::Brush);
    }

    QFile::remove(conversionCacheFile(dir.filePath("thing.lwo")));
    QMetaObject::invokeMethod(panel, "openModel", Q_RETURN_ARG(bool, opened), Q_ARG(QString, dir.filePath("thing.lwo")), Q_ARG(bool, false));
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 5000 && slotCombos().size() != 2)
        QApplication::processEvents();
    CHECK(slotCombos().size() == 2, "panel converts a foreign format in the background and shows it");
    act->setChecked(false);
}
#endif

// Plugins kept outside this repository can add tests: their CMakeLists.txt defines
// PIXELFORGE_LOCAL_TESTS as a file that implements localPluginTests().
#ifdef PIXELFORGE_LOCAL_TESTS
#include PIXELFORGE_LOCAL_TESTS
#endif

int main(int argc, char **argv)
{
    // keep the user's real settings untouched
    qputenv("XDG_CONFIG_HOME", QDir::temp().filePath("pixelforge-selftest-config").toUtf8());
    qputenv("XDG_CACHE_HOME", QDir::temp().filePath("pixelforge-selftest-cache").toUtf8());
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
    w.settings().brushWidth = 20;
    w.settings().hardness = 0;
    drag({50, 200}, {150, 200});
    CHECK(near(px(100, 200), Qt::red, 20), "soft brush is fully opaque in the centre");
    CHECK(px(100, 205).green() > 60 && px(100, 205).green() < 200, "soft brush fades towards the rim");
    CHECK(std::abs(px(60, 205).green() - px(100, 205).green()) <= 3 && std::abs(px(140, 205).green() - px(100, 205).green()) <= 3,
          "overlapping segments of one soft stroke don't build up");
    CHECK(near(px(100, 211), Qt::white), "nothing outside the brush radius");
    w.action("undo")->trigger();
    w.settings().hardness = 100;

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
    for (const EffectDef &def : effectRegistry()) {
        if (def.name != "Black and Alpha")
            continue;
        QImage src(4, 1, QImage::Format_ARGB32), dst(4, 1, QImage::Format_ARGB32);
        src.setPixel(0, 0, qRgba(255, 255, 255, 255));
        src.setPixel(1, 0, qRgba(128, 128, 128, 255));
        src.setPixel(2, 0, qRgba(0, 0, 0, 255));
        src.setPixel(3, 0, qRgba(0, 0, 0, 100));
        def.apply(src, dst, src.rect(), {});
        CHECK(dst.pixel(0, 0) == qRgba(0, 0, 0, 0) && dst.pixel(1, 0) == qRgba(0, 0, 0, 127) &&
              dst.pixel(2, 0) == qRgba(0, 0, 0, 255) && dst.pixel(3, 0) == qRgba(0, 0, 0, 100),
              "Black and Alpha: white turns transparent, darkness becomes opacity of black");
    }

    std::printf("[colour wheel modifiers]\n");
    if (ColorWheel *wheel = w.findChild<ColorWheel *>()) {
        auto drag = [wheel](QPointF from, QPointF to, Qt::KeyboardModifiers m) {
            QMouseEvent press(QEvent::MouseButtonPress, from, wheel->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, m);
            QApplication::sendEvent(wheel, &press);
            QMouseEvent move(QEvent::MouseMove, to, wheel->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, m);
            QApplication::sendEvent(wheel, &move);
        };
        const QPointF c(wheel->width() / 2.0, wheel->height() / 2.0);
        const double r = std::min(wheel->width(), wheel->height()) / 2.0 - 1;
        drag(c + QPointF(r / 2, 0), c + QPointF(r / 2, 0), {});            // red, half saturation
        QColor before = w.colors().primary;
        drag(c, c - QPointF(0, r * 0.9), Qt::ShiftModifier);               // drag up: hue to 90
        QColor tint = w.colors().primary;
        CHECK(std::abs(tint.hsvHueF() * 360 - 90) < 2 && std::abs(tint.hsvSaturationF() - before.hsvSaturationF()) < 0.01 &&
              std::abs(tint.valueF() - before.valueF()) < 0.01, "Shift-drag changes the hue only");
        drag(c, c + QPointF(r * 0.3, r), Qt::ControlModifier);             // drag down one radius: half as bright
        QColor light = w.colors().primary;
        CHECK(std::abs(light.valueF() - (tint.valueF() - 0.5)) < 0.02 && std::abs(light.hsvHueF() - tint.hsvHueF()) < 0.01 &&
              std::abs(light.hsvSaturationF() - tint.hsvSaturationF()) < 0.02, "Ctrl-drag changes the lightness only");
    } else {
        CHECK(false, "colour wheel found");
    }

    std::printf("[preview dialogs]\n");
    {
        QDialog dlg(&w);
        bool nonModal = false, blocked = false;
        QTimer::singleShot(0, &dlg, [&] {
            nonModal = !dlg.isModal() && dlg.isVisible();
            QKeyEvent key(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
            blocked = QApplication::sendEvent(&w, &key);
            dlg.accept();
        });
        CHECK(execUndimmed(dlg) == QDialog::Accepted && nonModal && blocked,
              "preview dialogs are not modal (no compositor dimming) but still block the main window");
    }

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
        {
            // painting on the bottom row / right column must not leak onto row 0 / column 0
            QImage &img = td->activeImage();
            img.fill(Qt::white);
            td->invalidate();
            w.colors().setPrimary(Qt::black);
            bool clean = true;
            for (ToolId tool : {ToolId::Pencil, ToolId::Brush}) {
                w.setActiveTool(tool);
                w.settings().brushWidth = 1;
                w.settings().antialias = false;
                drag({50.5, 79.5}, {60.5, 79.5});            // bottom row
                drag({99.5, 30.5}, {99.5, 40.5});            // right column
                CHECK(near(px(55, 79), Qt::black) && near(px(99, 35), Qt::black), "edge strokes land on the edge");
                for (int x = 0; x < 100; ++x)
                    clean &= near(px(x, 0), Qt::white);
                for (int y = 0; y < 80; ++y)
                    clean &= near(px(0, y), Qt::white);
                img.fill(Qt::white);
                td->invalidate();
            }
            w.settings().antialias = true;
            CHECK(clean, "1px strokes on the bottom/right edge do not paint the first row/column");
        }
        shot("09-tile-view");
        w.action("tileview")->trigger();
        CHECK(!w.view()->tiled(), "tile preview toggles off");
        w.setActiveTool(ToolId::Brush);
    }

    std::printf("[canvas panning]\n");
    {
        CanvasView *v = w.view();
        v->setZoom(1.0);
        QScrollBar *hb = v->horizontalScrollBar(), *vb = v->verticalScrollBar();
        hb->setValue(hb->minimum());
        vb->setValue(vb->minimum());
        QPointF tl = v->imageToView(QPointF(0, 0));
        CHECK(tl.x() > v->viewport()->width() / 2 && tl.y() > v->viewport()->height() / 2,
              "canvas can be panned with its top-left corner deep into the view");
        hb->setValue(hb->maximum());
        vb->setValue(vb->maximum());
        QPointF br = v->imageToView(QPointF(v->document()->width(), v->document()->height()));
        CHECK(br.x() > 0 && br.x() < v->viewport()->width() / 2 && br.y() > 0 && br.y() < v->viewport()->height() / 2,
              "canvas can be panned with its bottom-right corner past the middle, part of it still on screen");
        v->zoomToWindow();
        QPointF c = v->imageToView(QPointF(v->document()->width() / 2.0, v->document()->height() / 2.0));
        CHECK(std::abs(c.x() - v->viewport()->width() / 2.0) <= 1 && std::abs(c.y() - v->viewport()->height() / 2.0) <= 1,
              "zoom to window centres the canvas");
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

#ifdef PIXELFORGE_HAVE_MODELVIEW
    modelViewTests(w);
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
