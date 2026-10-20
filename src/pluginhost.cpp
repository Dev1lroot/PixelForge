#include "pluginhost.h"
#include "mainwindow.h"
#include "document.h"
#include "panels.h"

#include <QApplication>
#include <QDir>
#include <QPluginLoader>
#include <QStandardPaths>
#include <QSet>

PluginHost::PluginHost(MainWindow *main) : QObject(main), m_main(main)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(60);
    connect(&m_debounce, &QTimer::timeout, this, &PluginHost::documentChanged);
}

QStringList PluginHost::searchPaths()
{
    QStringList dirs;
    for (const QString &p : qEnvironmentVariable("PIXELFORGE_PLUGIN_PATH").split(':', Qt::SkipEmptyParts))
        dirs << p;
    const QString app = QCoreApplication::applicationDirPath();
    dirs << app + "/plugins" << app + "/../lib/pixelforge/plugins";
    dirs << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/pixelforge/plugins";
    return dirs;
}

void PluginHost::loadAll()
{
    QSet<QString> seen;
    for (const QString &dirPath : searchPaths()) {
        QDir dir(dirPath);
        if (!dir.exists())
            continue;
        for (const QString &f : dir.entryList({"*.so", "*.dylib", "*.dll"}, QDir::Files)) {
            QString path = dir.canonicalPath() + "/" + f;
            if (seen.contains(f))
                continue;                 // first location wins (user paths override bundled ones)
            seen.insert(f);
            LoadedPlugin info;
            info.file = path;
            QPluginLoader loader(path);
            QObject *obj = loader.instance();
            auto *plugin = qobject_cast<PixelForge::Plugin *>(obj);
            if (!plugin) {
                info.error = obj ? tr("Not a PixelForge plugin") : loader.errorString();
                m_plugins.append(info);
                continue;
            }
            info.name = plugin->name();
            info.version = plugin->version();
            info.description = plugin->description();
            plugin->initialize(this);
            m_plugins.append(info);
        }
    }
}

void PluginHost::setDocument(Document *doc)
{
    if (m_doc)
        disconnect(m_doc, nullptr, this, nullptr);
    m_doc = doc;
    if (doc) {
        connect(doc, &Document::imageChanged, this, [this] { m_debounce.start(); });
        connect(doc, &Document::layersChanged, this, [this] { m_debounce.start(); });
        connect(doc, &Document::sizeChanged, this, [this] { m_debounce.start(); });
    }
    m_debounce.start();
}

QString PluginHost::applicationVersion() const { return QCoreApplication::applicationVersion(); }
bool PluginHost::hasDocument() const { return m_doc; }
QString PluginHost::documentName() const { return m_doc ? m_doc->displayName() : QString(); }
QSize PluginHost::documentSize() const { return m_doc ? m_doc->size() : QSize(); }
int PluginHost::activeLayer() const { return m_doc ? m_doc->activeIndex() : -1; }

QVector<PixelForge::LayerInfo> PluginHost::layers() const
{
    QVector<PixelForge::LayerInfo> out;
    if (!m_doc)
        return out;
    for (const Layer &l : m_doc->layers()) {
        PixelForge::LayerInfo i;
        i.name = l.name;
        i.image = l.image;               // implicitly shared, no copy
        i.visible = l.visible;
        i.opacity = l.opacity;
        i.blendMode = blendModeName(l.blend);
        out.append(i);
    }
    return out;
}

QImage PluginHost::flattenedImage() const
{
    return m_doc ? m_doc->composite() : QImage();
}

void PluginHost::onDocumentChanged(QObject *context, std::function<void()> callback)
{
    connect(this, &PluginHost::documentChanged, context, [callback] { callback(); });
}

QWidget *PluginHost::addPanel(const QString &id, const QString &title, QWidget *content, const QKeySequence &toggle,
                              const QIcon &icon)
{
    return m_main->addFloatingPanel("plugin_" + id, title, content, toggle, icon, false, QSize(300, 430));
}

void PluginHost::addMenuAction(const QString &menu, QAction *action)
{
    m_main->addPluginAction(menu, action);
}

QWidget *PluginHost::mainWindow() const { return m_main; }

void PluginHost::addCanvasOverlay(QObject *context, std::function<void(QPainter &, const QTransform &)> paint)
{
    m_main->addCanvasOverlay(context, std::move(paint));
}

void PluginHost::updateCanvas() { m_main->updateCanvases(); }

void PluginHost::setFloodRegions(QObject *context, std::function<QImage(const QSize &)> regions)
{
    m_main->setFloodRegions(context, std::move(regions));
}
