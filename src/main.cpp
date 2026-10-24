#include <QApplication>
#include <QStringList>
#include "ui/main_window.h"
#include "debug/packet_logger.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("vdj-ui");
    app.setApplicationVersion("0.1.0");

    const QStringList args = app.arguments();
    const bool debug = args.contains(QStringLiteral("--debug"));

    vdj::MainWindow window;
    window.show();
    if (args.contains(QStringLiteral("--mock-beat")))
        window.addDemoDecks();
    if (debug) {
        window.setDebugModeFromCli();
    }
    return app.exec();
}