#pragma once

#include "pluginapi.h"
#include <QObject>
#include <QPointer>
#include <QTimer>

class MainWindow;
class Document;

struct LoadedPlugin {
    QString file;
    QString name, version, description;
    QString error;               // non-empty when loading failed
};

class PluginHost : public QObject, public PixelForge::Host
{
    Q_OBJECT
public:
    explicit PluginHost(MainWindow *main);

    void loadAll();
    const QVector<LoadedPlugin> &plugins() const { return m_plugins; }
    static QStringList searchPaths();
    void setDocument(Document *doc);

    // PixelForge::Host
    QString applicationVersion() const override;
    bool hasDocument() const override;
    QString documentName() const override;
    QSize documentSize() const override;
    QVector<PixelForge::LayerInfo> layers() const override;
    int activeLayer() const override;
    QImage flattenedImage() const override;
    void onDocumentChanged(QObject *context, std::function<void()> callback) override;
    QWidget *addPanel(const QString &id, const QString &title, QWidget *content, const QKeySequence &toggle,
                      const QIcon &icon) override;
    void addMenuAction(const QString &menu, QAction *action) override;
    QWidget *mainWindow() const override;

signals:
    void documentChanged();

private:
    MainWindow *m_main;
    QPointer<Document> m_doc;
    QTimer m_debounce;
    QVector<LoadedPlugin> m_plugins;
};
