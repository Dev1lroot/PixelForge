#include "mainwindow.h"
#include "document.h"
#include "icons.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("PixelForge");
    QApplication::setOrganizationName("PixelForge");
    QApplication::setApplicationVersion("1.1");
    QApplication::setDesktopFileName("pixelforge");

    QCommandLineParser parser;
    parser.setApplicationDescription("PixelForge image editor");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("files", "Images to open", "[files...]");
    parser.process(app);

    MainWindow w;
    const QStringList files = parser.positionalArguments();
    if (files.isEmpty()) {
        auto *doc = new Document(QSize(800, 600), Qt::white);
        doc->resetHistory(QObject::tr("New Image"), Icons::get("new"));
        w.addDocument(doc);
    } else {
        for (const QString &f : files)
            w.openFile(f);
    }
    w.show();
    // developer hook: PIXELFORGE_SCREENSHOT=/path.png grabs the window once laid out, then exits
    const QString shot = qEnvironmentVariable("PIXELFORGE_SCREENSHOT");
    if (!shot.isEmpty())
        QTimer::singleShot(1200, &w, [&w, shot] {
            w.grab().save(shot);
            QApplication::quit();
        });
    return app.exec();
}
