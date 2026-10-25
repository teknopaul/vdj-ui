#ifndef DRUM_KITS_H
#define DRUM_KITS_H

#include <QString>
#include <QStringList>
#include "drum_pattern.h"
#include "sampler_module.h"

namespace vdj {

// What a drum kit holds: the six voices (sample, volume, offset, choke, 16 step pattern) and the swing. It does not hold the module's
// name, its on/off switch or its master volume: loading a kit must not unmute or rename the DR-6S.
struct DrumKit {
    DrumVoice voices[kDrumVoices];
    int swing = 0;
};

DrumKit drumKitFromSettings(const ModuleSettings& s);
void applyDrumKit(ModuleSettings* s, const DrumKit& kit);     // sets the voices and the swing, nothing else

// The DR-6S's own save / load, apart from the sampler presets: one JSON file per kit in <baseDir>/drum-kits/.
class DrumKits {
public:
    static constexpr int kVersion = 1;

    explicit DrumKits(const QString& baseDir);

    static QString sanitise(const QString& name);              // the same rules as SamplerPresets::sanitise

    QStringList list() const;                                  // names, sorted
    bool exists(const QString& name) const;
    bool save(const QString& name, const DrumKit& kit);
    bool load(const QString& name, DrumKit* kit, QString* error = nullptr) const;
    bool remove(const QString& name);
    bool rename(const QString& from, const QString& to);       // false when `to` exists

    QString kitPath(const QString& name) const;
    QString kitDir() const { return dir_; }

private:
    QString dir_;
};

} // namespace vdj

#endif // DRUM_KITS_H
