#ifndef SAMPLER_PANEL_H
#define SAMPLER_PANEL_H

#include <QList>
#include <QModelIndex>
#include <QPoint>
#include <QTimer>
#include <QWidget>
#include <memory>
#include "audio/beat_source.h"
#include "audio/sample_cache.h"
#include "audio/sampler_engine.h"
#include "audio/sampler_presets.h"
#include "audio/sampler_scheduler.h"
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
class SamplerModuleWidget;

// The live sampler: a sample folder tree, a list of modules (Bombomatic, Stab player, Acid loop, One shot, MIDI matic) each with
// its own name / on-off / settings, saved presets and a master volume. It follows the VDJ sequencer (AlsaClock) and keeps playing
// when the panel is hidden. The working state is saved to <dataDir>/sampler-current.json after every change and restored at start.
class SamplerPanel : public QWidget {
    Q_OBJECT

public:
    static constexpr int kPreviewTag = -1;

    SamplerPanel(AlsaClock* clock, const QString& dataDir, QWidget* parent = nullptr);
    ~SamplerPanel() override;

    void shutdown();                                 // stop the threads (before the AlsaClock goes away); safe to repeat
    void setAudioEnabled(bool on) { audioEnabled_ = on; }
    // How long the sound takes from the sampler to the speakers (the sound server's buffer): sounds are rendered this much early.
    // ConfigManager sampler/latency_ms. Tune it by ear (or with the timing lines in the debug panel) until the kick is on the beat.
    void setLatencyMs(double ms);   // false: never open the sound output (tests)

    SamplerScheduler* scheduler() { return &scheduler_; }
    SamplerEngine* engine() { return &engine_; }
    SampleCache* cache() { return &cache_; }
    SamplerPresets* presets() { return &presets_; }
    SampleTreeModel* treeModel() { return treeModel_; }

    SamplerModuleWidget* addModule(SamplerModule::Type type);
    void removeModule(int moduleId);
    QList<SamplerModuleWidget*> moduleWidgets() const { return widgets_; }

    SamplerState state() const;
    void applyState(const SamplerState& st);         // replaces everything
    QString sampleFolder() const { return folder_; }
    void setSampleFolder(const QString& folder);
    double masterGain() const;
    // Modules are switched on and off with F1..F12 in the order they are shown (the main window owns the shortcuts and calls this).
    static constexpr int kFunctionKeys = 12;
    bool toggleModuleAt(int index);                  // false when there is no module at that position
    int columns() const;                             // modules per row: 2 or 4
    void setColumns(int columns);
    QComboBox* columnsCombo() const { return columnsBox_; }
    // grid position of module widget i (row, column), for tests
    QPoint gridCell(int index) const;

    // For the module widgets
    void ensureRunning();                            // open the sound output and start the scheduler (first use)
    void previewSample(const QString& path);
    void moduleEdited(int moduleId);
    QList<MidiPort> midiDestinations();

    QComboBox* presetCombo() const { return presetBox_; }
    QPushButton* savePresetButton() const { return savePreset_; }
    QPushButton* deletePresetButton() const { return deletePreset_; }
    QToolButton* addButton() const { return add_; }
    QSlider* masterSlider() const { return master_; }
    QLabel* statusLabel() const { return status_; }
    QTreeView* tree() const { return tree_; }
    // The right click menu of the tree: "Send to <module>" for every module that plays a sample (null when there is nothing to offer).
    // Public so a test can build it without a mouse.
    QMenu* sendToMenu(const QModelIndex& index, QWidget* parent);
    void saveCurrentNow();
    void refreshPresetList();
    bool savePresetAs(const QString& name);          // no dialog (the Save button asks for the name)
    bool loadPresetByName(const QString& name);

signals:
    void message(const QString& text);

private:
    void connectModuleWidget(SamplerModuleWidget* w);
    void relayout();
    void poll();
    void chooseFolder();
    void scheduleSave();

    AlsaClock* clock_;
    SampleCache cache_;
    AlsaClockBeatSource source_;
    SamplerEngine engine_;
    SamplerScheduler scheduler_;
    MidiOut midiOut_;
    SamplerPresets presets_;
    MidiRouter router_;

    SampleTreeModel* treeModel_;
    QTreeView* tree_;
    QPushButton* folderButton_;
    QToolButton* add_;
    QComboBox* presetBox_;
    QPushButton* savePreset_;
    QPushButton* deletePreset_;
    QSlider* master_;
    QLabel* status_;
    QGridLayout* modulesLayout_;
    QComboBox* columnsBox_;
    QWidget* modulesHost_;
    QTimer pollTimer_;
    QTimer saveTimer_;
    QList<SamplerModuleWidget*> widgets_;
    QString folder_;
    bool audioEnabled_ = true;
    bool running_ = false;
    bool loading_ = false;
    bool shutDown_ = false;
};

} // namespace vdj

#endif // SAMPLER_PANEL_H
