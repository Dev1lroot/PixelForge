#include "modelview.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QFileDialog>
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
#include <QSettings>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>

namespace ModelView {

static QSettings settings() { return QSettings("PixelForge", "PixelForge"); }

static bool isModelFile(const QString &path)
{
    return modelSuffixes().contains(QFileInfo(path).suffix().toLower());
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
    if (!m_model || m_model->isEmpty()) {
        p.setPen(QColor(215, 218, 225));
        p.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
                   tr("Open or drop a 3D model\n(OBJ, glTF, GLB, STL, PLY)"));
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

static const char *kNone = "none", *kImage = "image", *kModel = "model";
static QString layerKey(const QString &name) { return "layer:" + name; }

Panel::Panel(PixelForge::Host *host, QWidget *parent) : QWidget(parent), m_host(host)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);

    auto *top = new QHBoxLayout;
    auto *open = new QPushButton(tr("Open Model..."));
    open->setToolTip(tr("Load an OBJ, glTF/GLB, STL or PLY model"));
    connect(open, &QPushButton::clicked, this, [this] {
        QSettings s = settings();
        const QString dir = s.value("modelview/lastDir", s.value("modelview/lastModel").toString()).toString();
        const QString path = QFileDialog::getOpenFileName(this, tr("Open 3D Model"), dir, modelFileFilter());
        if (!path.isEmpty())
            openModel(path, true);
    });
    top->addWidget(open);
    auto *reset = new QPushButton(tr("Reset View"));
    top->addWidget(reset);
    top->addStretch(1);
    v->addLayout(top);

    m_view = new View;
    connect(reset, &QPushButton::clicked, m_view, &View::resetView);
    connect(m_view, &View::fileDropped, this, [this](const QString &path) { openModel(path, true); });
    v->addWidget(m_view, 1);

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
    v->addLayout(g);

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
    m_slotArea->setMaximumHeight(130);
    m_slotArea->hide();
    v->addWidget(m_slotArea);

    m_info = new QLabel;
    m_info->setWordWrap(true);
    m_info->setTextFormat(Qt::PlainText);
    QFont f = m_info->font();
    f.setPointSizeF(f.pointSizeF() * 0.9);
    m_info->setFont(f);
    m_info->setStyleSheet("color: #445;");
    v->addWidget(m_info);

    // follow the model file on disk, so re-exporting from a modeller updates the preview
    m_reloadModel.setSingleShot(true);
    m_reloadModel.setInterval(300);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_reloadModel.start(); });
    connect(&m_reloadModel, &QTimer::timeout, this, [this] {
        if (!m_model.file.isEmpty() && QFileInfo::exists(m_model.file))
            openModel(m_model.file, false);
    });
    updateInfo();
}

bool Panel::openModel(const QString &path, bool interactive)
{
    QString error;
    Model m = loadModel(path, &error);
    if (m.isEmpty()) {
        if (interactive)
            QMessageBox::warning(this, tr("3D Model Preview"), tr("Could not open %1:\n%2").arg(QFileInfo(path).fileName(), error));
        m_error = error;
        updateInfo();
        return false;
    }
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
    rebuildSlots();
    updateTextures();
    updateInfo();
    return true;
}

// slots pick up layers named after the material ("Body", "body normal", "Body_n") or simply "normal"
QString Panel::defaultKey(int material, bool normal) const
{
    const QString mat = m_model.materials.value(material).name.toLower();
    const bool hasOwn = normal ? !m_model.materials.value(material).normalTexture.isNull()
                               : !m_model.materials.value(material).texture.isNull();
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
    // with several materials, ones that bring their own texture keep it until a layer is assigned
    if (hasOwn && (normal || m_model.materials.size() > 1))
        return kModel;
    return normal ? kNone : kImage;
}

void Panel::fillCombo(QComboBox *combo, bool normal, int material, const QString &key)
{
    QSignalBlocker block(combo);
    combo->clear();
    combo->addItem(normal ? tr("(none)") : tr("(material color)"), kNone);
    combo->addItem(tr("(whole image)"), kImage);
    const Material &m = m_model.materials[material];
    if (!(normal ? m.normalTexture : m.texture).isNull())
        combo->addItem(tr("(model texture)"), kModel);
    for (int i = int(m_layerNames.size()) - 1; i >= 0; --i)       // top layer first, like the Layers window
        combo->addItem(m_layerNames[i], layerKey(m_layerNames[i]));
    const int at = combo->findData(key);
    combo->setCurrentIndex(at >= 0 ? at : combo->findData(normal ? kNone : kImage));
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
        });
        connect(s.normalCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, i] {
            m_slots[i].normal = m_slots[i].normalCombo->currentData().toString();
            updateTextures();
        });
        m_slotGrid->addWidget(s.baseCombo, i + 1, 1);
        m_slotGrid->addWidget(s.normalCombo, i + 1, 2);
    }
    m_slotGrid->setColumnStretch(1, 1);
    m_slotGrid->setColumnStretch(2, 1);
    // show up to three materials without scrolling
    m_slotArea->setMinimumHeight(std::min(m_slotWidget->sizeHint().height(), m_slotArea->maximumHeight()));
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
    if (names != m_layerNames) {
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
    updateTextures();
}

void Panel::showEvent(QShowEvent *)
{
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
}
