#include "settings.h"

#include <QBrush>
#include <QPainter>
#include <QPixmap>

static QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t,
                            a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF() * (1 - t) + b.blueF() * t);
}

ColorState::ColorState()
{
    static const QRgb hues[14] = {0xFF0000, 0xFF6A00, 0xFFD800, 0xB6FF00, 0x4CFF00, 0x00FF21, 0x00FF90,
                                  0x00FFFF, 0x0094FF, 0x0026FF, 0x4800FF, 0xB200FF, 0xFF00DC, 0xFF006E};
    static const int grays[6][2] = {{0, 255}, {64, 128}, {48, 160}, {32, 192}, {96, 224}, {16, 208}};
    // 6 rows x 16 columns = 96 colours
    const double shade[6] = {0.0, -0.5, 0.5, 0.75, -0.25, 0.25};
    for (int row = 0; row < 6; ++row) {
        palette.append(QColor(grays[row][0], grays[row][0], grays[row][0]));
        palette.append(QColor(grays[row][1], grays[row][1], grays[row][1]));
        for (int i = 0; i < 14; ++i) {
            QColor base(hues[i]);
            double s = shade[row];
            palette.append(s < 0 ? mix(base, Qt::black, -s) : mix(base, Qt::white, s));
        }
    }
}

void ColorState::setPrimary(const QColor &c)
{
    if (c == primary)
        return;
    primary = c;
    emit changed();
}

void ColorState::setSecondary(const QColor &c)
{
    if (c == secondary)
        return;
    secondary = c;
    emit changed();
}

void ColorState::swap()
{
    std::swap(primary, secondary);
    emit changed();
}

void ColorState::reset()
{
    primary = Qt::black;
    secondary = Qt::white;
    emit changed();
}

void ColorState::setPaletteColor(int i, const QColor &c)
{
    if (i < 0 || i >= palette.size())
        return;
    palette[i] = c;
    emit paletteChanged();
}

static const Qt::BrushStyle kHatches[] = {
    Qt::SolidPattern, Qt::HorPattern, Qt::VerPattern, Qt::CrossPattern, Qt::BDiagPattern,
    Qt::FDiagPattern, Qt::DiagCrossPattern, Qt::Dense1Pattern, Qt::Dense2Pattern, Qt::Dense3Pattern,
    Qt::Dense4Pattern, Qt::Dense5Pattern, Qt::Dense6Pattern, Qt::Dense7Pattern};

int fillStyleCount() { return int(sizeof(kHatches) / sizeof(kHatches[0])); }

Qt::BrushStyle fillStyleBrush(int index)
{
    if (index < 0 || index >= fillStyleCount())
        return Qt::SolidPattern;
    return kHatches[index];
}

QString fillStyleName(int index)
{
    static const char *names[] = {"Solid Color", "Horizontal", "Vertical", "Cross", "Backward Diagonal",
                                  "Forward Diagonal", "Diagonal Cross", "90 Percent", "75 Percent",
                                  "62 Percent", "50 Percent", "37 Percent", "25 Percent", "12 Percent"};
    if (index < 0 || index >= fillStyleCount())
        return QString();
    return QObject::tr(names[index]);
}

QBrush makeFillBrush(int fillStyle, const QColor &fg, const QColor &bg)
{
    Qt::BrushStyle st = fillStyleBrush(fillStyle);
    if (st == Qt::SolidPattern)
        return QBrush(fg);
    // pattern drawn in primary over secondary, tiled in image space
    QPixmap tile(8, 8);
    tile.fill(bg);
    QPainter p(&tile);
    p.fillRect(tile.rect(), QBrush(fg, st));
    p.end();
    return QBrush(tile);
}
