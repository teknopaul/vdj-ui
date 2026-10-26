#ifndef DRUM_KITS_H
#define DRUM_KITS_H

#include <QString>
#include <QMap>
#include <QStringList>
#include "drum_pattern.h"
#include "djtoolz_tool.h"

namespace vdj {

// What a drum kit holds: the six voices (sample, volume, offset, choke, 16 step pattern) and the swing. It does not hold the tool's
// name, its on/off switch or its master volume: loading a kit must not unmute or rename the DR-6S.
struct DrumKit {
    DrumVoice voices[kDrumVoices];
    int swing = 0;
};

DrumKit drumKitFromSettings(const ToolSettings& s);
void applyDrumKit(ToolSettings* s, const DrumKit& kit);     // sets the voices and the swing, nothing else

// The DR-6S's own save / load, apart from the DJ Tools presets: one JSON file per kit in <baseDir>/drum-kits/.
class DrumKits {
public:
    static constexpr int kVersion = 1;

    explicit DrumKits(const QString& baseDir);

    static QString sanitise(const QString& name);              // the same rules as DjtoolzPresets::sanitise

    QStringList list() const;                                  // names, sorted
    bool exists(const QString& name) const;
    bool save(const QString& name, const DrumKit& kit);
    bool load(const QString& name, DrumKit* kit, QString* error = nullptr) const;
    bool remove(const QString& name);
    bool rename(const QString& from, const QString& to);       // false when `to` exists

    QString kitPath(const QString& name) const;
    QString kitDir() const { return dir_; }

    // ---- Portable kits ----
    // An exported kit is a folder with kKitFile and a samples/ folder holding copies of the (up to six) samples it uses; the kit file
    // refers to them relatively, so the folder can be copied, backed up or taken to another PC.
    static constexpr const char* kKitFile = "drumkit.json";
    // Copies the samples the kit uses into `folder` (which must be empty, or not exist yet) and writes kKitFile. The originals are only read.
    // False and *error when the folder is not empty or a sample cannot be copied; nothing is left behind in that case.
    static bool exportKit(const QString& name, const DrumKit& kit, const QString& folder, QString* error = nullptr);
    // The folders of exported kits found in `folder`: the folder itself, and each folder directly inside it.
    static QStringList kitFoldersIn(const QString& folder);
    static bool loadKitFolder(const QString& folder, DrumKit* kit, QString* name = nullptr, QString* error = nullptr);

    // Folders with exported kits that are listed (and can be loaded, not saved over or deleted) next to the saved kits.
    void setImportFolders(const QStringList& folders);
    QStringList importFolders() const { return importFolders_; }
    bool isImported(const QString& name) const { return imported_.contains(name); }

private:
    bool loadJson(const QByteArray& json, const QString& baseDir, DrumKit* kit, QString* error) const;
    static QByteArray kitJson(const DrumKit& kit, const QString& name, const QStringList* relativeSamples);

    QString dir_;
    QStringList importFolders_;
    QMap<QString, QString> imported_;                          // list name -> folder
};

} // namespace vdj

#endif // DRUM_KITS_H
