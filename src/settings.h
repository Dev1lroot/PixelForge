#pragma once

#include <QObject>
#include <QColor>
#include <QFont>
#include <QVector>

enum class SelectionMode { Replace, Union, Exclude, Intersect, Xor };
enum class ShapeType { Outline, Filled, FilledOutline };
enum class GradientType { Linear, LinearReflected, Diamond, Radial, Conical };
enum class AfterPick { DoNothing, PreviousTool, Pencil };
enum class Units { Pixels, Inches, Centimeters };
// Magic Wand / Paint Bucket: similar connected colours, similar colours anywhere, or a UV region
enum class FloodMode { Contiguous, Global, UVMap };

// Settings shared across all tools (exactly like the classic editor: brush width,
// antialiasing etc. are global and shown in the tool options bar when relevant).
class ToolSettings : public QObject
{
    Q_OBJECT
public:
    int brushWidth = 2;
    int hardness = 100;                 // percent: 100 = hard edge, lower = soft falloff
    bool antialias = true;
    bool alphaBlend = true;
    int fillStyle = 0;                  // 0 = solid, otherwise index into hatch list
    ShapeType shapeType = ShapeType::Outline;
    int tolerance = 50;                 // percent
    FloodMode floodMode = FloodMode::Contiguous;
    SelectionMode selectionMode = SelectionMode::Replace;
    GradientType gradientType = GradientType::Linear;
    bool gradientTransparency = false;
    int sampling = 0;                   // 0=single pixel, 1=3x3, 2=5x5, 3=7x7, 4=9x9
    bool sampleImage = false;
    AfterPick afterPick = AfterPick::DoNothing;
    QString fontFamily = "Sans Serif";
    int fontSize = 12;
    bool bold = false, italic = false, underline = false, strike = false;
    int textAlign = 0;                  // 0 left, 1 center, 2 right
    int lineStyle = 0;                  // index into dash style list
    int arrowStart = 0, arrowEnd = 0;
    int radius = 20;
    bool smoothResample = true;
    Units units = Units::Pixels;
    bool tileMode = false;              // tile view: paint wraps across canvas edges

    void notify() { emit changed(); }

signals:
    void changed();
};

class ColorState : public QObject
{
    Q_OBJECT
public:
    QColor primary = Qt::black;
    QColor secondary = Qt::white;
    QVector<QColor> palette;

    ColorState();
    void setPrimary(const QColor &c);
    void setSecondary(const QColor &c);
    void swap();
    void reset();
    void setPaletteColor(int i, const QColor &c);

signals:
    void changed();
    void paletteChanged();
};

Qt::BrushStyle fillStyleBrush(int index);
QString fillStyleName(int index);
int fillStyleCount();
QBrush makeFillBrush(int fillStyle, const QColor &fg, const QColor &bg);
