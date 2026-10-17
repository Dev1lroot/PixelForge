#pragma once
// Smallest useful plugin: one menu action that reads from the host.

#include "pluginapi.h"

#include <QObject>

class HelloPlugin : public QObject, public PixelForge::Plugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PixelForgePlugin_iid FILE "hello.json")
    Q_INTERFACES(PixelForge::Plugin)
public:
    QString name() const override { return "Hello"; }
    QString version() const override { return "1.0"; }
    QString description() const override { return "Minimal example plugin: a Help menu entry that shows document info."; }
    void initialize(PixelForge::Host *host) override;
};
