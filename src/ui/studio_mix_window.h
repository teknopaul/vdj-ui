#ifndef STUDIO_MIX_WINDOW_H
#define STUDIO_MIX_WINDOW_H

#include "mix_canvas.h"
#include "trakcrate/library.h"
#include "trakcrate/mix_model.h"
#include "trakcrate/stretch_cache.h"

#include <QDialog>
#include <QMainWindow>

#include <functional>
#include <map>
#include <memory>

class QComboBox;
class QDoubleSpinBox;
class QDockWidget;
class QLabel;
class QTimer;
class QListWidget;
class QPushButton;
class QSlider;
class QUndoStack;

namespace vdj {

class ConfigManager;
class MixFiltersDialog;
class MixTreeDock;
class TrakcrateEditPanel;
class MixStretchDock;
class MixPreviewer;
class MixRenderDialog;
class StretchJobQueue;

// "New studio mix from a playlist": the playlists of the library, the tracks of the chosen one (those that are not analysed are red, with the reason, and
// are left out) and an Analyse button that analyses them in the background.
class NewMixDialog : public QDialog {
    Q_OBJECT

public:
    NewMixDialog(const trakcrate::Library* lib, QWidget* parent = nullptr);

    void selectPlaylist(const QString& name);
    QString playlist() const;
    // The analysed tracks of the chosen playlist in play order.
    std::vector<trakcrate::MixTrackInput> tracks() const;
    int unanalysedCount() const { return unanalysed_; }
    QListWidget* trackList() const { return tracks_; }
    QPushButton* analyseButton() const { return analyse_; }
    void waitForAnalysis();            // for tests

private:
    void fillTracks();
    void startAnalysis();

    const trakcrate::Library* lib_;
    QListWidget* playlists_;
    QListWidget* tracks_;
    QPushButton* analyse_;
    QPushButton* ok_;
    QLabel* note_;
    int unanalysed_ = 0;
    class QThread* worker_ = nullptr;
    std::atomic<bool> cancel_{false};
};

// The studio mix window (plan studio-mix_PLAN.md): a normal top level window with the lanes of a mix. Editing, saving and playing come in later phases.
class StudioMixWindow : public QMainWindow {
    Q_OBJECT

public:
    using LibraryProvider = std::function<const trakcrate::Library*()>;

    StudioMixWindow(LibraryProvider provider, ConfigManager* config, QWidget* parent = nullptr);
    ~StudioMixWindow() override;

    MixCanvas* canvas() const { return canvas_; }
    QUndoStack* undoStack() const { return undo_; }
    const trakcrate::Mix& mix() const { return mix_; }
    const std::vector<trakcrate::TrackFacts>& facts() const { return facts_; }
    trakcrate::StretchCache* stretchCache() { return cache_.get(); }
    StretchJobQueue* stretchQueue() const { return queue_; }

    // Every change of the mix goes through here: one undo step. A non empty mergeKey merges consecutive edits with the same key (a drag, a spin box).
    void edit(const QString& text, std::function<void(trakcrate::Mix&)> change, const QString& mergeKey = QString());
    // Puts a whole snapshot in place (used by the undo commands; not an edit by itself).
    void applyMix(const trakcrate::Mix& mix);

    // The edits of the toolbar and docks, also callable directly.
    void setMasterBpm(double bpm);
    void setTrim(int lane, double percent);
    void nudgeGrid(int lane, double deltaMs);               // 0 resets
    void setMixType(int pair, trakcrate::MixType type);
    void removeTrack(int lane);                              // out of the mix only; the playlist is untouched
    int selectedLane() const { return canvas_->selectedLane(); }
    QDoubleSpinBox* bpmSpin() const { return bpmSpin_; }
    MixStretchDock* stretchDock() const { return stretchDock_; }
    QDoubleSpinBox* trimSpin() const { return trimSpin_; }
    MixFiltersDialog* filtersDialog();
    MixPreviewer* previewer() const { return previewer_; }
    void togglePreview();
    void renderMp3();                                 // File > Render MP3...
    std::function<void(MixRenderDialog*)> renderHook;       // tests: replaces exec()

    // ---- save, open, the tree ----
    // The questions the window asks. The defaults open dialogs; tests inject answers.
    struct Prompts {
        // name may include a folder ("Folder/Name"); false = cancelled
        std::function<bool(QString* name, QString* author, QString* image)> saveDetails;
        std::function<int(const QString& text)> askUnsaved;          // 0 save, 1 discard, 2 cancel
        std::function<QString()> chooseOpenFile;                     // "" = cancelled
        std::function<void(const QString& text)> notify;
    };
    Prompts prompts;
    bool save();                                      // Ctrl+S: the first time it asks for name, author and image
    bool saveAs();
    bool saveToName(const QString& relName, const QString& author, const QString& image);   // no questions asked
    bool openMixFile(const QString& path);
    bool maybeSave();                                 // asks when there are unsaved changes; false = cancelled
    bool isModified() const;
    QString currentFile() const { return currentFile_; }
    MixTreeDock* treeDock() const { return tree_; }
    TrakcrateEditPanel* regionPanel() const { return regionPanel_; }

    // Shows a mix: the facts and lane infos are read from the track folders. Returns false (with a message) when the library cannot be used.
    void setMix(const trakcrate::Mix& mix, const std::vector<trakcrate::TrackFacts>& facts, const std::vector<MixLaneInfo>& lanes);
    // Builds the lanes from the tracks of a playlist (the dialog's result) and shows them.
    void showPlaylistMix(const QString& playlist, const std::vector<trakcrate::MixTrackInput>& tracks);
    void newFromPlaylist(const QString& preselect = QString());

signals:
    void mixChanged();

protected:
    void closeEvent(QCloseEvent*) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildMenus();
    void setLaneHeightChoice(int px);
    void updateStatus();

    LibraryProvider provider_;
    ConfigManager* config_;
    trakcrate::Mix mix_;
    std::vector<trakcrate::TrackFacts> facts_;
    MixCanvas* canvas_;
    QUndoStack* undo_;
    QDoubleSpinBox* bpmSpin_;
    QDoubleSpinBox* trimSpin_;
    QLabel* nudgeLabel_;
    QLabel* trackLabel_;
    MixStretchDock* stretchDock_;
    StretchJobQueue* queue_;
    std::unique_ptr<trakcrate::StretchCache> cache_;
    QTimer* stretchTimer_;
    std::string stretchSignature_;
    MixFiltersDialog* filters_ = nullptr;
    MixPreviewer* previewer_ = nullptr;
    bool filling_ = false;
    QString currentFile_;                      // the .mix file this mix is saved in ("" = never saved)
    MixTreeDock* tree_ = nullptr;
    TrakcrateEditPanel* regionPanel_ = nullptr;
    QUndoStack* panelUndo_ = nullptr;          // the panel wants one; its changes are routed through undo_ instead (external mode)
    class QPushButton* rangeButton_ = nullptr;
    int regionSel_ = -1, regionSelLane_ = -1;
    int panelLane_ = -2;
    trakcrate::BeatGrid panelGrid_;
    double panelLen_ = 0;
    std::vector<trakcrate::EditRegion> panelRegions_;
    int panelSel_ = -3;
    struct Parked { trakcrate::TrackFacts facts; MixLaneInfo info; };
    std::map<std::string, Parked> parked_;     // tracks taken out of the mix, kept so that undo can bring them back
    void realignTracks(const trakcrate::Mix& old);
    void refreshDocks();
    void scheduleStretch();
    void audition();
    void updateTitle();
    void updateRegionPanel();
    void loadMixAndFacts(const trakcrate::Mix& mix, bool opened);
    bool writeFile(const QString& path);
    QLabel* posLabel_;
    QLabel* hoverLabel_;
    QComboBox* playlistCombo_;
    QSlider* zoom_;
    QAction* heightActions_[3] = {nullptr, nullptr, nullptr};
};

} // namespace vdj

#endif
