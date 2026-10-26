#ifndef DJTOOLZ_PANEL_H
#define DJTOOLZ_PANEL_H

#include <QList>
#include <QModelIndex>
#include <QPoint>
#include <QTimer>
#include <QWidget>
#include <memory>
#include "audio/beat_source.h"
#include "audio/sample_cache.h"
#include "audio/djtoolz_engine.h"
#include "audio/drum_kits.h"
#include "audio/djtoolz_presets.h"
#include "audio/djtoolz_scheduler.h"
#include "midi/midi_out.h"
#include "midi/midi_router.h"

class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QSlider;
class QToolButton;
class QTreeView;
class QVBoxLayout;
class QGridLayout;

namespace vdj {

class AlsaClock;
class SampleTreeModel;
class DjtoolzController;
class DjtoolzToolWidget;

// The live DJ Tools: a sample folder tree, a list of tools (Bombomatic, Stab player, Acid loop, One shot, MIDI matic) each with
// its own name / on-off / settings, saved presets and a master volume. It follows the VDJ sequencer (AlsaClock) and keeps playing
// when the panel is hidden. The working state is saved to <dataDir>/djtoolz-current.json after every change and restored at start.
class DjtoolzPanel : public QWidget {
    Q_OBJECT

public:
    static constexpr int kPreviewTag = -1;

    DjtoolzPanel(AlsaClock* clock, const QString& dataDir, QWidget* parent = nullptr);
    ~DjtoolzPanel() override;

    void shutdown();                                 // stop the threads (before the AlsaClock goes away); safe to repeat
    void setAudioEnabled(bool on) { audioEnabled_ = on; }
    // How long the sound takes from the DJ Tools to the speakers (the sound server's buffer): sounds are rendered this much early.
    // ConfigManager djtoolz/latency_ms. Tune it by ear (or with the timing lines in the debug panel) until the kick is on the beat.
    void setLatencyMs(double ms);   // false: never open the sound output (tests)

    DjtoolzScheduler* scheduler() { return &scheduler_; }
    DjtoolzEngine* engine() { return &engine_; }
    FxRack* fxRack() { return &fxRack_; }            // the local player applies the same effects
    SampleCache* cache() { return &cache_; }
    DjtoolzPresets* presets() { return &presets_; }
    DrumKits* drumKits() { return &kits_; }                 // the DR-6S's own presets, shared by all DR-6S tools
    void drumKitsChanged() { emit kitsChanged(); }          // a DR-6S saved or deleted a kit: the others refresh their lists
    SampleTreeModel* treeModel() { return treeModel_; }

    DjtoolzToolWidget* addTool(DjtoolzTool::Type type);
    void removeTool(int toolId);
    QList<DjtoolzToolWidget*> toolWidgets() const { return widgets_; }

    DjtoolzState state() const;
    void applyState(const DjtoolzState& st);         // replaces everything
    QString sampleFolder() const { return folder_; }
    void setSampleFolder(const QString& folder);
    double masterGain() const;
    // Tools are switched on and off with F1..F12 in the order they are shown (the main window owns the shortcuts and calls this).
    static constexpr int kFunctionKeys = 12;
    bool toggleToolAt(int index);                  // false when there is no tool at that position
    int columns() const;                             // tools per row: 2 or 4
    void setColumns(int columns);
    QComboBox* columnsCombo() const { return columnsBox_; }
    // Where tool widget i sits in the grid: the cell and how many columns it spans (the DR-6S takes the whole row)
    QRect gridCell(int index) const;               // x = column, y = row, width = columns spanned, height 1

    // For the tool widgets
    void ensureRunning();                            // open the sound output and start the scheduler (first use)
    void previewSample(const QString& path);
    void toolEdited(int toolId);
    QList<MidiPort> midiDestinations();

    DjtoolzController* controller() const { return controller_; }
    QComboBox* controlModeCombo() const { return controlModeBox_; }
    QPushButton* controllerButton() const { return controllerButton_; }
    // The DR-6S the MIDI controller drives in the DR-6S mode: the one the human touched last, else the first (null without one)
    DjtoolzToolWidget* drumTarget() const;
    void setControlMode(int mode);                   // 0 = the strips drive the tools, 1 = the DR-6S

    QComboBox* presetCombo() const { return presetBox_; }
    QPushButton* loadPresetButton() const { return loadPreset_; }
    QPushButton* savePresetButton() const { return savePreset_; }
    QPushButton* newPresetButton() const { return newPreset_; }
    QPushButton* clearButton() const { return clearPreset_; }
    void clearTools();                             // remove every tool, nothing is saved to a preset
    QPushButton* deletePresetButton() const { return deletePreset_; }
    QToolButton* addButton() const { return add_; }
    QSlider* masterSlider() const { return master_; }
    QLabel* statusLabel() const { return status_; }
    QTreeView* tree() const { return tree_; }
    // The right click menu of the tree: "Send to <tool>" for every tool that plays a sample (null when there is nothing to offer).
    // Public so a test can build it without a mouse.
    QMenu* sendToMenu(const QModelIndex& index, QWidget* parent);
    void saveCurrentNow();
    void refreshPresetList();
    bool savePresetAs(const QString& name);          // no dialog (the Save button asks for the name)
    bool loadPresetByName(const QString& name);

signals:
    void message(const QString& text);
    void kitsChanged();
    void controllerDialogRequested();                // the "Controller..." button

private:
    void connectToolWidget(DjtoolzToolWidget* w);
    void relayout();
    void poll();
    void chooseFolder();
    void scheduleSave();
    void updatePresetButtons();

    AlsaClock* clock_;
    SampleCache cache_;
    AlsaClockBeatSource source_;
    FxRack fxRack_;                                  // before the engine: the engine thread uses it
    DjtoolzEngine engine_;
    DjtoolzScheduler scheduler_;
    MidiOut midiOut_;
    DjtoolzPresets presets_;
    DrumKits kits_;
    MidiRouter router_;

    SampleTreeModel* treeModel_;
    QTreeView* tree_;
    QPushButton* folderButton_;
    QToolButton* add_;
    QComboBox* presetBox_;
    QPushButton* loadPreset_;
    QPushButton* savePreset_;
    QPushButton* newPreset_;
    QPushButton* clearPreset_;
    QPushButton* deletePreset_;
    QSlider* master_;
    QLabel* status_;
    QGridLayout* toolsLayout_;
    QComboBox* columnsBox_;
    QComboBox* controlModeBox_;
    QPushButton* controllerButton_;
    DjtoolzController* controller_;
    QWidget* toolsHost_;
    QTimer pollTimer_;
    QTimer saveTimer_;
    QList<DjtoolzToolWidget*> widgets_;
    QString folder_;
    int lastTouchedId_ = -1;
    bool audioEnabled_ = true;
    bool running_ = false;
    bool loading_ = false;
    bool shutDown_ = false;
};

} // namespace vdj

#endif // DJTOOLZ_PANEL_H
