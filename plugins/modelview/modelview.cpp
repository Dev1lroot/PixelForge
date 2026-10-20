#include "modelview.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSlider>
#include <QSplitter>
#include <QSettings>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>
#include <numeric>

namespace ModelView {

static QSettings settings() { return QSettings("PixelForge", "PixelForge"); }

// formats with a built-in loader, then ones an installed converter can turn into glTF
static const QStringList &openableSuffixes()
{
    static const QStringList all = [] {
        QStringList s = modelSuffixes();
        for (const QString &c : convertibleSuffixes())
            if (!s.contains(c))
                s << c;
        return s;
    }();
    return all;
}

static bool isModelFile(const QString &path)
{
    return openableSuffixes().contains(QFileInfo(path).suffix().toLower());
}

static QString openFilter()
{
    QStringList patterns;
    for (const QString &s : openableSuffixes())
        patterns << "*." + s;
    QString filter = QObject::tr("3D models (%1)").arg(patterns.join(' ')) + ";;" + modelFileFilter();
    QStringList converted;
    for (const QString &s : convertibleSuffixes())
        if (!modelSuffixes().contains(s))
            converted << "*." + s;
    if (!converted.isEmpty())
        filter.replace(";;All files", ";;" + QObject::tr("Converted on open (%1)").arg(converted.join(' ')) + ";;All files");
    return filter;
}

// ---------------------------------------------------------------- View

View::View(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(160, 140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::OpenHandCursor);
    setAcceptDrops(true);
    setToolTip(tr("Left drag: rotate, right or middle drag: pan, wheel: zoom, double-click: reset view.\n"
                  "Drop a model file here to open it."));
    m_rotate.setInterval(33);
    connect(&m_rotate, &QTimer::timeout, this, [this] {
        if (m_drag == Qt::NoButton) {
            m_params.yaw += 0.015;
            changed();
        }
    });
    // after interaction ends, replace the fast draft frame with the antialiased one
    m_settle.setSingleShot(true);
    m_settle.setInterval(150);
    connect(&m_settle, &QTimer::timeout, this, &View::changed);
}

void View::setModel(const Model *model)
{
    m_model = model;
    changed();
}

void View::setTextures(const QVector<MaterialTextures> &textures)
{
    m_textures = textures;
    m_allHidden = !textures.isEmpty() && std::all_of(textures.begin(), textures.end(), [](const MaterialTextures &t) { return t.hidden; });
    changed();
}

void View::setAutoRotate(bool on)
{
    m_autoRotate = on;
    if (on && isVisible())
        m_rotate.start();
    else
        m_rotate.stop();
}

void View::setBusy(const QString &text)
{
    m_busy = text;
    update();
}

void View::resetView()
{
    const RenderParams defaults;
    m_params.yaw = defaults.yaw;
    m_params.pitch = defaults.pitch;
    m_params.distance = defaults.distance;
    m_params.panX = m_params.panY = 0;
    changed();
}

void View::changed()
{
    m_dirty = true;
    update();
}

void View::showEvent(QShowEvent *)
{
    if (m_autoRotate)
        m_rotate.start();
}

void View::hideEvent(QHideEvent *) { m_rotate.stop(); }

void View::paintEvent(QPaintEvent *)
{
    const qreal dpr = devicePixelRatioF();
    const QSize target = (QSizeF(size()) * dpr).toSize();
    // while the view moves render at device resolution; when it rests, 2x supersampled
    const bool draft = m_drag != Qt::NoButton || m_rotate.isActive() || m_settle.isActive();
    if (m_dirty || m_frame.size() != target || (m_frameDraft && !draft)) {
        QImage img((draft ? target : target * 2).expandedTo(QSize(1, 1)), QImage::Format_RGB32);
        renderModel(m_model ? *m_model : Model(), m_textures, m_params, img);
        m_frame = draft ? img : img.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        m_frame.setDevicePixelRatio(dpr);
        m_frameDraft = draft;
        m_dirty = false;
    }
    QPainter p(this);
    p.drawImage(0, 0, m_frame);
    if (!m_busy.isEmpty()) {
        p.setPen(QColor(235, 238, 245));
        p.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, m_busy);
    } else if (m_model && !m_model->isEmpty() && m_allHidden) {
        p.setPen(QColor(215, 218, 225));
        p.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
                   tr("Pick the whole image or a layer for a material below to show it"));
    } else if (!m_model || m_model->isEmpty()) {
        p.setPen(QColor(215, 218, 225));
        p.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
                   tr("Open or drop a 3D model\n(OBJ, FBX, glTF, GLB, STL, PLY%1)")
                       .arg(convertibleSuffixes().isEmpty() ? QString() : tr(", more via converter")));
    }
}

void View::mousePressEvent(QMouseEvent *e)
{
    if (m_drag != Qt::NoButton)
        return;
    m_drag = e->button();
    m_last = e->position();
    setCursor(m_drag == Qt::LeftButton ? Qt::ClosedHandCursor : Qt::SizeAllCursor);
}

void View::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag == Qt::NoButton)
        return;
    const QPointF d = e->position() - m_last;
    m_last = e->position();
    if (m_drag == Qt::LeftButton) {
        m_params.yaw -= d.x() * 0.012;
        m_params.pitch = std::clamp(m_params.pitch + d.y() * 0.012, -1.55, 1.55);
    } else {
        // pan so the model follows the pointer at the orbit centre's depth
        const double perPixel = 2 * m_params.distance * std::tan(qDegreesToRadians(20.0)) / std::max(1, height());
        m_params.panX += d.x() * perPixel;
        m_params.panY -= d.y() * perPixel;
    }
    changed();
}

void View::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != m_drag)
        return;
    m_drag = Qt::NoButton;
    setCursor(Qt::OpenHandCursor);
    m_settle.start();
}

void View::mouseDoubleClickEvent(QMouseEvent *) { resetView(); }

void View::wheelEvent(QWheelEvent *e)
{
    const double steps = e->angleDelta().y() / 120.0;
    m_params.distance = std::clamp(m_params.distance * std::pow(0.9, steps), 0.3, 30.0);
    m_settle.start();
    changed();
}

void View::dragEnterEvent(QDragEnterEvent *e)
{
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile() && isModelFile(u.toLocalFile())) {
            e->acceptProposedAction();
            return;
        }
}

void View::dropEvent(QDropEvent *e)
{
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile() && isModelFile(u.toLocalFile())) {
            e->acceptProposedAction();
            emit fileDropped(u.toLocalFile());
            return;
        }
}

// ---------------------------------------------------------------- Panel

// slot sources: kNone hides the material (colour slot) or means no map (normal slot)
static const char *kNone = "none", *kColor = "color", *kImage = "image", *kModel = "model";
static QString layerKey(const QString &name) { return "layer:" + name; }

Panel::Panel(PixelForge::Host *host, QWidget *parent) : QWidget(parent), m_host(host)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);

    auto *top = new QHBoxLayout;
    auto *open = new QPushButton(tr("Open Model..."));
    open->setToolTip(tr("Load an OBJ, FBX, glTF/GLB, STL or PLY model; other formats are converted with Blender or assimp "
                       "when installed"));
    connect(open, &QPushButton::clicked, this, [this] {
        QSettings s = settings();
        const QString dir = s.value("modelview/lastDir", s.value("modelview/lastModel").toString()).toString();
        const QString path = QFileDialog::getOpenFileName(this, tr("Open 3D Model"), dir, openFilter());
        if (!path.isEmpty())
            openModel(path, true);
    });
    top->addWidget(open);
    auto *reset = new QPushButton(tr("Reset View"));
    top->addWidget(reset);
    top->addStretch(1);
    v->addLayout(top);

    // the 3D view and the options below it share the window; drag the handle between them to resize
    m_splitter = new QSplitter(Qt::Vertical);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);
    v->addWidget(m_splitter, 1);
    m_view = new View;
    connect(reset, &QPushButton::clicked, m_view, &View::resetView);
    connect(m_view, &View::fileDropped, this, [this](const QString &path) { openModel(path, true); });
    m_splitter->addWidget(m_view);
    auto *options = new QWidget;
    auto *ov = new QVBoxLayout(options);
    ov->setContentsMargins(0, 2, 0, 0);
    ov->setSpacing(4);
    m_splitter->addWidget(options);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->restoreState(settings().value("modelview/splitter").toByteArray());
    connect(m_splitter, &QSplitter::splitterMoved, this, [this] { settings().setValue("modelview/splitter", m_splitter->saveState()); });

    auto *g = new QGridLayout;
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(1);
    auto check = [&](const QString &text, bool on, int row, int col, auto apply) {
        auto *cb = new QCheckBox(text);
        cb->setChecked(on);
        connect(cb, &QCheckBox::toggled, this, [this, apply](bool b) {
            apply(b);
            m_view->changed();
        });
        g->addWidget(cb, row, col);
        return cb;
    };
    RenderParams &rp = m_view->params();
    check(tr("Pixelated"), rp.pixelated, 0, 0, [&rp](bool b) { rp.pixelated = b; })
        ->setToolTip(tr("Nearest-neighbour texture sampling; off = bilinear"));
    check(tr("Lighting"), rp.lighting, 0, 1, [&rp](bool b) { rp.lighting = b; });
    check(tr("Normal maps"), rp.normalMaps, 0, 2, [&rp](bool b) { rp.normalMaps = b; });
    check(tr("Two-sided"), rp.twoSided, 1, 0, [&rp](bool b) { rp.twoSided = b; })
        ->setToolTip(tr("Draw back faces too; off = cull them"));
    check(tr("Wireframe"), rp.wireframe, 1, 1, [&rp](bool b) { rp.wireframe = b; });
    check(tr("Rotate"), false, 1, 2, [this](bool b) { m_view->setAutoRotate(b); });
    ov->addLayout(g);

    // UV layout drawn over the image in the editor
    auto *uvRow = new QHBoxLayout;
    m_showUVs = new QCheckBox(tr("UVs on canvas"));
    m_showUVs->setToolTip(tr("Draw the model's UV layout over the image, for the materials that use it"));
    m_showUVs->setChecked(settings().value("modelview/showUVs", false).toBool());
    connect(m_showUVs, &QCheckBox::toggled, this, [this](bool on) {
        settings().setValue("modelview/showUVs", on);
        m_host->updateCanvas();
    });
    uvRow->addWidget(m_showUVs);
    m_uvOpacity = new QSlider(Qt::Horizontal);
    m_uvOpacity->setRange(10, 100);
    m_uvOpacity->setValue(settings().value("modelview/uvOpacity", 80).toInt());
    m_uvOpacity->setToolTip(tr("UV overlay opacity"));
    m_uvOpacity->setMaximumWidth(70);
    connect(m_uvOpacity, &QSlider::valueChanged, this, [this](int v) {
        settings().setValue("modelview/uvOpacity", v);
        if (m_showUVs->isChecked())
            m_host->updateCanvas();
    });
    uvRow->addWidget(m_uvOpacity);
    m_uvLabel = new QLabel(tr("UV map:"));
    uvRow->addWidget(m_uvLabel);
    m_uvCombo = new QComboBox;
    m_uvCombo->setToolTip(tr("The UV map used for the preview and the canvas overlay"));
    m_uvCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_uvCombo->setMinimumContentsLength(5);
    connect(m_uvCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (i < 0 || i == m_model.uvSet)
            return;
        m_model.selectUVSet(i);
        m_regionKey.clear();
        m_view->changed();
        rebuildUvOverlay();
    });
    uvRow->addWidget(m_uvCombo, 1);
    m_uvLabel->hide();
    m_uvCombo->hide();
    ov->addLayout(uvRow);

    m_slotWidget = new QWidget;
    m_slotGrid = new QGridLayout(m_slotWidget);
    m_slotGrid->setContentsMargins(0, 0, 0, 0);
    m_slotGrid->setHorizontalSpacing(4);
    m_slotGrid->setVerticalSpacing(2);
    m_slotArea = new QScrollArea;
    m_slotArea->setWidget(m_slotWidget);
    m_slotArea->setWidgetResizable(true);
    m_slotArea->setFrameShape(QFrame::NoFrame);
    m_slotArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_slotArea->hide();
    ov->addWidget(m_slotArea, 1);

    m_info = new QLabel;
    m_info->setWordWrap(true);
    m_info->setTextFormat(Qt::PlainText);
    QFont f = m_info->font();
    f.setPointSizeF(f.pointSizeF() * 0.9);
    m_info->setFont(f);
    m_info->setStyleSheet("color: #445;");
    ov->addWidget(m_info);

    // follow the model file on disk, so re-exporting from a modeller updates the preview
    m_reloadModel.setSingleShot(true);
    m_reloadModel.setInterval(300);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_reloadModel.start(); });
    connect(&m_conversion, &Conversion::finished, this, &Panel::conversionFinished);
    connect(&m_reloadModel, &QTimer::timeout, this, [this] {
        if (!m_model.file.isEmpty() && QFileInfo::exists(m_model.file))
            openModel(m_model.file, false);
    });
    updateInfo();
}

bool Panel::openModel(const QString &path, bool interactive)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    QString error;
    if (modelSuffixes().contains(suffix)) {
        Model m = loadModel(path, &error);
        if (!m.isEmpty()) {
            m_conversion.cancel();
            m_converting.clear();
            setModel(m, path);
            return true;
        }
    }
    // no built-in loader (or it failed, e.g. an old FBX): let an external tool convert it to glTF
    if (convertibleSuffixes().contains(suffix)) {
        m_convertInteractive = interactive;
        m_converting = path;
        m_conversion.start(path);              // may finish at once from the cache
        if (!m_converting.isEmpty()) {
            m_info->setText(tr("Converting %1 with %2...").arg(QFileInfo(path).fileName(), m_conversion.currentTool()));
            m_view->setBusy(tr("Converting %1...").arg(QFileInfo(path).fileName()));
        }
        return true;
    }
    if (error.isEmpty())
        error = modelSuffixes().contains(suffix)
                    ? tr("Unknown error")
                    : tr("No converter for .%1 files is installed. Install Blender or assimp to open them.").arg(suffix);
    showError(path, error, interactive);
    return false;
}

void Panel::conversionFinished(const QString &source, const QString &glb, const QString &error)
{
    if (QFileInfo(source) != QFileInfo(m_converting))
        return;
    m_converting.clear();
    m_view->setBusy(QString());
    QString err = error;
    if (!glb.isEmpty()) {
        Model m = loadModel(glb, &err);
        if (!m.isEmpty()) {
            setModel(m, source);
            return;
        }
        QFile::remove(glb);                    // don't keep serving a conversion we can't read
    }
    showError(source, err, m_convertInteractive);
}

void Panel::showError(const QString &path, const QString &error, bool interactive)
{
    if (interactive)
        QMessageBox::warning(this, tr("3D Model Preview"), tr("Could not open %1:\n%2").arg(QFileInfo(path).fileName(), error));
    m_error = error;
    updateInfo();
}

void Panel::setModel(Model m, const QString &path)
{
    m.file = path;                             // the source file, also when a converted copy was loaded
    ++m_modelGeneration;
    m_error.clear();
    // keep the slot assignments of materials that are still there (file reloads, re-exports)
    QHash<QString, QPair<QString, QString>> previous;
    for (int i = 0; i < m_slots.size() && i < m_model.materials.size(); ++i)
        previous.insert(m_model.materials[i].name, {m_slots[i].base, m_slots[i].normal});
    m_model = m;
    m_layers = m_host->hasDocument() ? m_host->layers() : QVector<PixelForge::LayerInfo>();
    m_layerNames.clear();
    for (const PixelForge::LayerInfo &l : m_layers)
        if (!m_layerNames.contains(l.name))
            m_layerNames << l.name;
    const int active = m_host->activeLayer();
    m_activeLayer = active >= 0 && active < m_layers.size() ? m_layers[active].name : QString();
    m_slots = QVector<Slot>(m_model.materials.size());
    for (int i = 0; i < m_slots.size(); ++i) {
        const auto prev = previous.find(m_model.materials[i].name);
        m_slots[i].base = prev != previous.end() ? prev->first : defaultKey(i, false);
        m_slots[i].normal = prev != previous.end() ? prev->second : defaultKey(i, true);
    }
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    m_watcher.addPath(path);
    QSettings s = settings();
    s.setValue("modelview/lastModel", path);
    s.setValue("modelview/lastDir", QFileInfo(path).absolutePath());

    m_view->setModel(&m_model);
    {
        QSignalBlocker block(m_uvCombo);
        m_uvCombo->clear();
        m_uvCombo->addItems(m_model.uvSets);
        m_uvCombo->setCurrentIndex(m_model.uvSet);
    }
    m_uvLabel->setVisible(m_model.uvSets.size() > 1);
    m_uvCombo->setVisible(m_model.uvSets.size() > 1);
    rebuildSlots();
    updateTextures();
    rebuildUvOverlay();
    updateInfo();
}

// UV edges of every material, each edge once; which materials are drawn is decided while painting
void Panel::rebuildUvOverlay()
{
    m_uvLines = QVector<QVector<QLineF>>(m_model.materials.size());
    if (m_model.hasUV) {
        for (const Part &part : m_model.parts) {
            QSet<quint64> seen;
            QVector<QLineF> &lines = m_uvLines[part.material];
            for (int i = part.first; i + 2 < part.first + part.count; i += 3)
                for (int k = 0; k < 3; ++k) {
                    const quint32 a = m_model.indices[i + k], b = m_model.indices[i + (k + 1) % 3];
                    const quint64 key = quint64(std::min(a, b)) << 32 | std::max(a, b);
                    if (seen.contains(key))
                        continue;
                    seen.insert(key);
                    const Vertex &va = m_model.vertices[a], &vb = m_model.vertices[b];
                    lines.append(QLineF(va.u, 1.0 - va.v, vb.u, 1.0 - vb.v));
                }
        }
    }
    if (m_showUVs->isChecked())
        m_host->updateCanvas();
}

// A material's UVs are shown when it is painted from the whole image, or from the layer being edited.
// Hidden materials (colour slot "(none)") never show.
bool Panel::showsUVs(int material, const QString &activeLayer) const
{
    const Slot s = m_slots.value(material);
    if (s.base == kNone)
        return false;
    auto matches = [&activeLayer](const QString &key) {
        return key == kImage || (key.startsWith("layer:") && key.mid(6) == activeLayer);
    };
    return matches(s.base) || matches(s.normal);
}

// Rasterizes the UV islands of the materials that show their UVs (same rule as the overlay). Triangles
// sharing a UV position belong to one island; seams split islands, as in a UV editor.
QImage Panel::floodRegions(const QSize &size)
{
    if (m_model.isEmpty() || !m_model.hasUV || size.isEmpty() || !m_host->hasDocument())
        return QImage();
    const QVector<PixelForge::LayerInfo> layers = m_host->layers();
    const int active = m_host->activeLayer();
    const QString activeName = active >= 0 && active < layers.size() ? layers[active].name : QString();
    QString key = QStringLiteral("%1x%2/%3/%4/").arg(size.width()).arg(size.height()).arg(m_modelGeneration).arg(m_model.uvSet);
    QVector<bool> used(m_model.materials.size());
    for (int i = 0; i < used.size(); ++i) {
        used[i] = showsUVs(i, activeName);
        key += used[i] ? '1' : '0';
    }
    if (!used.contains(true))
        return QImage();
    if (key == m_regionKey)
        return m_regions;

    // union-find over triangles, joined through quantized UV positions
    QVector<int> tris;                                   // first index of each used triangle
    for (const Part &part : m_model.parts)
        if (used.value(part.material))
            for (int i = part.first; i + 2 < part.first + part.count; i += 3)
                tris << i;
    QVector<int> parent(tris.size());
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&parent](int x) {
        while (parent[x] != x)
            x = parent[x] = parent[parent[x]];
        return x;
    };
    QHash<quint64, int> owner;
    for (int t = 0; t < tris.size(); ++t)
        for (int k = 0; k < 3; ++k) {
            const Vertex &v = m_model.vertices[m_model.indices[tris[t] + k]];
            const quint64 q = quint64(quint32(qint32(std::lround(v.u * 65536.0)))) << 32 |
                              quint32(qint32(std::lround(v.v * 65536.0)));
            const auto it = owner.constFind(q);
            if (it == owner.cend())
                owner.insert(q, t);
            else
                parent[find(t)] = find(it.value());
        }
    QHash<int, int> label;                               // island root -> 1..n

    QImage img(size, QImage::Format_RGB32);
    img.fill(0);
    const int W = size.width(), H = size.height();
    for (int t = 0; t < tris.size(); ++t) {
        const int root = find(t);
        const int id = label.value(root, int(label.size() + 1));
        label.insert(root, id);
        QPointF p[3];
        for (int k = 0; k < 3; ++k) {
            const Vertex &v = m_model.vertices[m_model.indices[tris[t] + k]];
            p[k] = QPointF(v.u * W, (1.0 - v.v) * H);
        }
        const double area = (p[1].x() - p[0].x()) * (p[2].y() - p[0].y()) - (p[2].x() - p[0].x()) * (p[1].y() - p[0].y());
        if (std::abs(area) < 1e-12)
            continue;
        const int x0 = std::max(0, int(std::floor(std::min({p[0].x(), p[1].x(), p[2].x()}))));
        const int x1 = std::min(W - 1, int(std::ceil(std::max({p[0].x(), p[1].x(), p[2].x()}))));
        const int y0 = std::max(0, int(std::floor(std::min({p[0].y(), p[1].y(), p[2].y()}))));
        const int y1 = std::min(H - 1, int(std::ceil(std::max({p[0].y(), p[1].y(), p[2].y()}))));
        auto edge = [](const QPointF &a, const QPointF &b, double x, double y) {
            return (b.x() - a.x()) * (y - a.y()) - (x - a.x()) * (b.y() - a.y());
        };
        for (int y = y0; y <= y1; ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = x0; x <= x1; ++x) {
                // pixel centres inside the triangle (either winding), edges inclusive
                const double cx = x + 0.5, cy = y + 0.5;
                const double e0 = edge(p[1], p[2], cx, cy) / area, e1 = edge(p[2], p[0], cx, cy) / area,
                             e2 = edge(p[0], p[1], cx, cy) / area;
                if (e0 >= 0 && e1 >= 0 && e2 >= 0)
                    row[x] = 0xFF000000u | quint32(id & 0xFFFFFF);
            }
        }
    }
    m_regionKey = key;
    m_regions = img;
    return img;
}

void Panel::paintUvOverlay(QPainter &p, const QTransform &imageToView)
{
    if (!m_showUVs->isChecked() || !isVisible() || !m_host->hasDocument())
        return;
    QVector<const QVector<QLineF> *> shown;
    qsizetype count = 0;
    for (int i = 0; i < m_uvLines.size(); ++i)
        if (!m_uvLines[i].isEmpty() && showsUVs(i, m_activeLayer)) {
            shown << &m_uvLines[i];
            count += m_uvLines[i].size();
        }
    if (shown.isEmpty())
        return;
    const QSize size = m_host->documentSize();
    p.setTransform(QTransform::fromScale(size.width(), size.height()) * imageToView);
    const int alpha = m_uvOpacity->value() * 255 / 100;
    const bool small = count < 20000;
    p.setRenderHint(QPainter::Antialiasing, small);
    if (small) {
        // dark halo keeps the lines readable on light and dark textures
        QPen halo(QColor(0, 0, 0, alpha / 2), 3);
        halo.setCosmetic(true);
        p.setPen(halo);
        for (const QVector<QLineF> *l : shown)
            p.drawLines(*l);
    }
    QPen pen(QColor(80, 225, 255, alpha), 1);
    pen.setCosmetic(true);
    p.setPen(pen);
    for (const QVector<QLineF> *l : shown)
        p.drawLines(*l);
}

// slots pick up layers named after the material ("Body", "body normal", "Body_n") or simply "normal"
QString Panel::defaultKey(int material, bool normal) const
{
    const QString mat = m_model.materials.value(material).name.toLower();
    QStringList wanted;
    if (normal) {
        for (const QString &sep : {" ", "_", "-", "."})
            wanted << mat + sep + "normal" << mat + sep + "n" << mat + sep + "nrm";
        // a lone "normal" layer is meant for the only material
        if (m_model.materials.size() == 1)
            wanted << "normal";
    } else {
        wanted << mat;
    }
    for (const QString &w : wanted)
        for (const QString &name : m_layerNames)
            if (name.toLower() == w)
                return layerKey(name);
    return kNone;                         // nothing is shown until you pick a source
}

void Panel::fillCombo(QComboBox *combo, bool normal, int material, const QString &key)
{
    QSignalBlocker block(combo);
    combo->clear();
    combo->addItem(tr("(none)"), kNone);
    combo->setItemData(0, normal ? tr("No normal map") : tr("Hide this material: not drawn, no UVs on the canvas"),
                       Qt::ToolTipRole);
    if (!normal)
        combo->addItem(tr("(material color)"), kColor);
    combo->addItem(tr("(whole image)"), kImage);
    const Material &m = m_model.materials[material];
    if (!(normal ? m.normalTexture : m.texture).isNull())
        combo->addItem(tr("(model texture)"), kModel);
    for (int i = int(m_layerNames.size()) - 1; i >= 0; --i)       // top layer first, like the Layers window
        combo->addItem(m_layerNames[i], layerKey(m_layerNames[i]));
    const int at = combo->findData(key);
    combo->setCurrentIndex(at >= 0 ? at : 0);
}

void Panel::rebuildSlots()
{
    while (QLayoutItem *item = m_slotGrid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_slotArea->setVisible(!m_model.isEmpty());
    if (m_model.isEmpty())
        return;
    auto header = [this](const QString &text, int col) {
        auto *l = new QLabel(text);
        QFont f = l->font();
        f.setBold(true);
        l->setFont(f);
        m_slotGrid->addWidget(l, 0, col);
    };
    header(tr("Material"), 0);
    header(tr("Color"), 1);
    header(tr("Normal"), 2);
    for (int i = 0; i < m_slots.size(); ++i) {
        Slot &s = m_slots[i];
        auto *name = new QLabel(m_model.materials[i].name);
        name->setToolTip(m_model.materials[i].name);
        name->setMinimumWidth(40);
        name->setMaximumWidth(90);
        m_slotGrid->addWidget(name, i + 1, 0);
        s.baseCombo = new QComboBox;
        s.normalCombo = new QComboBox;
        for (QComboBox *c : {s.baseCombo, s.normalCombo}) {
            c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            c->setMinimumContentsLength(6);
        }
        fillCombo(s.baseCombo, false, i, s.base);
        fillCombo(s.normalCombo, true, i, s.normal);
        s.base = s.baseCombo->currentData().toString();
        s.normal = s.normalCombo->currentData().toString();
        connect(s.baseCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, i] {
            m_slots[i].base = m_slots[i].baseCombo->currentData().toString();
            updateTextures();
            if (m_showUVs->isChecked())
                m_host->updateCanvas();
        });
        connect(s.normalCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, i] {
            m_slots[i].normal = m_slots[i].normalCombo->currentData().toString();
            updateTextures();
            if (m_showUVs->isChecked())
                m_host->updateCanvas();
        });
        m_slotGrid->addWidget(s.baseCombo, i + 1, 1);
        m_slotGrid->addWidget(s.normalCombo, i + 1, 2);
    }
    m_slotGrid->setColumnStretch(1, 1);
    m_slotGrid->setColumnStretch(2, 1);
    // show up to three materials without scrolling; drag the splitter up to see more
    m_slotArea->setMinimumHeight(std::min(m_slotWidget->sizeHint().height(), 100));
}

QImage Panel::sourceImage(const QString &key, int material, bool normal)
{
    QImage src;
    if (key == kImage) {
        if (m_host->hasDocument())
            src = m_host->flattenedImage();
    } else if (key == kModel) {
        const Material &m = m_model.materials[material];
        return normal ? m.normalTexture : m.texture;                   // already ARGB32
    } else if (key.startsWith("layer:")) {
        // named layers are used even when hidden, so a map can be hidden while painting the others
        const QString name = key.mid(6);
        for (const PixelForge::LayerInfo &l : m_layers)
            if (l.name == name) {
                src = l.image;
                break;
            }
    }
    if (src.isNull())
        return src;
    const qint64 id = src.cacheKey();
    QImage conv = m_converted.value(id);
    if (conv.isNull())
        conv = src.convertToFormat(QImage::Format_ARGB32);             // straight alpha for sampling
    m_convertedNext.insert(id, conv);
    return conv;
}

void Panel::updateTextures()
{
    if (m_model.isEmpty())
        return;
    QVector<MaterialTextures> tex(m_model.materials.size());
    m_convertedNext.clear();
    for (int i = 0; i < tex.size(); ++i) {
        tex[i].color = m_model.materials[i].color;
        tex[i].hidden = m_slots.value(i).base == kNone;
        tex[i].base = sourceImage(m_slots.value(i).base, i, false);
        tex[i].normal = sourceImage(m_slots.value(i).normal, i, true);
    }
    m_converted.swap(m_convertedNext);                                 // drop copies nothing uses any more
    m_convertedNext.clear();
    m_view->setTextures(tex);
}

void Panel::updateInfo()
{
    if (!m_error.isEmpty() && m_model.isEmpty()) {
        m_info->setText(m_error);
        return;
    }
    if (m_model.isEmpty()) {
        m_info->setText(tr("Load a model to see the image on it. Each material becomes a slot that takes the "
                           "whole image or one of its layers."));
        return;
    }
    QString text = tr("%1 — %2 triangles, %n material(s)", nullptr, int(m_model.materials.size()))
                       .arg(QFileInfo(m_model.file).fileName(), QLocale().toString(m_model.triangleCount()));
    if (!m_model.hasUV)
        text += "\n" + tr("The model has no UV coordinates, so textures can't be shown on it.");
    if (!m_error.isEmpty())
        text += "\n" + tr("Reload failed: %1").arg(m_error);
    m_info->setText(text);
}

void Panel::reload()
{
    // the host calls us for every edit, so skip the work while the window is closed
    if (!isVisible())
        return;
    m_layers = m_host->hasDocument() ? m_host->layers() : QVector<PixelForge::LayerInfo>();
    QStringList names;
    for (const PixelForge::LayerInfo &l : m_layers)
        if (!names.contains(l.name))
            names << l.name;
    const bool layersRenamed = names != m_layerNames;
    if (layersRenamed) {
        // layers were added, renamed or reordered: refresh the choices, keeping what each slot uses
        m_layerNames = names;
        for (int i = 0; i < m_slots.size(); ++i) {
            if (!m_slots[i].baseCombo)
                continue;
            fillCombo(m_slots[i].baseCombo, false, i, m_slots[i].base);
            fillCombo(m_slots[i].normalCombo, true, i, m_slots[i].normal);
            m_slots[i].base = m_slots[i].baseCombo->currentData().toString();
            m_slots[i].normal = m_slots[i].normalCombo->currentData().toString();
        }
    }
    const int active = m_host->activeLayer();
    const QString activeName = active >= 0 && active < m_layers.size() ? m_layers[active].name : QString();
    const bool overlayChanged = activeName != m_activeLayer || layersRenamed;
    m_activeLayer = activeName;
    updateTextures();
    if (overlayChanged && m_showUVs->isChecked())
        m_host->updateCanvas();             // the overlay follows the layer being edited
}

void Panel::hideEvent(QHideEvent *)
{
    if (m_showUVs->isChecked())
        m_host->updateCanvas();             // the overlay is only drawn while this window is open
}

void Panel::showEvent(QShowEvent *)
{
    if (m_showUVs->isChecked())
        m_host->updateCanvas();
    if (!m_triedLast && m_model.isEmpty()) {
        m_triedLast = true;
        const QString last = settings().value("modelview/lastModel").toString();
        if (!last.isEmpty() && QFileInfo::exists(last)) {
            openModel(last, false);
            return;
        }
    }
    reload();                             // catch up on edits made while the window was hidden
}

} // namespace ModelView

static QIcon modelIcon()
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(2, 2);
    QRadialGradient g(QPointF(6, 5.5), 8);
    g.setColorAt(0, QColor(250, 210, 150));
    g.setColorAt(1, QColor(200, 100, 40));
    p.setPen(QPen(QColor(70, 35, 15), 0.8));
    p.setBrush(g);
    p.drawEllipse(QRectF(1.5, 1.5, 13, 13));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(90, 45, 20, 160), 0.6));
    p.drawEllipse(QRectF(5, 1.5, 6, 13));
    p.drawLine(QPointF(1.5, 8), QPointF(14.5, 8));
    p.end();
    pm.setDevicePixelRatio(2);
    return QIcon(pm);
}

void ModelViewPlugin::initialize(PixelForge::Host *host)
{
    auto *panel = new ModelView::Panel(host);
    panel->setObjectName("modelview_panel");
    host->addPanel("modelview", tr("3D &Model Preview"), panel, QKeySequence(Qt::Key_F12), modelIcon());
    host->onDocumentChanged(panel, [panel] { panel->reload(); });
    host->addCanvasOverlay(panel, [panel](QPainter &p, const QTransform &t) { panel->paintUvOverlay(p, t); });
    host->setFloodRegions(panel, [panel](const QSize &size) { return panel->floodRegions(size); });
}
