#pragma once

#include <QObject>
#include <QCursor>
#include <QIcon>
#include <QImage>
#include <QPainterPath>
#include <QTransform>
#include <QVector>
#include <QFont>

class MainWindow;
class Document;
class CanvasView;
class ToolSettings;
class ColorState;
class QPainter;
class QKeyEvent;

enum class ToolId {
    RectSelect, Move, Lasso, MoveSelection, EllipseSelect, Zoom, MagicWand, Pan,
    Bucket, Gradient, Brush, Eraser, Pencil, ColorPicker, CloneStamp, Recolor,
    Text, Line, Rectangle, RoundRect, Ellipse, Freeform, Count
};

enum ToolOption : quint32 {
    OptBrushWidth = 1 << 0,
    OptAntialias = 1 << 1,
    OptAlphaBlend = 1 << 2,
    OptFill = 1 << 3,
    OptShapeType = 1 << 4,
    OptTolerance = 1 << 5,
    OptFloodMode = 1 << 6,
    OptSelectionMode = 1 << 7,
    OptGradient = 1 << 8,
    OptSampling = 1 << 9,
    OptFont = 1 << 10,
    OptLineStyle = 1 << 11,
    OptRadius = 1 << 12,
    OptResample = 1 << 13,
    OptHardness = 1 << 14,
};

struct ToolEvent {
    QPointF pos;                 // image coordinates (sub-pixel)
    QPointF viewPos;             // viewport coordinates
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons;
    Qt::KeyboardModifiers mods;
};

class Tool : public QObject
{
    Q_OBJECT
public:
    explicit Tool(MainWindow *m) : main(m) {}

    virtual ToolId id() const = 0;
    virtual QString name() const = 0;
    virtual QString iconName() const = 0;
    virtual QString helpText() const = 0;
    virtual QChar shortcut() const = 0;
    virtual quint32 options() const { return 0; }
    virtual QCursor cursor() const;

    virtual void activate() {}
    virtual void deactivate() { commit(); }
    virtual void commit() {}                 // finalise pending work (pushes history)
    virtual void reset() {}                  // drop transient state without history
    // called before undo/redo/history jumps: tools whose pending state is already the
    // top history item just drop it, others finalise first
    virtual void prepareForHistory() { commit(); }
    virtual bool isEditing() const { return false; }
    // how pointer positions are delivered in tile view: raw (may lie on a neighbour tile),
    // or wrapped back into the canvas for click-to-sample tools
    enum TileBehavior { TileRaw, TileWrap };
    virtual TileBehavior tileBehavior() const { return TileRaw; }
    virtual bool wantsKeys() const { return false; }

    virtual void mousePress(CanvasView *, const ToolEvent &) {}
    virtual void mouseMove(CanvasView *, const ToolEvent &) {}
    virtual void mouseRelease(CanvasView *, const ToolEvent &) {}
    virtual void mouseDoubleClick(CanvasView *, const ToolEvent &) {}
    virtual bool keyPress(QKeyEvent *) { return false; }
    virtual void paintOverlay(QPainter &, CanvasView *) {}

    QIcon icon() const;

protected:
    Document *doc() const;
    CanvasView *view() const;
    ToolSettings &settings() const;
    ColorState &colors() const;
    void setStatus(const QString &s) const;

    MainWindow *main;
    bool m_hooked = false;                   // live-update signal hooks installed
};

QVector<Tool *> createTools(MainWindow *main);

// helpers shared with menu commands
QImage floodMask(const QImage &src, QPoint seed, int tolerancePct, bool global, const QImage *limitMask = nullptr);
QPainterPath maskToPath(const QImage &mask);
QVector<QPointF> tileOffsets(const Document *d, const ToolSettings &s);
QPainterPath combineSelection(const QPainterPath &oldSel, bool hadSel, const QPainterPath &newPath, int mode);

// Interface the Move Selected Pixels tool exposes for paste
class MoveTool;
void movetoolBeginPaste(Tool *moveTool, const QImage &img, const QPoint &pos);
