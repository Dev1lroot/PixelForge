#include "colorpanel.h"
#include "settings.h"
#include "icons.h"

#include <QPainter>
#include <QMouseEvent>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include <QSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QMenu>
#include <QColorDialog>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QRegularExpressionValidator>
#include <cmath>

// ============================================================ ColorWheel

ColorWheel::ColorWheel(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(110, 110);
    setCursor(Qt::CrossCursor);
}

void ColorWheel::setHsv(double h, double s, double v)
{
    m_h = h;
    m_s = s;
    m_v = v;
    update();
}

void ColorWheel::resizeEvent(QResizeEvent *)
{
    m_cacheV = -1;
}

void ColorWheel::rebuild()
{
    const qreal dpr = devicePixelRatioF();
    int side = int(std::min(width(), height()) * dpr);
    m_cache = QImage(side, side, QImage::Format_ARGB32_Premultiplied);
    m_cache.fill(Qt::transparent);
    const double r = side / 2.0 - 1;
    const double c = side / 2.0;
    // Wheel shows full brightness so colours stay legible, like the classic picker.
    for (int y = 0; y < side; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(m_cache.scanLine(y));
        for (int x = 0; x < side; ++x) {
            double dx = x + 0.5 - c, dy = y + 0.5 - c;
            double d = std::sqrt(dx * dx + dy * dy);
            if (d > r + 1)
                continue;
            double hue = std::atan2(-dy, dx) * 180 / M_PI;
            if (hue < 0)
                hue += 360;
            double sat = std::min(1.0, d / r);
            QColor col = QColor::fromHsvF(hue / 360.0, sat, 1.0);
            double a = std::clamp(r + 1 - d, 0.0, 1.0);
            line[x] = qPremultiply(qRgba(col.red(), col.green(), col.blue(), int(a * 255)));
        }
    }
    m_cache.setDevicePixelRatio(dpr);
    m_cacheV = 1;
}

void ColorWheel::paintEvent(QPaintEvent *)
{
    if (m_cacheV < 0)
        rebuild();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    int side = std::min(width(), height());
    QPointF o((width() - side) / 2.0, (height() - side) / 2.0);
    p.drawImage(o, m_cache);
    double r = side / 2.0 - 1;
    QPointF c = o + QPointF(side / 2.0, side / 2.0);
    double a = m_h * M_PI / 180;
    QPointF m = c + QPointF(std::cos(a) * m_s * r, -std::sin(a) * m_s * r);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::white, 1));
    p.drawRect(QRectF(m.x() - 3, m.y() - 3, 6, 6));
    p.setPen(QPen(Qt::black, 1));
    p.drawRect(QRectF(m.x() - 2, m.y() - 2, 4, 4));
}

void ColorWheel::pick(const QPointF &pt)
{
    int side = std::min(width(), height());
    QPointF c(width() / 2.0, height() / 2.0);
    double r = side / 2.0 - 1;
    double dx = pt.x() - c.x(), dy = pt.y() - c.y();
    double h = std::atan2(-dy, dx) * 180 / M_PI;
    if (h < 0)
        h += 360;
    double s = std::min(1.0, std::sqrt(dx * dx + dy * dy) / r);
    m_h = h;
    m_s = s;
    update();
    emit hsChanged(h, s);
}

void ColorWheel::mousePressEvent(QMouseEvent *e) { pick(e->position()); }
void ColorWheel::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons())
        pick(e->position());
}

// ============================================================ SwatchWidget

SwatchWidget::SwatchWidget(ColorState *cs, QWidget *parent) : QWidget(parent), m_cs(cs)
{
    setFixedSize(52, 52);
    connect(cs, &ColorState::changed, this, qOverload<>(&QWidget::update));
    setToolTip(tr("Primary / secondary color. Click the arrow to swap, the small squares to reset."));
}

void SwatchWidget::setEditingSecondary(bool s)
{
    m_secondary = s;
    update();
}

static void drawSwatch(QPainter &p, const QRect &r, const QColor &c, bool active)
{
    p.fillRect(r, QBrush(Icons::checkerboard(4)));
    p.fillRect(r, c);
    p.setPen(QColor(40, 40, 40));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r.adjusted(0, 0, -1, -1));
    p.setPen(active ? QColor(255, 255, 255) : QColor(255, 255, 255, 120));
    p.drawRect(r.adjusted(1, 1, -2, -2));
}

void SwatchWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    drawSwatch(p, secondaryRect(), m_cs->secondary, m_secondary);
    drawSwatch(p, primaryRect(), m_cs->primary, !m_secondary);
    p.drawPixmap(swapRect(), Icons::pixmap("swap", 14));
    p.drawPixmap(resetRect(), Icons::pixmap("resetcolors", 14));
}

void SwatchWidget::mousePressEvent(QMouseEvent *e)
{
    QPoint p = e->position().toPoint();
    if (swapRect().contains(p))
        m_cs->swap();
    else if (resetRect().contains(p))
        m_cs->reset();
    else if (primaryRect().contains(p))
        emit selectPrimary();
    else if (secondaryRect().contains(p))
        emit selectSecondary();
}

void SwatchWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    QPoint p = e->position().toPoint();
    bool prim = primaryRect().contains(p);
    if (!prim && !secondaryRect().contains(p))
        return;
    QColor c = QColorDialog::getColor(prim ? m_cs->primary : m_cs->secondary, this, tr("Choose Color"),
                                      QColorDialog::ShowAlphaChannel);
    if (!c.isValid())
        return;
    if (prim)
        m_cs->setPrimary(c);
    else
        m_cs->setSecondary(c);
}

// ============================================================ GradientSlider

GradientSlider::GradientSlider(int maximum, QWidget *parent) : QWidget(parent), m_max(maximum)
{
    setFixedHeight(18);
    setMinimumWidth(90);
}

void GradientSlider::setStops(const QGradientStops &stops, bool alpha)
{
    m_stops = stops;
    m_alpha = alpha;
    update();
}

void GradientSlider::setValue(int v)
{
    v = std::clamp(v, 0, m_max);
    if (v == m_value)
        return;
    m_value = v;
    update();
}

void GradientSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    QRect bar(4, 1, width() - 8, 11);
    if (m_alpha)
        p.fillRect(bar, QBrush(Icons::checkerboard(3)));
    QLinearGradient g(bar.left(), 0, bar.right(), 0);
    g.setStops(m_stops);
    p.fillRect(bar, g);
    p.setPen(QColor(100, 100, 100));
    p.drawRect(bar.adjusted(0, 0, -1, -1));
    double x = bar.left() + double(m_value) / m_max * (bar.width() - 1);
    p.setRenderHint(QPainter::Antialiasing);
    QPolygonF tri;
    tri << QPointF(x, bar.bottom() + 1) << QPointF(x - 4, height() - 0.5) << QPointF(x + 4, height() - 0.5);
    p.setPen(QColor(30, 30, 30));
    p.setBrush(Qt::black);
    p.drawPolygon(tri);
}

void GradientSlider::setFromPos(int x)
{
    int w = width() - 8;
    int v = int(std::round(double(x - 4) / std::max(1, w - 1) * m_max));
    v = std::clamp(v, 0, m_max);
    if (v != m_value) {
        m_value = v;
        update();
        emit valueChanged(v);
    }
}

void GradientSlider::mousePressEvent(QMouseEvent *e) { setFromPos(e->position().toPoint().x()); }
void GradientSlider::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons())
        setFromPos(e->position().toPoint().x());
}

// ============================================================ PaletteWidget

PaletteWidget::PaletteWidget(ColorState *cs, QWidget *parent) : QWidget(parent), m_cs(cs)
{
    connect(cs, &ColorState::paletteChanged, this, qOverload<>(&QWidget::update));
    setToolTip(tr("Left click to set primary color, right click to set secondary color.\n"
                  "Shift+click to replace a palette entry with the current color."));
}

QSize PaletteWidget::sizeHint() const
{
    int rows = (m_cs->palette.size() + Cols - 1) / Cols;
    return QSize(Cols * CellW + 1, rows * CellH + 1);
}

int PaletteWidget::indexAt(const QPoint &p) const
{
    int c = p.x() / CellW, r = p.y() / CellH;
    if (c < 0 || c >= Cols || r < 0)
        return -1;
    int i = r * Cols + c;
    return i < m_cs->palette.size() ? i : -1;
}

void PaletteWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    for (int i = 0; i < m_cs->palette.size(); ++i) {
        QRect r((i % Cols) * CellW, (i / Cols) * CellH, CellW, CellH);
        p.fillRect(r, m_cs->palette[i]);
    }
    if (assignMode) {
        p.setPen(QPen(QColor(220, 30, 30), 1, Qt::DashLine));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void PaletteWidget::mousePressEvent(QMouseEvent *e)
{
    int i = indexAt(e->position().toPoint());
    if (i >= 0)
        emit picked(i, e->button(), e->modifiers());
}

// ============================================================ ColorsPanel

ColorsPanel::ColorsPanel(ColorState *cs, QWidget *parent) : QWidget(parent), m_cs(cs)
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(10);

    // ---- left column
    auto *left = new QVBoxLayout;
    left->setSpacing(6);
    auto *top = new QHBoxLayout;
    m_which = new QComboBox;
    m_which->addItems({tr("Primary"), tr("Secondary")});
    m_which->setFixedWidth(110);
    m_more = new QPushButton(tr("More >>"));
    m_more->setFixedWidth(72);
    top->addWidget(m_which);
    top->addStretch();
    top->addWidget(m_more);
    left->addLayout(top);

    auto *mid = new QHBoxLayout;
    auto *swCol = new QVBoxLayout;
    m_swatch = new SwatchWidget(cs);
    swCol->addWidget(m_swatch);
    swCol->addStretch();
    auto *tools = new QHBoxLayout;
    tools->setSpacing(1);
    auto *addPal = new QToolButton;
    addPal->setIcon(Icons::get("addpalette"));
    addPal->setAutoRaise(true);
    addPal->setCheckable(true);
    addPal->setToolTip(tr("Add color to palette: click this, then click a palette entry"));
    auto *palMenu = new QToolButton;
    palMenu->setIcon(Icons::get("palette"));
    palMenu->setAutoRaise(true);
    palMenu->setPopupMode(QToolButton::InstantPopup);
    palMenu->setToolTip(tr("Palette"));
    palMenu->setStyleSheet("QToolButton::menu-indicator { subcontrol-position: right bottom; }");
    tools->addWidget(addPal);
    tools->addWidget(palMenu);
    swCol->addLayout(tools);
    mid->addLayout(swCol);
    m_wheel = new ColorWheel;
    m_wheel->setFixedSize(136, 136);
    mid->addWidget(m_wheel, 1, Qt::AlignCenter);
    left->addLayout(mid);

    m_palette = new PaletteWidget(cs);
    m_palette->setFixedSize(m_palette->sizeHint());
    left->addWidget(m_palette);
    left->addStretch();
    root->addLayout(left);

    // ---- expanded editors
    m_extra = new QWidget;
    auto *eg = new QGridLayout(m_extra);
    eg->setContentsMargins(0, 0, 0, 0);
    eg->setHorizontalSpacing(4);
    eg->setVerticalSpacing(3);
    const char *labels[7] = {"R:", "G:", "B:", "H:", "S:", "V:", ""};
    const int maxes[7] = {255, 255, 255, 360, 100, 100, 255};
    auto header = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setStyleSheet("color:#1e3287;");
        return l;
    };
    int row = 0;
    eg->addWidget(header(tr("RGB")), row++, 0, 1, 3);
    for (int i = 0; i < 7; ++i) {
        if (i == 3) {
            auto *hexL = new QLabel(tr("Hex:"));
            m_hex = new QLineEdit;
            m_hex->setFixedWidth(64);
            m_hex->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9A-Fa-f]{0,8}"), m_hex));
            eg->addWidget(hexL, row, 0);
            eg->addWidget(m_hex, row++, 2, Qt::AlignRight);
            eg->addWidget(header(tr("HSV")), row++, 0, 1, 3);
        }
        if (i == 6)
            eg->addWidget(header(tr("Transparency - Alpha")), row++, 0, 1, 3);
        m_sl[i] = new GradientSlider(maxes[i]);
        m_sp[i] = new QSpinBox;
        m_sp[i]->setRange(0, maxes[i]);
        m_sp[i]->setFixedWidth(56);
        if (labels[i][0])
            eg->addWidget(new QLabel(labels[i]), row, 0);
        eg->addWidget(m_sl[i], row, 1);
        eg->addWidget(m_sp[i], row++, 2);
        connect(m_sl[i], &GradientSlider::valueChanged, m_sp[i], &QSpinBox::setValue);
        connect(m_sp[i], qOverload<int>(&QSpinBox::valueChanged), this, [this, i](int v) {
            if (m_updating)
                return;
            m_sl[i]->setValue(v);
            QColor c = currentColor();
            if (i < 3) {
                int rgb[3] = {m_sp[0]->value(), m_sp[1]->value(), m_sp[2]->value()};
                c.setRgb(rgb[0], rgb[1], rgb[2], c.alpha());
                m_h = c.hsvHueF() < 0 ? m_h : c.hsvHueF() * 360;
                m_s = c.hsvSaturationF();
                m_v = c.valueF();
            } else if (i < 6) {
                m_h = m_sp[3]->value() % 360;
                m_s = m_sp[4]->value() / 100.0;
                m_v = m_sp[5]->value() / 100.0;
                c = QColor::fromHsvF(m_h / 360.0, m_s, m_v, c.alphaF());
            } else {
                c.setAlpha(v);
            }
            setCurrentColor(c);
        });
    }
    eg->setRowStretch(row, 1);
    m_extra->setVisible(false);
    root->addWidget(m_extra);

    // ---- wiring
    connect(m_which, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_secondary = i == 1;
        m_swatch->setEditingSecondary(m_secondary);
        syncFromState();
    });
    connect(m_swatch, &SwatchWidget::selectPrimary, this, [this] { m_which->setCurrentIndex(0); });
    connect(m_swatch, &SwatchWidget::selectSecondary, this, [this] { m_which->setCurrentIndex(1); });
    connect(m_more, &QPushButton::clicked, this, [this] { setExpanded(!m_expanded); });
    connect(m_wheel, &ColorWheel::hsChanged, this, [this](double h, double s) {
        m_h = h;
        m_s = s;
        if (m_v < 0.02)
            m_v = 1.0;                           // picking a hue on black should show it
        QColor c = QColor::fromHsvF(m_h / 360.0, m_s, m_v, currentColor().alphaF());
        setCurrentColor(c);
    });
    connect(m_hex, &QLineEdit::editingFinished, this, [this] {
        QString t = m_hex->text();
        bool ok;
        uint v = t.toUInt(&ok, 16);
        if (!ok)
            return;
        QColor c;
        if (t.size() == 8)
            c = QColor::fromRgba(QRgb(v));
        else
            c = QColor(QRgb(v | 0xff000000)), c.setAlpha(currentColor().alpha());
        m_h = c.hsvHueF() < 0 ? m_h : c.hsvHueF() * 360;
        m_s = c.hsvSaturationF();
        m_v = c.valueF();
        setCurrentColor(c);
    });
    connect(m_palette, &PaletteWidget::picked, this, [this, addPal](int i, Qt::MouseButton b, Qt::KeyboardModifiers mods) {
        if ((mods & Qt::ShiftModifier) || addPal->isChecked()) {
            m_cs->setPaletteColor(i, currentColor());
            addPal->setChecked(false);
            m_palette->assignMode = false;
            m_palette->update();
            return;
        }
        if (b == Qt::RightButton)
            m_cs->setSecondary(m_cs->palette[i]);
        else
            m_cs->setPrimary(m_cs->palette[i]);
    });
    connect(addPal, &QToolButton::toggled, this, [this](bool on) {
        m_palette->assignMode = on;
        m_palette->update();
    });
    auto *menu = new QMenu(palMenu);
    menu->addAction(tr("Open Palette..."), this, [this] {
        QString fn = QFileDialog::getOpenFileName(this, tr("Open Palette"), QString(), tr("Palette (*.txt *.hex *.gpl);;All files (*)"));
        if (fn.isEmpty())
            return;
        QFile f(fn);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return;
        QVector<QColor> cols;
        QTextStream ts(&f);
        while (!ts.atEnd()) {
            QString l = ts.readLine().trimmed();
            if (l.isEmpty() || l.startsWith(';') || l.startsWith('#') || l.startsWith("GIMP") || l.startsWith("Name") || l.startsWith("Columns"))
                continue;
            QStringList parts = l.split(QRegularExpression("\\s+"));
            bool ok = false;
            if (parts.size() >= 3) {
                int r = parts[0].toInt(&ok), g = parts[1].toInt(), b = parts[2].toInt();
                if (ok) {
                    cols << QColor(r, g, b);
                    continue;
                }
            }
            uint v = parts[0].toUInt(&ok, 16);
            if (ok)
                cols << (parts[0].size() == 8 ? QColor::fromRgba(v) : QColor(QRgb(v | 0xff000000)));
        }
        for (int i = 0; i < m_cs->palette.size() && i < cols.size(); ++i)
            m_cs->palette[i] = cols[i];
        emit m_cs->paletteChanged();
    });
    menu->addAction(tr("Save Palette As..."), this, [this] {
        QString fn = QFileDialog::getSaveFileName(this, tr("Save Palette"), "palette.txt", tr("Palette (*.txt)"));
        if (fn.isEmpty())
            return;
        QFile f(fn);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            return;
        QTextStream ts(&f);
        ts << "; PixelForge palette: one AARRGGBB colour per line\n";
        for (const QColor &c : m_cs->palette)
            ts << QString("%1").arg(c.rgba(), 8, 16, QChar('0')).toUpper() << "\n";
    });
    menu->addSeparator();
    menu->addAction(tr("Reset to Default"), this, [this] {
        m_cs->palette = ColorState().palette;
        emit m_cs->paletteChanged();
    });
    palMenu->setMenu(menu);

    connect(cs, &ColorState::changed, this, &ColorsPanel::syncFromState);
    syncFromState();
}

QColor ColorsPanel::currentColor() const
{
    return m_secondary ? m_cs->secondary : m_cs->primary;
}

void ColorsPanel::setCurrentColor(const QColor &c)
{
    m_updating = true;
    if (m_secondary)
        m_cs->setSecondary(c);
    else
        m_cs->setPrimary(c);
    m_updating = false;
    updateEditors();
}

void ColorsPanel::syncFromState()
{
    if (m_updating)
        return;
    QColor c = currentColor();
    if (c.hsvHueF() >= 0)
        m_h = c.hsvHueF() * 360;
    m_s = c.hsvSaturationF();
    m_v = c.valueF();
    updateEditors();
}

void ColorsPanel::updateEditors()
{
    bool was = m_updating;
    m_updating = true;
    QColor c = currentColor();
    m_wheel->setHsv(m_h, m_s, m_v);
    int vals[7] = {c.red(), c.green(), c.blue(), int(std::round(m_h)) % 360, int(std::round(m_s * 100)),
                   int(std::round(m_v * 100)), c.alpha()};
    for (int i = 0; i < 7; ++i) {
        m_sp[i]->setValue(vals[i]);
        m_sl[i]->setValue(vals[i]);
    }
    auto rgbStops = [&](int ch) {
        QColor a = c, b = c;
        a.setAlpha(255);
        b.setAlpha(255);
        if (ch == 0) { a.setRed(0); b.setRed(255); }
        if (ch == 1) { a.setGreen(0); b.setGreen(255); }
        if (ch == 2) { a.setBlue(0); b.setBlue(255); }
        return QGradientStops{{0, a}, {1, b}};
    };
    for (int i = 0; i < 3; ++i)
        m_sl[i]->setStops(rgbStops(i));
    QGradientStops hue;
    for (int i = 0; i <= 6; ++i)
        hue << QGradientStop(i / 6.0, QColor::fromHsvF(fmod(i / 6.0, 1.0), std::max(0.0, m_s) > 0.05 ? m_s : 1.0, std::max(m_v, 0.5)));
    m_sl[3]->setStops(hue);
    m_sl[4]->setStops({{0, QColor::fromHsvF(m_h / 360, 0, m_v)}, {1, QColor::fromHsvF(m_h / 360, 1, m_v)}});
    m_sl[5]->setStops({{0, Qt::black}, {1, QColor::fromHsvF(m_h / 360, m_s, 1)}});
    QColor a0 = c, a1 = c;
    a0.setAlpha(0);
    a1.setAlpha(255);
    m_sl[6]->setStops({{0, a0}, {1, a1}}, true);
    if (!m_hex->hasFocus())
        m_hex->setText(c.alpha() == 255 ? QString("%1").arg(c.rgb() & 0xffffff, 6, 16, QChar('0')).toUpper()
                                        : QString("%1").arg(c.rgba(), 8, 16, QChar('0')).toUpper());
    m_updating = was;
}

void ColorsPanel::setExpanded(bool e)
{
    m_expanded = e;
    m_extra->setVisible(e);
    m_more->setText(e ? tr("<< Less") : tr("More >>"));
    adjustSize();
    emit sizeChangedRequest();
}
