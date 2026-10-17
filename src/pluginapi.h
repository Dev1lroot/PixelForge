#pragma once
// PixelForge plugin API. Plugins are Qt plugins (shared libraries) implementing PixelForge::Plugin.
// They only need this header and Qt; they never link against the editor itself.

#include <QtPlugin>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QString>
#include <QVector>
#include <functional>

class QAction;
class QObject;
class QWidget;

namespace PixelForge {

struct LayerInfo {
    QString name;
    QImage image;                // Format_ARGB32_Premultiplied, document sized
    bool visible = true;
    int opacity = 255;           // 0..255
    QString blendMode;           // human readable, e.g. "Normal", "Multiply"
};

class Host
{
public:
    virtual ~Host() = default;
    virtual QString applicationVersion() const = 0;

    virtual bool hasDocument() const = 0;
    virtual QString documentName() const = 0;
    virtual QSize documentSize() const = 0;
    virtual QVector<LayerInfo> layers() const = 0;       // bottom to top
    virtual int activeLayer() const = 0;
    virtual QImage flattenedImage() const = 0;

    // Called (coalesced) whenever pixels or layers of the current document change, or another
    // document becomes current. The connection lives as long as `context`.
    virtual void onDocumentChanged(QObject *context, std::function<void()> callback) = 0;

    // Adds a floating tool window with a toggle in the Window menu. Returns the window.
    virtual QWidget *addPanel(const QString &id, const QString &title, QWidget *content,
                              const QKeySequence &toggle, const QIcon &icon) = 0;
    // Adds an action to a top-level menu: "File", "Edit", "View", "Image", "Layers",
    // "Adjustments", "Effects", "Window" or "Help".
    virtual void addMenuAction(const QString &menu, QAction *action) = 0;
    virtual QWidget *mainWindow() const = 0;
};

class Plugin
{
public:
    virtual ~Plugin() = default;
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual QString description() const = 0;
    virtual void initialize(Host *host) = 0;
};

} // namespace PixelForge

#define PixelForgePlugin_iid "org.pixelforge.Plugin/1"
Q_DECLARE_INTERFACE(PixelForge::Plugin, PixelForgePlugin_iid)
