#ifndef TRAKCRATE_WINDOW_H
#define TRAKCRATE_WINDOW_H

#include <QMainWindow>

class QDockWidget;

namespace vdj {

class ConfigManager;
class QuantizeSettings;
class TrakcratePanel;
class TrakcrateTrackEditor;

// The trakcrate-only application (started as `trakcrate-ui`, or `vdj-ui --trakcrate`): the library view in a window of its own, without the
// decks, the network, MIDI or audio sequencer of MainWindow. Its menus are built in setupMenus() and nowhere else, so what this view offers is
// decided in one place.
class TrakcrateWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit TrakcrateWindow(QWidget* parent = nullptr);

    TrakcratePanel* panel() const { return panel_; }
    TrakcrateTrackEditor* editor() const { return editor_; }
    static QString windowTitleText() { return QStringLiteral("\U0001F4E6 trakcrate"); }

private:
    void setupMenus();

    ConfigManager* config_;
    QuantizeSettings* quantize_;
    TrakcratePanel* panel_;
    TrakcrateTrackEditor* editor_;
    QDockWidget* editorDock_;
};

} // namespace vdj

#endif
