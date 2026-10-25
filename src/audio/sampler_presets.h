#ifndef SAMPLER_PRESETS_H
#define SAMPLER_PRESETS_H

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <memory>
#include <vector>
#include "sampler_module.h"

namespace vdj {

// Everything the DJ sets in the sampler: the sample folder, the master volume and the modules with all their settings.
// Kept as plain data (the modules as QVariantMaps from SamplerModule::toVariant) so a preset can be read without a scheduler.
struct SamplerState {
    QString folder;
    double masterGain = 1.0;
    int columns = 2;                                 // modules per row in the panel: 2 or 4
    QVariantList modules;
};

SamplerState samplerStateFrom(const std::vector<std::shared_ptr<SamplerModule>>& modules, const QString& folder, double masterGain);
// Rebuild the modules (new ids). Entries of an unknown type or that are not objects are skipped, each adds a line to *warnings.
// A module whose sample file is gone keeps its path (the UI shows it as missing).
std::vector<std::shared_ptr<SamplerModule>> samplerModulesFrom(const SamplerState& state, QStringList* warnings = nullptr);

// Presets are one JSON file per name in <baseDir>/sampler-presets/, the working state is <baseDir>/sampler-current.json.
// baseDir is ConfigManager::configDir() in the program.
class SamplerPresets {
public:
    static constexpr int kVersion = 1;

    explicit SamplerPresets(const QString& baseDir);

    // File-name safe version of a preset name: letters, digits, space, '_', '-', '.' stay, anything else becomes '_'; trimmed,
    // no leading dots, at most 64 characters. Empty when nothing is left.
    static QString sanitise(const QString& name);

    QStringList list() const;                        // names, sorted
    bool exists(const QString& name) const;
    bool save(const QString& name, const SamplerState& state);
    bool load(const QString& name, SamplerState* state, QString* error = nullptr) const;
    bool remove(const QString& name);
    bool rename(const QString& from, const QString& to);      // false when `to` exists

    bool saveCurrent(const SamplerState& state);
    bool loadCurrent(SamplerState* state, QString* error = nullptr) const;

    QString presetPath(const QString& name) const;
    QString currentPath() const;
    QString presetDir() const { return dir_; }

private:
    static bool writeFile(const QString& path, const SamplerState& state);
    static bool readFile(const QString& path, SamplerState* state, QString* error);

    QString base_;
    QString dir_;
};

} // namespace vdj

#endif // SAMPLER_PRESETS_H
