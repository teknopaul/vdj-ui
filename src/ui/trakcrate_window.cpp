#include "trakcrate_window.h"

#include <QActionGroup>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>

#include "audio/quantize_settings.h"
#include "config/config_manager.h"
#include "pendrive_clone_dialog.h"
#include "trakcrate_panel.h"
#include "trakcrate_scripts.h"
#include "trakcrate_track_editor.h"

namespace vdj {

TrakcrateWindow::TrakcrateWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(windowTitleText());
    resize(1200, 720);
    config_ = new ConfigManager(this);
    quantize_ = new QuantizeSettings(config_, this);

    panel_ = new TrakcratePanel(config_);
    setCentralWidget(panel_);
    panel_->setSidebarWidth(340);                                     // twice the width of the tree in the dock of vdj-ui
    panel_->setLoadActionText(QStringLiteral("Edit track"));          // there is no player here: the same action (and the double click) opens the editor
    connect(panel_, &TrakcratePanel::status, this, [this](const QString& m) { statusBar()->showMessage(m, 6000); });

    editor_ = new TrakcrateTrackEditor;
    editor_->setConfig(config_);
    editor_->setScripts(panel_->scripts());
    editorDock_ = new QDockWidget(QStringLiteral("trakcrate track"), this);
    editorDock_->setObjectName(QStringLiteral("trakcrateTrackDock"));
    editorDock_->setWidget(editor_);
    addDockWidget(Qt::BottomDockWidgetArea, editorDock_);
    editorDock_->hide();
    connect(panel_, &TrakcratePanel::loadRequested, this, [this](const QString& dir) {
        editor_->setTrack(dir);
        editorDock_->show();
    });
    connect(editor_, &TrakcrateTrackEditor::trackChanged, panel_, &TrakcratePanel::refresh);
    connect(panel_, &TrakcratePanel::ratingEdited, this, [this](const QString& dir) {
        if (editor_->trackDir() == dir) editor_->setTrack(dir);
    });
    connect(editor_, &TrakcrateTrackEditor::status, this, [this](const QString& m) { statusBar()->showMessage(m, 8000); });

    setupMenus();
}

void TrakcrateWindow::setupMenus()
{
    QMenu* file = menuBar()->addMenu(QStringLiteral("&File"));
    QAction* clone = file->addAction(QStringLiteral("&Clone Pendrive..."));
    connect(clone, &QAction::triggered, this, [this] {
        PendriveCloneDialog dlg(this);
        dlg.exec();
    });
    file->addSeparator();
    QAction* quit = file->addAction(QStringLiteral("&Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QMainWindow::close);

    QMenu* configMenu = menuBar()->addMenu(QStringLiteral("&Config"));
    QMenu* quantizeMenu = configMenu->addMenu(QStringLiteral("&Quantize"));
    auto* group = new QActionGroup(this);
    for (auto r : {QuantizeSettings::Beat, QuantizeSettings::Half, QuantizeSettings::Quarter}) {
        QAction* a = quantizeMenu->addAction(QuantizeSettings::label(r));
        a->setCheckable(true);
        a->setChecked(quantize_->resolution() == r);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, r] { quantize_->setResolution(r); });
    }
    configMenu->addSeparator();
    QAction* quitConfig = configMenu->addAction(QStringLiteral("&Quit"));
    connect(quitConfig, &QAction::triggered, this, &QMainWindow::close);
}

} // namespace vdj
