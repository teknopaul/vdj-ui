#include <QApplication>
#include <QIcon>
#include <QStringList>
#include "ui/main_window.h"
#include "debug/packet_logger.h"
#include "dbserver/rb_mode_options.h"
#include <cstdio>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("vdj-ui");
    // The window / taskbar icon, also when run from the build tree: the icons are compiled in. The desktop file name is how Wayland and GNOME
    // match the window to data/vdj-ui.desktop (and its installed hicolor icons, see `make install-icons`).
    {
        QIcon icon;
        for (int sz : {16, 22, 24, 32, 64, 128, 256, 512}) icon.addFile(QStringLiteral(":/img/vdj-%1x%1.png").arg(sz), QSize(sz, sz));
        app.setWindowIcon(icon);
        QGuiApplication::setDesktopFileName(QStringLiteral("vdj-ui"));
    }
    app.setApplicationVersion(QStringLiteral(VDJ_UI_VERSION));

    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--version")) || args.contains(QStringLiteral("-V"))) {
        std::printf("vdj-ui %s\n", VDJ_UI_VERSION);
        return 0;
    }
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