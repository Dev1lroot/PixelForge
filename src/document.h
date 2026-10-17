#pragma once

#include <QObject>
#include <QImage>
#include <QIcon>
#include <QPainterPath>
#include <QVector>

enum class BlendMode {
    Normal, Multiply, Additive, ColorBurn, ColorDodge, Reflect, Glow,
    Overlay, Difference, Negation, Lighten, Darken, Screen, Xor, Count
};

QString blendModeName(BlendMode m);
QString blendModeSvgOp(BlendMode m);
BlendMode blendModeFromSvgOp(const QString &op);

struct Layer {
    QImage image;               // ARGB32_Premultiplied, document sized
    QString name;
    bool visible = true;
    int opacity = 255;          // 0..255
    BlendMode blend = BlendMode::Normal;
};

struct DocState {
    QVector<Layer> layers;
    QPainterPath selection;
    bool hasSelection = false;
    int active = 0;
    QSize size;
};

struct HistoryItem {
    QString name;
    QIcon icon;
    DocState state;
};

// Draws one layer onto dst (premultiplied) within rect r, honouring its blend mode and opacity.
void blendLayer(QImage &dst, const Layer &layer, const QRect &r);

class Document : public QObject
{
    Q_OBJECT
public:
    Document(const QSize &size, const QColor &background, QObject *parent = nullptr);
    Document(const QVector<Layer> &layers, const QSize &size, QObject *parent = nullptr);

    QSize size() const { return m_size; }
    int width() const { return m_size.width(); }
    int height() const { return m_size.height(); }
    QRect rect() const { return QRect(QPoint(0, 0), m_size); }

    // layers (index 0 = bottom / background)
    QVector<Layer> &layers() { return m_layers; }
    const QVector<Layer> &layers() const { return m_layers; }
    Layer &layer(int i) { return m_layers[i]; }
    int layerCount() const { return m_layers.size(); }
    int activeIndex() const { return m_active; }
    void setActiveIndex(int i);
    Layer &activeLayer() { return m_layers[m_active]; }
    QImage &activeImage() { return m_layers[m_active].image; }
    QImage blankLayerImage() const;
    QString nextLayerName();

    // selection
    bool hasSelection() const { return m_hasSelection; }
    const QPainterPath &selection() const { return m_selection; }
    void setSelection(const QPainterPath &path);
    void clearSelection();
    QPainterPath selectionOrAll() const;
    QRect selectionBounds() const;          // clipped to canvas; whole canvas if no selection
    const QImage &selectionMask();          // Alpha8, antialiased
    void clipToSelection(class QPainter &p) const;

    // composite
    const QImage &composite();
    void invalidate(const QRect &r = QRect());
    QImage flattened() const;
    QImage thumbnail(int maxSide);

    // history
    void resetHistory(const QString &name, const QIcon &icon);
    void pushHistory(const QString &name, const QIcon &icon);
    void replaceTopHistory();
    const QVector<HistoryItem> &history() const { return m_history; }
    int historyIndex() const { return m_historyIndex; }
    bool canUndo() const { return m_historyIndex > 0; }
    bool canRedo() const { return m_historyIndex < m_history.size() - 1; }
    void undo();
    void redo();
    void jumpTo(int index);

    // structural edits (caller pushes history afterwards)
    void setLayers(const QVector<Layer> &layers, const QSize &size, int active);
    void notifyLayersChanged();
    void notifyStructureChanged();

    // file
    QString filePath;
    double dpi = 96.0;
    QString displayName() const;
    bool isDirty() const { return m_historyIndex != m_savedIndex; }
    void markSaved();
    DocState captureState() const;

signals:
    void imageChanged(const QRect &r);
    void layersChanged();
    void selectionChanged();
    void historyChanged();
    void sizeChanged();
    void dirtyChanged();
    void stateRestored();

private:
    void restoreState(const DocState &s);

    QSize m_size;
    QVector<Layer> m_layers;
    int m_active = 0;
    QPainterPath m_selection;
    bool m_hasSelection = false;
    QImage m_selMask;
    bool m_selMaskValid = false;

    QImage m_composite;
    QRect m_dirty;

    QVector<HistoryItem> m_history;
    int m_historyIndex = -1;
    int m_savedIndex = 0;
    int m_layerCounter = 1;
};
