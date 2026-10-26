#ifndef DJTOOLZ_PRESETS_H
#define DJTOOLZ_PRESETS_H

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <memory>
#include <vector>
#include "djtoolz_tool.h"

namespace vdj {

// Everything the DJ sets in the djtoolz: the sample folder, the master volume and the tools with all their settings.
// Kept as plain data (the tools as QVariantMaps from DjtoolzTool::toVariant) so a preset can be read without a scheduler.
struct DjtoolzState {
    QString folder;
    double masterGain = 1.0;
    int columns = 2;                                 // tools per row in the panel: 2 or 4
    int controlMode = 0;                             // MIDI controller mode: 0 = strips drive the tools, 1 = the DR-6S
    QVariantList tools;
};

DjtoolzState djtoolzStateFrom(const std::vector<std::shared_ptr<DjtoolzTool>>& tools, const QString& folder, double masterGain);
// Rebuild the tools (new ids). Entries of an unknown type or that are not objects are skipped, each adds a line to *warnings.
// A tool whose sample file is gone keeps its path (the UI shows it as missing).
std::vector<std::shared_ptr<DjtoolzTool>> djtoolzToolsFrom(const DjtoolzState& state, QStringList* warnings = nullptr);

// Presets are one JSON file per name in <baseDir>/djtoolz-presets/, the working state is <baseDir>/djtoolz-current.json.
// baseDir is ConfigManager::configDir() in the program.
class DjtoolzPresets {
public:
    static constexpr int kVersion = 1;

    explicit DjtoolzPresets(const QString& baseDir);

    // File-name safe version of a preset name: letters, digits, space, '_', '-', '.' stay, anything else becomes '_'; trimmed,
    // no leading dots, at most 64 characters. Empty when nothing is left.
    static QString sanitise(const QString& name);

    QStringList list() const;                        // names, sorted
    bool exists(const QString& name) const;
    bool save(const QString& name, const DjtoolzState& state);
    bool load(const QString& name, DjtoolzState* state, QString* error = nullptr) const;
    bool remove(const QString& name);
    bool rename(const QString& from, const QString& to);      // false when `to` exists

    bool saveCurrent(const DjtoolzState& state);
    bool loadCurrent(DjtoolzState* state, QString* error = nullptr) const;

    QString presetPath(const QString& name) const;
    QString currentPath() const;
    QString presetDir() const { return dir_; }

private:
    static bool writeFile(const QString& path, const DjtoolzState& state);
    static bool readFile(const QString& path, DjtoolzState* state, QString* error);

    QString base_;
    QString dir_;
};

} // namespace vdj

#endif // DJTOOLZ_PRESETS_H
