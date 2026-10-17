#include "hello.h"

#include <QAction>
#include <QMessageBox>

void HelloPlugin::initialize(PixelForge::Host *host)
{
    // the action is parented to the main window, so it lives as long as the editor
    auto *action = new QAction(tr("Say &Hello..."), host->mainWindow());
    action->setObjectName("example_hello");
    connect(action, &QAction::triggered, host->mainWindow(), [host] {
        QString text;
        if (!host->hasDocument()) {
            text = tr("No image is open.");
        } else {
            const QSize s = host->documentSize();
            text = tr("<b>%1</b><br>%2 × %3 pixels, %4 layer(s)<br>active layer: %5")
                       .arg(host->documentName().toHtmlEscaped())
                       .arg(s.width())
                       .arg(s.height())
                       .arg(host->layers().size())
                       .arg(host->activeLayer() + 1);
        }
        QMessageBox::information(host->mainWindow(), tr("Hello from a plugin"),
                                 tr("PixelForge %1<br><br>%2").arg(host->applicationVersion(), text));
    });
    host->addMenuAction("Help", action);
}
