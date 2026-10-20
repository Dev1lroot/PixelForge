#pragma once
// 3D model preview: shows the current image as the texture of a loaded model and follows every edit.
// Each material of the model is a texture slot; its colour and normal map can come from the whole
// image, any layer of it, or the model's own texture.

#include "pluginapi.h"
#include "meshrender.h"
#include "convert.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QSlider;
class QSplitter;
class QGridLayout;
class QLabel;
class QScrollArea;

namespace ModelView {

class View : public QWidget
{
    Q_OBJECT
public:
    explicit View(QWidget *parent = nullptr);
    void setModel(const Model *model);              // not owned; nullptr shows the hint
    void setTextures(const QVector<MaterialTextures> &textures);
    RenderParams &params() { return m_params; }
    void setAutoRotate(bool on);
    void resetView();
    void changed();                                  // re-render on the next paint
    void setBusy(const QString &text);               // overlay message, e.g. while converting
    QSize sizeHint() const override { return QSize(280, 240); }

signals:
    void fileDropped(const QString &path);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

private:
    const Model *m_model = nullptr;
    QString m_busy;
    QVector<MaterialTextures> m_textures;
    RenderParams m_params;
    QImage m_frame;
    bool m_dirty = true, m_frameDraft = false;
    bool m_allHidden = false;                        // every material is set to (none)
    QTimer m_rotate, m_settle;
    bool m_autoRotate = false;
    Qt::MouseButton m_drag = Qt::NoButton;
    QPointF m_last;
};

class Panel : public QWidget
{
    Q_OBJECT
public:
    explicit Panel(PixelForge::Host *host, QWidget *parent = nullptr);
    void reload();                                   // the document changed
    Q_INVOKABLE bool openModel(const QString &path, bool interactive);
    const Model &model() const { return m_model; }
    View *view() const { return m_view; }
    void paintUvOverlay(QPainter &p, const QTransform &imageToView);
    QImage floodRegions(const QSize &size);           // UV islands labelled 1.., gaps 0 (UV Map flood mode)

protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

private:
    struct Slot {
        QString base, normal;                        // source keys, see sourceImage()
        QComboBox *baseCombo = nullptr, *normalCombo = nullptr;
    };

    void setModel(Model m, const QString &path);
    void conversionFinished(const QString &source, const QString &glb, const QString &error);
    void showError(const QString &path, const QString &error, bool interactive);
    void rebuildSlots();
    void rebuildUvOverlay();
    bool showsUVs(int material, const QString &activeLayer) const;
    void fillCombo(QComboBox *combo, bool normal, int material, const QString &key);
    QString defaultKey(int material, bool normal) const;
    QImage sourceImage(const QString &key, int material, bool normal);
    void updateTextures();
    void updateInfo();

    PixelForge::Host *m_host;
    View *m_view;
    QSplitter *m_splitter;
    QCheckBox *m_showUVs;
    QSlider *m_uvOpacity;
    QLabel *m_uvLabel;
    QComboBox *m_uvCombo;
    QVector<QVector<QLineF>> m_uvLines;              // per material: UV layout in 0..1 image space (y down)
    QString m_activeLayer;                           // name of the document's active layer
    int m_modelGeneration = 0;                       // bumped on every (re)load
    QString m_regionKey;                             // what m_regions was computed for
    QImage m_regions;
    QLabel *m_info;
    QScrollArea *m_slotArea;
    QWidget *m_slotWidget;
    QGridLayout *m_slotGrid;
    Model m_model;
    QVector<Slot> m_slots;
    QStringList m_layerNames;
    QVector<PixelForge::LayerInfo> m_layers;
    QHash<qint64, QImage> m_converted, m_convertedNext;   // ARGB32 copies by source cacheKey
    QFileSystemWatcher m_watcher;
    QTimer m_reloadModel;
    QString m_error;
    Conversion m_conversion;
    QString m_converting;                            // source being converted
    bool m_convertInteractive = false;
    bool m_triedLast = false;
};

} // namespace ModelView

class ModelViewPlugin : public QObject, public PixelForge::Plugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PixelForgePlugin_iid FILE "modelview.json")
    Q_INTERFACES(PixelForge::Plugin)
public:
    QString name() const override { return "3D Model Preview"; }
    QString version() const override { return "1.0"; }
    QString description() const override
    {
        return "Shows the image as the texture of a 3D model (OBJ, FBX, glTF, GLB, STL, PLY) and updates while you paint. "
               "Each material can take the whole image or any layer as its colour or normal map. Other formats "
               "(DAE, 3DS, BLEND, USD, ...) are converted with an installed Blender or assimp.";
    }
    void initialize(PixelForge::Host *host) override;
};
