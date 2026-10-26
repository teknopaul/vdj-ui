#ifndef DJTOOLZ_MODULE_H
#define DJTOOLZ_MODULE_H

#include <QMap>
#include <QString>
#include <QVariantMap>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include "drum_pattern.h"

namespace vdj {

// Everything the DJ can set on a tool. One struct for all types so a preset is just a list of these; a type uses the
// fields that make sense for it (the others keep their defaults).
struct ToolSettings {
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

// A sound the tool wants at an integer beat (counted from the sequencer's start). Which beat of the bar it is: beat % 4.
struct PlannedHit {
    double beat = 0.0;
    int voice = -1;                      // DR-6S: which of the six voices (-1 for the other tools)
    float level = 1.0f;                  // DR-6S: 1 on, 0.5 half volume
};

enum class OneBarState { Idle, Armed, Playing };

// A DJ Tools type. Settings are changed from the GUI thread and read from the scheduler thread, so they sit behind a mutex.
class DjtoolzTool {
public:
    enum Type { Bombomatic, StabPlayer, AcidLoop, OneShot, MidiMatic, Dembomatik, DrumMachine, Fx };
    static constexpr double kMaxOffsetMs = 80.0;
    static constexpr int kMaxVolume = 127;     // the scheduler looks 100 ms ahead

    virtual ~DjtoolzTool() = default;

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
    virtual bool isEffect() const { return false; }         // processes the audio output instead of playing sounds (FxTool)
    virtual bool periodic() const { return true; }   // false: only plays when fired (OneShot)
    virtual bool stretchesToBpm() const { return false; }   // the scheduler plays a time-stretched copy (AcidLoop)

    ToolSettings settings() const;
    void setSettings(const ToolSettings& s);       // clamps offset / channel / note / velocity / gain
    bool enabled() const { return settings().enabled; }

    // The hits in [fromBeat, toBeat) (not yet moved by the offset or reverse sync: the scheduler does that).
    virtual void plan(double fromBeat, double toBeat, std::vector<PlannedHit>& out);

    // One bar on demand (tools that supportsOneBar): arms the bar starting at the next bar line after nowBeat, a second
    // call while armed or playing cancels. Works with the on/off switch off; with it on the tool already plays all the time.
    void fireOneBar(double nowBeat);
    void cancelOneBar();
    void finishOneBar(double nowBeat);               // the scheduler calls this each tick: forget a bar that has played
    OneBarState oneBarState(double nowBeat) const;

    QVariantMap toVariant() const;
    static std::shared_ptr<DjtoolzTool> create(Type type);
    static std::shared_ptr<DjtoolzTool> fromVariant(const QVariantMap& m);   // null for an unknown type
    static QString typeKey(Type t);

protected:
    DjtoolzTool();
    virtual void settingsApplied(const ToolSettings&) {}    // after setSettings(), without the lock held
    virtual void extraToVariant(QVariantMap&) const {}       // what a type saves besides the ToolSettings
    virtual void extraFromVariant(const QVariantMap&) {}
    // Does this beat sound for the tool? (beat is an integer) Default: every beat.
    virtual bool hitsBeat(const ToolSettings&, long beat) const { Q_UNUSED(beat); return true; }
    // The hits in [lo, hi) after the one bar gating. Default: the integer beats that hitsBeat() accepts.
    virtual void collectHits(double lo, double hi, const ToolSettings& s, std::vector<PlannedHit>& out) const;

private:
    int id_;
    mutable std::mutex mutex_;
    ToolSettings s_;
    double oneBarStart_ = -1.0;                      // beat of the bar line the one bar starts on, -1 = none
};

class Bombomatic : public DjtoolzTool {
public:
    Type type() const override { return DjtoolzTool::Bombomatic; }
    QString typeName() const override { return QStringLiteral("Bombomatic"); }
    bool supportsOneBar() const override { return true; }
};

class StabPlayer : public DjtoolzTool {
public:
    Type type() const override { return DjtoolzTool::StabPlayer; }
    QString typeName() const override { return QStringLiteral("Stab player"); }
    bool hasBeatChooser() const override { return true; }
    bool supportsOneBar() const override { return true; }
    bool supportsReverseSync() const override { return true; }

protected:
    bool hitsBeat(const ToolSettings& s, long beat) const override { return s.beatMask & (1 << (beat & 3)); }
};

// A sample that is already a 4 beat loop: it is (re)started on every bar line, stretched so that it is 4 beats long.
class AcidLoop : public DjtoolzTool {
public:
    Type type() const override { return DjtoolzTool::AcidLoop; }
    QString typeName() const override { return QStringLiteral("Acid loop"); }
    bool supportsOneBar() const override { return true; }
    bool stretchesToBpm() const override { return true; }

protected:
    bool hitsBeat(const ToolSettings&, long beat) const override { return (beat & 3) == 0; }
};

// Plays one sample to its end when fired (DjtoolzScheduler::fireOneShot), optionally on the next beat and stretched.
class OneShot : public DjtoolzTool {
public:
    Type type() const override { return DjtoolzTool::OneShot; }
    QString typeName() const override { return QStringLiteral("One shot"); }
    bool periodic() const override { return false; }
};

// The dembow kick: the bar cut into 16 equal parts, played on  X . . X . . X . X . . . . X . .
// (parts 1, 4, 7, 9 and 14), the "boom-ka-boom-chick" of reggaeton.
class Dembomatik : public DjtoolzTool {
public:
    static constexpr int kSteps = 16;
    static bool stepIsOn(int step);                  // step 0..15
    Type type() const override { return DjtoolzTool::Dembomatik; }
    QString typeName() const override { return QStringLiteral("Dembomatik"); }
    bool supportsOneBar() const override { return true; }
    bool supportsSwing() const override { return true; }

protected:
    void collectHits(double lo, double hi, const ToolSettings& s, std::vector<PlannedHit>& out) const override;
};

// The DR-6S: six voices (BD SD SY CL HH OH) each with its own sample, volume, offset, choke and 16 step pattern, and a swing.
// (The hits and the scheduling arrive with the next phases; for now the tool only holds and saves its settings.)
class DrumMachine : public DjtoolzTool {
public:
    Type type() const override { return DjtoolzTool::DrumMachine; }
    QString typeName() const override { return QStringLiteral("DR-6S"); }
    bool supportsOneBar() const override { return true; }
    bool supportsSwing() const override { return true; }
    bool hasDrumVoices() const override { return true; }
    bool usesSample() const override { return false; }
    bool perVoiceSamples() const override { return true; }
    // The tool level offsetMs is not used: every voice has its own offset.

protected:
    void collectHits(double lo, double hi, const ToolSettings& s, std::vector<PlannedHit>& out) const override;
};

// A MIDI note on the chosen beats of the bar (device, channel and note in the settings).
class MidiMatic : public DjtoolzTool {
public:
    Type type() const override { return DjtoolzTool::MidiMatic; }
    QString typeName() const override { return QStringLiteral("MIDI matic"); }
    bool hasBeatChooser() const override { return true; }
    bool supportsOneBar() const override { return true; }
    bool usesSample() const override { return false; }

protected:
    bool hitsBeat(const ToolSettings& s, long beat) const override { return s.beatMask & (1 << (beat & 3)); }
};

// An effect on the audio output of vdj-ui (the DJ Tools and the local player): any LADSPA plugin with 1-2 audio ins and outs
// (filters, reverbs, delays ...), picked by key (see LadspaHost) with its knob values. The on/off switch bypasses it.
// The effects run in the order of the tools. FxRunner (fx_rack.h) does the processing; the tool only holds the settings.
class FxTool : public DjtoolzTool {
public:
    struct Spec {
        QString plugin;                              // LadspaPluginInfo::key, empty = none chosen
        QMap<QString, float> values;                 // control name -> value; a missing control has its default
        QString knob, fader;                         // the controls the MIDI strip's knob and fader drive; empty = the 1st and 2nd
    };
    Type type() const override { return DjtoolzTool::Fx; }
    QString typeName() const override { return QStringLiteral("FX"); }
    bool isEffect() const override { return true; }
    bool periodic() const override { return false; }
    bool usesSample() const override { return false; }

    Spec spec() const;
    void setPlugin(const QString& key);              // forgets the values
    void setValue(const QString& control, float value);
    void setKnobControl(const QString& control);
    void setFaderControl(const QString& control);
    bool bypassed() const { return !on_.load(); }
    // Changes whenever anything the runner uses changes (cheap to poll from the audio thread)
    quint64 revision() const { return revision_.load(); }

protected:
    void settingsApplied(const ToolSettings& s) override;
    void extraToVariant(QVariantMap& m) const override;
    void extraFromVariant(const QVariantMap& m) override;

private:
    mutable std::mutex specMutex_;
    Spec spec_;
    std::atomic<bool> on_{ false };
    std::atomic<quint64> revision_{ 1 };
};

} // namespace vdj

#endif // DJTOOLZ_MODULE_H
