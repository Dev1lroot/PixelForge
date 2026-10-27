#include "dialogs.h"
#include "icons.h"

#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QComboBox>
#include <QLineEdit>
#include <QSlider>
#include <QLabel>
#include <QGroupBox>
#include <QGridLayout>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QButtonGroup>
#include <QToolButton>
#include <QDial>
#include <QPainter>
#include <QApplication>
#include <QEventLoop>
#include <cmath>

// ============================================================ SizeEditor

SizeEditor::SizeEditor(const QSize &initial, double dpi, bool percentMode, QWidget *parent)
    : QWidget(parent), m_base(initial)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    if (percentMode) {
        auto *pr = new QHBoxLayout;
        m_byPercent = new QRadioButton(tr("By percentage:"));
        m_percent = new QSpinBox;
        m_percent->setRange(1, 3200);
        m_percent->setValue(100);
        m_percent->setSuffix(" %");
        pr->addWidget(m_byPercent);
        pr->addWidget(m_percent);
        pr->addStretch();
        v->addLayout(pr);
        m_byAbs = new QRadioButton(tr("By absolute size:"));
        v->addWidget(m_byAbs);
        m_byAbs->setChecked(true);
        connect(m_percent, qOverload<int>(&QSpinBox::valueChanged), this, [this] {
            m_byPercent->setChecked(true);
            fromPercent();
        });
    }
    m_aspect = new QCheckBox(tr("Maintain aspect ratio"));
    m_aspect->setChecked(percentMode);
    v->addWidget(m_aspect);

    auto *pix = new QGroupBox(tr("Pixel size"));
    auto *pg = new QGridLayout(pix);
    m_w = new QSpinBox;
    m_h = new QSpinBox;
    for (QSpinBox *s : {m_w, m_h}) {
        s->setRange(1, 65535);
        s->setFixedWidth(90);
    }
    m_w->setValue(initial.width());
    m_h->setValue(initial.height());
    pg->addWidget(new QLabel(tr("Width:")), 0, 0);
    pg->addWidget(m_w, 0, 1);
    pg->addWidget(new QLabel(tr("pixels")), 0, 2);
    pg->addWidget(new QLabel(tr("Height:")), 1, 0);
    pg->addWidget(m_h, 1, 1);
    pg->addWidget(new QLabel(tr("pixels")), 1, 2);
    v->addWidget(pix);

    auto *pr = new QGroupBox(tr("Print size"));
    auto *rg = new QGridLayout(pr);
    m_res = new QDoubleSpinBox;
    m_res->setRange(1, 10000);
    m_res->setDecimals(2);
    m_res->setValue(dpi);
    m_resUnit = new QComboBox;
    m_resUnit->addItems({tr("pixels/inch"), tr("pixels/centimeter")});
    m_pw = new QDoubleSpinBox;
    m_ph = new QDoubleSpinBox;
    for (QDoubleSpinBox *s : {m_pw, m_ph}) {
        s->setRange(0.01, 100000);
        s->setDecimals(2);
        s->setFixedWidth(90);
    }
    m_printUnit = new QComboBox;
    m_printUnit->addItems({tr("inches"), tr("centimeters")});
    rg->addWidget(new QLabel(tr("Resolution:")), 0, 0);
    rg->addWidget(m_res, 0, 1);
    rg->addWidget(m_resUnit, 0, 2);
    rg->addWidget(new QLabel(tr("Width:")), 1, 0);
    rg->addWidget(m_pw, 1, 1);
    rg->addWidget(m_printUnit, 1, 2);
    rg->addWidget(new QLabel(tr("Height:")), 2, 0);
    rg->addWidget(m_ph, 2, 1);
    v->addWidget(pr);
    syncPrint();

    connect(m_w, qOverload<int>(&QSpinBox::valueChanged), this, [this] { fromPixels(true); });
    connect(m_h, qOverload<int>(&QSpinBox::valueChanged), this, [this] { fromPixels(false); });
    connect(m_pw, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { fromPrint(true); });
    connect(m_ph, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { fromPrint(false); });
    connect(m_res, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { syncPrint(); });
    connect(m_resUnit, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_busy = true;
        m_res->setValue(i == 1 ? m_res->value() / 2.54 : m_res->value() * 2.54);
        m_busy = false;
        syncPrint();
    });
    connect(m_printUnit, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { syncPrint(); });
}

double SizeEditor::dpi() const
{
    return m_resUnit->currentIndex() == 1 ? m_res->value() * 2.54 : m_res->value();
}

QSize SizeEditor::pixelSize() const { return QSize(m_w->value(), m_h->value()); }

void SizeEditor::syncPrint()
{
    if (m_busy)
        return;
    m_busy = true;
    double unitPerInch = m_printUnit->currentIndex() == 1 ? 2.54 : 1.0;
    m_pw->setValue(m_w->value() / dpi() * unitPerInch);
    m_ph->setValue(m_h->value() / dpi() * unitPerInch);
    m_busy = false;
}

void SizeEditor::fromPercent()
{
    if (m_busy)
        return;
    m_busy = true;
    m_w->setValue(std::max(1, int(std::round(m_base.width() * m_percent->value() / 100.0))));
    m_h->setValue(std::max(1, int(std::round(m_base.height() * m_percent->value() / 100.0))));
    m_busy = false;
    syncPrint();
}

void SizeEditor::fromPixels(bool widthChanged)
{
    if (m_busy)
        return;
    if (m_byAbs)
        m_byAbs->setChecked(true);
    m_busy = true;
    if (m_aspect->isChecked()) {
        if (widthChanged)
            m_h->setValue(std::max(1, int(std::round(m_w->value() * double(m_base.height()) / m_base.width()))));
        else
            m_w->setValue(std::max(1, int(std::round(m_h->value() * double(m_base.width()) / m_base.height()))));
    }
    m_busy = false;
    syncPrint();
}

void SizeEditor::fromPrint(bool widthChanged)
{
    if (m_busy)
        return;
    double unitPerInch = m_printUnit->currentIndex() == 1 ? 2.54 : 1.0;
    m_busy = true;
    if (widthChanged)
        m_w->setValue(std::max(1, int(std::round(m_pw->value() / unitPerInch * dpi()))));
    else
        m_h->setValue(std::max(1, int(std::round(m_ph->value() / unitPerInch * dpi()))));
    m_busy = false;
    fromPixels(widthChanged);
}

static QDialogButtonBox *okCancel(QDialog *d)
{
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(bb, &QDialogButtonBox::accepted, d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, d, &QDialog::reject);
    return bb;
}

// ============================================================ New

NewImageDialog::NewImageDialog(const QSize &initial, double dpi, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("New Image"));
    setWindowIcon(Icons::get("new"));
    auto *v = new QVBoxLayout(this);
    m_size = new SizeEditor(initial, dpi, false);
    v->addWidget(m_size);
    v->addWidget(okCancel(this));
}

QSize NewImageDialog::imageSize() const { return m_size->pixelSize(); }
double NewImageDialog::dpi() const { return m_size->dpi(); }

// ============================================================ Resize

ResizeDialog::ResizeDialog(const QSize &cur, double dpi, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Resize Image"));
    setWindowIcon(Icons::get("resize"));
    auto *v = new QVBoxLayout(this);
    auto *rh = new QHBoxLayout;
    rh->addWidget(new QLabel(tr("Resampling:")));
    m_resample = new QComboBox;
    m_resample->addItems({tr("Best Quality"), tr("Bilinear"), tr("Nearest Neighbor")});
    rh->addWidget(m_resample, 1);
    v->addLayout(rh);
    m_size = new SizeEditor(cur, dpi, true);
    v->addWidget(m_size);
    v->addWidget(okCancel(this));
}

QSize ResizeDialog::imageSize() const { return m_size->pixelSize(); }
double ResizeDialog::dpi() const { return m_size->dpi(); }
Qt::TransformationMode ResizeDialog::mode() const
{
    return m_resample->currentIndex() == 2 ? Qt::FastTransformation : Qt::SmoothTransformation;
}
bool ResizeDialog::nearest() const { return m_resample->currentIndex() == 2; }

// ============================================================ Canvas Size

static QIcon anchorArrow(int dx, int dy)
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    if (dx == 0 && dy == 0) {
        p.setBrush(QColor(60, 120, 220));
        p.setPen(QColor(20, 50, 110));
        p.drawRect(QRectF(9, 9, 14, 14));
    } else {
        double a = std::atan2(dy, dx);
        QPointF c(16, 16);
        QPointF tip = c + QPointF(std::cos(a), std::sin(a)) * 11;
        QPointF base = c - QPointF(std::cos(a), std::sin(a)) * 3;
        p.setPen(QPen(QColor(40, 40, 50), 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(base, tip);
        QPointF l = tip - QPointF(std::cos(a - 0.6), std::sin(a - 0.6)) * 6;
        QPointF r = tip - QPointF(std::cos(a + 0.6), std::sin(a + 0.6)) * 6;
        p.drawLine(tip, l);
        p.drawLine(tip, r);
    }
    return QIcon(pm);
}

CanvasSizeDialog::CanvasSizeDialog(const QSize &cur, double dpi, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Resize Canvas"));
    setWindowIcon(Icons::get("canvassize"));
    auto *v = new QVBoxLayout(this);
    m_size = new SizeEditor(cur, dpi, true);
    v->addWidget(m_size);
    auto *ab = new QGroupBox(tr("Anchor"));
    auto *ag = new QGridLayout(ab);
    ag->setSpacing(2);
    m_anchorGroup = new QButtonGroup(this);
    for (int i = 0; i < 9; ++i) {
        auto *b = new QToolButton;
        b->setFixedSize(34, 34);
        b->setIconSize(QSize(28, 28));
        b->setCheckable(true);
        m_anchorGroup->addButton(b, i);
        ag->addWidget(b, i / 3, i % 3);
    }
    ag->setColumnStretch(3, 1);
    m_anchorGroup->button(4)->setChecked(true);
    connect(m_anchorGroup, &QButtonGroup::idClicked, this, [this](int id) {
        Qt::Alignment h = id % 3 == 0 ? Qt::AlignLeft : id % 3 == 1 ? Qt::AlignHCenter : Qt::AlignRight;
        Qt::Alignment vv = id / 3 == 0 ? Qt::AlignTop : id / 3 == 1 ? Qt::AlignVCenter : Qt::AlignBottom;
        m_anchor = h | vv;
        updateAnchorIcons();
    });
    updateAnchorIcons();
    v->addWidget(ab);
    v->addWidget(okCancel(this));
}

void CanvasSizeDialog::updateAnchorIcons()
{
    int sel = m_anchorGroup->checkedId();
    int sx = sel % 3, sy = sel / 3;
    for (int i = 0; i < 9; ++i) {
        int dx = i % 3 - sx, dy = i / 3 - sy;
        m_anchorGroup->button(i)->setIcon(std::abs(dx) <= 1 && std::abs(dy) <= 1 ? anchorArrow(dx, dy) : QIcon());
    }
}

QSize CanvasSizeDialog::imageSize() const { return m_size->pixelSize(); }
double CanvasSizeDialog::dpi() const { return m_size->dpi(); }

// ============================================================ Layer Properties

LayerPropertiesDialog::LayerPropertiesDialog(Document *doc, QWidget *parent) : QDialog(parent), m_doc(doc)
{
    setWindowTitle(tr("Layer Properties"));
    setWindowIcon(Icons::get("layerprops"));
    m_orig = doc->activeLayer();
    auto *v = new QVBoxLayout(this);
    auto *gen = new QGroupBox(tr("General"));
    auto *f = new QFormLayout(gen);
    m_name = new QLineEdit(m_orig.name);
    m_visible = new QCheckBox(tr("Visible"));
    m_visible->setChecked(m_orig.visible);
    f->addRow(tr("Name:"), m_name);
    f->addRow(QString(), m_visible);
    v->addWidget(gen);

    auto *bl = new QGroupBox(tr("Blending"));
    auto *bf = new QFormLayout(bl);
    m_blend = new QComboBox;
    for (int i = 0; i < int(BlendMode::Count); ++i)
        m_blend->addItem(blendModeName(BlendMode(i)));
    m_blend->setCurrentIndex(int(m_orig.blend));
    bf->addRow(tr("Mode:"), m_blend);
    auto *oh = new QHBoxLayout;
    m_opSlider = new QSlider(Qt::Horizontal);
    m_opSlider->setRange(0, 255);
    m_opSlider->setValue(m_orig.opacity);
    m_opSpin = new QSpinBox;
    m_opSpin->setRange(0, 255);
    m_opSpin->setValue(m_orig.opacity);
    oh->addWidget(m_opSlider, 1);
    oh->addWidget(m_opSpin);
    bf->addRow(tr("Opacity:"), oh);
    v->addWidget(bl);
    v->addWidget(okCancel(this));

    connect(m_opSlider, &QSlider::valueChanged, m_opSpin, &QSpinBox::setValue);
    connect(m_opSpin, qOverload<int>(&QSpinBox::valueChanged), m_opSlider, &QSlider::setValue);
    connect(m_opSpin, qOverload<int>(&QSpinBox::valueChanged), this, &LayerPropertiesDialog::apply);
    connect(m_visible, &QCheckBox::toggled, this, &LayerPropertiesDialog::apply);
    connect(m_blend, qOverload<int>(&QComboBox::currentIndexChanged), this, &LayerPropertiesDialog::apply);
    connect(m_name, &QLineEdit::textChanged, this, &LayerPropertiesDialog::apply);
    setMinimumWidth(340);
}

void LayerPropertiesDialog::apply()
{
    Layer &l = m_doc->activeLayer();
    l.name = m_name->text();
    l.visible = m_visible->isChecked();
    l.blend = BlendMode(m_blend->currentIndex());
    l.opacity = m_opSpin->value();
    m_doc->notifyStructureChanged();
}

bool LayerPropertiesDialog::changed() const
{
    const Layer &l = m_doc->activeLayer();
    return l.name != m_orig.name || l.visible != m_orig.visible || l.blend != m_orig.blend || l.opacity != m_orig.opacity;
}

void LayerPropertiesDialog::done(int r)
{
    if (r != QDialog::Accepted) {
        Layer &l = m_doc->activeLayer();
        l.name = m_orig.name;
        l.visible = m_orig.visible;
        l.blend = m_orig.blend;
        l.opacity = m_orig.opacity;
        m_doc->notifyStructureChanged();
    }
    QDialog::done(r);
}

// ============================================================ Rotate / Zoom

RotateZoomDialog::RotateZoomDialog(Document *doc, QWidget *parent) : QDialog(parent), m_doc(doc)
{
    setWindowTitle(tr("Rotate / Zoom Layer"));
    setWindowIcon(Icons::get("rotatezoom"));
    m_backup = doc->activeImage();
    auto *v = new QVBoxLayout(this);

    auto *ag = new QGroupBox(tr("Angle"));
    auto *ah = new QHBoxLayout(ag);
    auto *dial = new QDial;
    dial->setRange(-180, 180);
    dial->setWrapping(true);
    dial->setNotchesVisible(true);
    dial->setFixedSize(64, 64);
    m_angle = new QDoubleSpinBox;
    m_angle->setRange(-180, 180);
    m_angle->setDecimals(2);
    m_angle->setSuffix(QString::fromUtf8("°"));
    ah->addWidget(dial);
    ah->addWidget(m_angle);
    ah->addStretch();
    v->addWidget(ag);

    auto *pg = new QGroupBox(tr("Pan"));
    auto *pf = new QFormLayout(pg);
    m_panX = new QSpinBox;
    m_panY = new QSpinBox;
    for (QSpinBox *s : {m_panX, m_panY})
        s->setRange(-65535, 65535);
    pf->addRow(tr("Offset X:"), m_panX);
    pf->addRow(tr("Offset Y:"), m_panY);
    v->addWidget(pg);

    auto *zg = new QGroupBox(tr("Zoom"));
    auto *zh = new QHBoxLayout(zg);
    auto *zs = new QSlider(Qt::Horizontal);
    zs->setRange(1, 1600);
    zs->setValue(100);
    m_zoom = new QDoubleSpinBox;
    m_zoom->setRange(0.01, 16);
    m_zoom->setDecimals(2);
    m_zoom->setValue(1.0);
    m_zoom->setSingleStep(0.05);
    zh->addWidget(zs, 1);
    zh->addWidget(m_zoom);
    v->addWidget(zg);
    m_tile = new QCheckBox(tr("Tiling"));
    v->addWidget(m_tile);
    v->addWidget(okCancel(this));

    connect(dial, &QDial::valueChanged, m_angle, [this](int d) {
        double a = 90 - d;
        while (a > 180) a -= 360;
        m_angle->setValue(a);
    });
    connect(zs, &QSlider::valueChanged, m_zoom, [this](int z) { m_zoom->setValue(z / 100.0); });
    connect(m_angle, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &RotateZoomDialog::apply);
    connect(m_zoom, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &RotateZoomDialog::apply);
    connect(m_panX, qOverload<int>(&QSpinBox::valueChanged), this, &RotateZoomDialog::apply);
    connect(m_panY, qOverload<int>(&QSpinBox::valueChanged), this, &RotateZoomDialog::apply);
    connect(m_tile, &QCheckBox::toggled, this, &RotateZoomDialog::apply);
    setMinimumWidth(320);
}

void RotateZoomDialog::apply()
{
    QImage out(m_backup.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::Antialiasing);
        m_doc->clipToSelection(p);
        QPointF c(m_backup.width() / 2.0, m_backup.height() / 2.0);
        p.translate(c + QPointF(m_panX->value(), m_panY->value()));
        p.rotate(-m_angle->value());
        p.scale(m_zoom->value(), m_zoom->value());
        p.translate(-c);
        if (m_tile->isChecked()) {
            QBrush b(m_backup);
            p.fillRect(QRectF(-m_backup.width() * 8.0, -m_backup.height() * 8.0, m_backup.width() * 17.0, m_backup.height() * 17.0), b);
        } else {
            p.drawImage(0, 0, m_backup);
        }
    }
    if (m_doc->hasSelection()) {
        // keep pixels outside the selection untouched
        QImage merged = m_backup;
        QPainter p(&merged);
        p.setClipPath(m_doc->selection());
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(0, 0, out);
        p.end();
        out = merged;
    }
    m_doc->activeImage() = out;
    m_doc->invalidate();
}

void RotateZoomDialog::done(int r)
{
    if (r != QDialog::Accepted) {
        m_doc->activeImage() = m_backup;
        m_doc->invalidate();
    } else {
        apply();
    }
    QDialog::done(r);
}

// ============================================================ Save options

SaveOptionsDialog::SaveOptionsDialog(const QString &format, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Save Configuration"));
    auto *v = new QVBoxLayout(this);
    auto *g = new QGroupBox(tr("%1 Quality").arg(format.toUpper()));
    auto *h = new QHBoxLayout(g);
    m_q = new QSlider(Qt::Horizontal);
    m_q->setRange(1, 100);
    m_q->setValue(95);
    auto *spin = new QSpinBox;
    spin->setRange(1, 100);
    spin->setValue(95);
    connect(m_q, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    connect(spin, qOverload<int>(&QSpinBox::valueChanged), m_q, &QSlider::setValue);
    h->addWidget(m_q, 1);
    h->addWidget(spin);
    v->addWidget(g);
    v->addWidget(okCancel(this));
    setMinimumWidth(300);
}

int SaveOptionsDialog::quality() const { return m_q->value(); }

// ============================================================ execUndimmed

namespace {
class InputBlocker : public QObject
{
public:
    explicit InputBlocker(QDialog *dlg) : m_dlg(dlg) { qApp->installEventFilter(this); }
    ~InputBlocker() override { qApp->removeEventFilter(this); }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        switch (e->type()) {
        case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease: case QEvent::MouseButtonDblClick:
        case QEvent::Wheel: case QEvent::KeyPress: case QEvent::KeyRelease: case QEvent::ShortcutOverride:
        case QEvent::TabletPress: case QEvent::TabletRelease: case QEvent::TouchBegin:
        case QEvent::DragEnter: case QEvent::Drop: case QEvent::Close:
            break;
        default:
            return false;
        }
        auto *w = qobject_cast<QWidget *>(o);
        if (!w)
            return false;
        // the dialog's own popups (combo lists, menus) are separate windows parented inside it
        for (QWidget *p = w; p; p = p->parentWidget())
            if (p == m_dlg)
                return false;
        if (e->type() == QEvent::MouseButtonPress && w->isWindow()) {
            m_dlg->raise();
            m_dlg->activateWindow();
        }
        e->ignore();
        return true;
    }

private:
    QDialog *m_dlg;
};
}

int execUndimmed(QDialog &dlg)
{
    dlg.setWindowModality(Qt::NonModal);
    InputBlocker blocker(&dlg);
    QEventLoop loop;
    QObject::connect(&dlg, &QDialog::finished, &loop, &QEventLoop::quit);
    dlg.show();
    dlg.raise();
    dlg.activateWindow();
    loop.exec(QEventLoop::DialogExec);
    return dlg.result();
}
