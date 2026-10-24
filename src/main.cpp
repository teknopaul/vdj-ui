#include <QApplication>
#include <QStringList>
#include "ui/main_window.h"
#include "debug/packet_logger.h"
#include "dbserver/rb_mode_options.h"
#include <cstdio>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("vdj-ui");
    app.setApplicationVersion("0.1.0");

    const QStringList args = app.arguments();
    const bool debug = args.contains(QStringLiteral("--debug"));

    vdj::RbModeOptions rb;
    QString rbError;
    if (!vdj::parseRbModeArgs(args, &rb, &rbError)) {
        std::fprintf(stderr, "vdj-ui: %s\n", qPrintable(rbError));
        return 2;
    }

    vdj::MainWindow window;
    window.show();
    if (args.contains(QStringLiteral("--mock-beat")))
        window.addDemoDecks();
    if (debug) {
        window.setDebugModeFromCli();
    }
    // --link-export <folder>: serve that pendrive / export as a LINK player once connected (also in the Network menu)
    {
        const int i = args.indexOf(QStringLiteral("--link-export"));
        if (i >= 0 && i + 1 < args.size()) window.setLinkExport(args.at(i + 1));
    }
    // --rekordbox-mode <export>: open the rekordbox PC mode panel and start serving (see doc/rekordbox-mode.md)
    if (!rb.exportRoot.isEmpty())
        window.startRekordboxMode(rb);
    return app.exec();
}