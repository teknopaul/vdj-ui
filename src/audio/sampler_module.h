#ifndef SAMPLER_MODULE_H
#define SAMPLER_MODULE_H

#include <QString>
#include <QVariantMap>
#include <memory>
#include <mutex>
#include <vector>
#include "drum_pattern.h"

namespace vdj {

// Everything the DJ can set on a module. One struct for all types so a preset is just a list of these; a type uses the
// fields that make sense for it (the others keep their defaults).
struct ModuleSettings {
    QString name;
    bool enabled = false;
    QString samplePath;
    float gain = 1.0f;
    int volume = 127;                    // 0 = silent, 1..127 like a MIDI value; 127 = as loud as the sample (MIDI matic: the velocity)
    double offsetMs = 0.0;               // +/- to the beat, kMaxOffsetMs either way
    quint8 beatMask = 0x0F;              // bit 0..3 = beat 1..4 of the bar (stab player, MidiMatic)
    bool reverseSync = false;            // the sample ends on the beat instead of starting on it (stab player)
    bool quantize = true;                // start on the next beat (one shots)
    bool stretch = true;                 // time-stretch to the BPM when the sample's BPM is known (acid loops, one shots)
    double manualBpm = 0.0;              // the sample's BPM when it does not say (0 = unknown)
    QString midiDevice;                  // MidiMatic: "client:port"
    int midiChannel = 1;                 // 1..16
    int midiNote = 36;
    int midiVelocity = 100;
    int swing = 0;                       // 0..127, see swing.h (Dembomatik, DR-6S)
    DrumVoice drums[kDrumVoices];        // DR-6S: sample, volume, offset, choke and pattern of BD SD SY CL HH OH
};

// A sound the module wants at an integer beat (counted from the sequencer's start). Which beat of the bar it is: beat % 4.
struct PlannedHit {
    double beat = 0.0;
    int voice = -1;                      // DR-6S: which of the six voices (-1 for the other modules)
    float level = 1.0f;                  // DR-6S: 1 on, 0.5 half volume
};

enum class OneBarState { Idle, Armed, Playing };

// A sampler type. Settings are changed from the GUI thread and read from the scheduler thread, so they sit behind a mutex.
class SamplerModule {
public:
    enum Type { Bombomatic, StabPlayer, AcidLoop, OneShot, MidiMatic, Dembomatik, DrumMachine };
    static constexpr double kMaxOffsetMs = 80.0;
    static constexpr int kMaxVolume = 127;     // the scheduler looks 100 ms ahead

    virtual ~SamplerModule() = default;

    int id() const { return id_; }                   // unique, used as the voice tag
    virtual Type type() const = 0;
    virtual QString typeName() const = 0;
    virtual bool hasBeatChooser() const { return false; }
    virtual bool supportsOneBar() const { return false; }
    virtual bool supportsReverseSync() const { return false; }
    virtual bool usesSample() const { return true; }       // false: no single sample (MidiMatic plays MIDI, the DR-6S has six)
    virtual bool perVoiceSamples() const { return false; }  // the DR-6S: samples, volumes and offsets are per voice (settings.drums)
    virtual bool supportsSwing() const { return false; }    // has a swing setting that is saved
    virtual bool hasDrumVoices() const { return false; }    // saves the six drum voices (DR-6S)
    virtual bool periodic() const { return true; }   // false: only plays when fired (OneShot)
    virtual bool stretchesToBpm() const { return false; }   // the scheduler plays a time-stretched copy (AcidLoop)

    ModuleSettings settings() const;
    void setSettings(const ModuleSettings& s);       // clamps offset / channel / note / velocity / gain
    bool enabled() const { return settings().enabled; }

    // The hits in [fromBeat, toBeat) (not yet moved by the offset or reverse sync: the scheduler does that).
    virtual void plan(double fromBeat, double toBeat, std::vector<PlannedHit>& out);

    // One bar on demand (modules that supportsOneBar): arms the bar starting at the next bar line after nowBeat, a second
    // call while armed or playing cancels. Works with the on/off switch off; with it on the module already plays all the time.
    void fireOneBar(double nowBeat);
    void cancelOneBar();
    void finishOneBar(double nowBeat);               // the scheduler calls this each tick: forget a bar that has played
    OneBarState oneBarState(double nowBeat) const;

    QVariantMap toVariant() const;
    static std::shared_ptr<SamplerModule> create(Type type);
    static std::shared_ptr<SamplerModule> fromVariant(const QVariantMap& m);   // null for an unknown type
    static QString typeKey(Type t);

protected:
    SamplerModule();
    // Does this beat sound for the module? (beat is an integer) Default: every beat.
    virtual bool hitsBeat(const ModuleSettings&, long beat) const { Q_UNUSED(beat); return true; }
    // The hits in [lo, hi) after the one bar gating. Default: the integer beats that hitsBeat() accepts.
    virtual void collectHits(double lo, double hi, const ModuleSettings& s, std::vector<PlannedHit>& out) const;

private:
    int id_;
    mutable std::mutex mutex_;
    ModuleSettings s_;
    double oneBarStart_ = -1.0;                      // beat of the bar line the one bar starts on, -1 = none
};

class Bombomatic : public SamplerModule {
public:
    Type type() const override { return SamplerModule::Bombomatic; }
    QString typeName() const override { return QStringLiteral("Bombomatic"); }
    bool supportsOneBar() const override { return true; }
};

class StabPlayer : public SamplerModule {
public:
    Type type() const override { return SamplerModule::StabPlayer; }
    QString typeName() const override { return QStringLiteral("Stab player"); }
    bool hasBeatChooser() const override { return true; }
    bool supportsOneBar() const override { return true; }
    bool supportsReverseSync() const override { return true; }

protected:
    bool hitsBeat(const ModuleSettings& s, long beat) const override { return s.beatMask & (1 << (beat & 3)); }
};

// A sample that is already a 4 beat loop: it is (re)started on every bar line, stretched so that it is 4 beats long.
class AcidLoop : public SamplerModule {
public:
    Type type() const override { return SamplerModule::AcidLoop; }
    QString typeName() const override { return QStringLiteral("Acid loop"); }
    bool supportsOneBar() const override { return true; }
    bool stretchesToBpm() const override { return true; }

protected:
    bool hitsBeat(const ModuleSettings&, long beat) const override { return (beat & 3) == 0; }
};

// Plays one sample to its end when fired (SamplerScheduler::fireOneShot), optionally on the next beat and stretched.
class OneShot : public SamplerModule {
public:
    Type type() const override { return SamplerModule::OneShot; }
    QString typeName() const override { return QStringLiteral("One shot"); }
    bool periodic() const override { return false; }
};

// The dembow kick: the bar cut into 16 equal parts, played on  X . . X . . X . X . . . . X . .
// (parts 1, 4, 7, 9 and 14), the "boom-ka-boom-chick" of reggaeton.
class Dembomatik : public SamplerModule {
public:
    static constexpr int kSteps = 16;
    static bool stepIsOn(int step);                  // step 0..15
    Type type() const override { return SamplerModule::Dembomatik; }
    QString typeName() const override { return QStringLiteral("Dembomatik"); }
    bool supportsOneBar() const override { return true; }
    bool supportsSwing() const override { return true; }

protected:
    void collectHits(double lo, double hi, const ModuleSettings& s, std::vector<PlannedHit>& out) const override;
};

// The DR-6S: six voices (BD SD SY CL HH OH) each with its own sample, volume, offset, choke and 16 step pattern, and a swing.
// (The hits and the scheduling arrive with the next phases; for now the module only holds and saves its settings.)
class DrumMachine : public SamplerModule {
public:
    Type type() const override { return SamplerModule::DrumMachine; }
    QString typeName() const override { return QStringLiteral("DR-6S"); }
    bool supportsOneBar() const override { return true; }
    bool supportsSwing() const override { return true; }
    bool hasDrumVoices() const override { return true; }
    bool usesSample() const override { return false; }
    bool perVoiceSamples() const override { return true; }
    // The module level offsetMs is not used: every voice has its own offset.

protected:
    void collectHits(double lo, double hi, const ModuleSettings& s, std::vector<PlannedHit>& out) const override;
};

// A MIDI note on the chosen beats of the bar (device, channel and note in the settings).
class MidiMatic : public SamplerModule {
public:
    Type type() const override { return SamplerModule::MidiMatic; }
    QString typeName() const override { return QStringLiteral("MIDI matic"); }
    bool hasBeatChooser() const override { return true; }
    bool supportsOneBar() const override { return true; }
    bool usesSample() const override { return false; }

protected:
    bool hitsBeat(const ModuleSettings& s, long beat) const override { return s.beatMask & (1 << (beat & 3)); }
};

} // namespace vdj

#endif // SAMPLER_MODULE_H
