#pragma once
// Example of a File menu command that reads every layer from the host.

#include "pluginapi.h"

#include <QObject>

namespace ExportLayers {
// "Layer 1/2" -> "Layer 1_2": strip characters that are unsafe in file names
QString safeFileName(const QString &name);
// writes <dir>/<NN>-<layer name>.png for each layer (bottom first); returns the files written
QStringList exportLayers(const QVector<PixelForge::LayerInfo> &layers, const QString &dir, bool visibleOnly);
} // namespace ExportLayers

class ExportLayersPlugin : public QObject, public PixelForge::Plugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PixelForgePlugin_iid FILE "exportlayers.json")
    Q_INTERFACES(PixelForge::Plugin)
public:
    QString name() const override { return "Export Layers"; }
    QString version() const override { return "1.0"; }
    QString description() const override { return "Example plugin: saves every layer as a separate PNG."; }
    void initialize(PixelForge::Host *host) override;
};
