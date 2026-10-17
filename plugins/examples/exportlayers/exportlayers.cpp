#include "exportlayers.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QRegularExpression>

namespace ExportLayers {

QString safeFileName(const QString &name)
{
    static const QRegularExpression bad(R"([\\/:*?"<>|\x00-\x1f])");
    QString s = name.trimmed();
    s.replace(bad, "_");
    return s.isEmpty() ? QStringLiteral("layer") : s;
}

QStringList exportLayers(const QVector<PixelForge::LayerInfo> &layers, const QString &dir, bool visibleOnly)
{
    QStringList written;
    for (int i = 0; i < layers.size(); ++i) {
        const PixelForge::LayerInfo &l = layers[i];
        if (visibleOnly && !l.visible)
            continue;
        const QString path = QDir(dir).filePath(QString("%1-%2.png").arg(i + 1, 2, 10, QChar('0')).arg(safeFileName(l.name)));
        // layer opacity is a display property; bake it in so the PNG looks like the layer does
        QImage img = l.image;
        if (l.opacity < 255) {
            img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < img.height(); ++y) {
                QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
                for (int x = 0; x < img.width(); ++x) {
                    const QRgb p = row[x];
                    auto f = [&](int c) { return c * l.opacity / 255; };
                    row[x] = qRgba(f(qRed(p)), f(qGreen(p)), f(qBlue(p)), f(qAlpha(p)));
                }
            }
        }
        if (img.save(path, "PNG"))
            written << path;
    }
    return written;
}

} // namespace ExportLayers

void ExportLayersPlugin::initialize(PixelForge::Host *host)
{
    auto *action = new QAction(tr("Export &Layers as PNG..."), host->mainWindow());
    action->setObjectName("example_exportlayers");
    connect(action, &QAction::triggered, host->mainWindow(), [host] {
        QWidget *parent = host->mainWindow();
        if (!host->hasDocument())
            return;
        const QString dir = QFileDialog::getExistingDirectory(parent, tr("Export Layers To"));
        if (dir.isEmpty())
            return;
        const bool visibleOnly =
            QMessageBox::question(parent, tr("Export Layers"), tr("Skip hidden layers?")) == QMessageBox::Yes;
        const QStringList files = ExportLayers::exportLayers(host->layers(), dir, visibleOnly);
        QMessageBox::information(parent, tr("Export Layers"), tr("Wrote %n file(s) to %1.", nullptr, files.size()).arg(dir));
    });
    host->addMenuAction("File", action);
}
