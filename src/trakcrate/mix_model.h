#ifndef TRAKCRATE_MIX_MODEL_H
#define TRAKCRATE_MIX_MODEL_H

#include "edit_plan.h"
#include "stretch_choice.h"

#include <map>
#include <string>
#include <vector>

// Studio mix model without Qt or audio (plan studio-mix_PLAN.md): an ordered list of tracks, a mix period between neighbours, automation
// curves, and the maths that places every track on the mix timeline. Positions are in beats of the track's EDITED timeline (stretched to the
// master tempo, then cuts and loops applied), so they stay with the audio when anything is moved.
namespace trakcrate {

struct CurvePoint {
    double beat = 0;
    double value = 100;            // percent, 0..100
};
// Points sorted by beat; two points at one beat make a vertical change.
struct Curve {
    std::vector<CurvePoint> points;
};

enum class MixType { CrossFade, CrossFadeDrop, BassDrop, RoughCut, DropCut, StopCut, TailCut, BassSwitch };
constexpr int kMixTypeCount = 8;
const char* mixTypeName(MixType t);                      // "cross-fade", "bass-drop" ...
bool mixTypeFromName(const std::string& name, MixType* out);

struct MixPair {
    double outStartBeat = 0;       // beat of the outgoing track where the period starts
    double inStartBeat = 0;        // beat of the incoming track where the period starts
    double lenBeats = 32;          // 8 bars
    MixType type = MixType::CrossFade;
};

struct MixTrack {
    fs::path file;                 // audio file of the track (its parent is the track folder)
    double trim = 100;             // percent, 0..200
    bool stretchOff = false;       // keep the track's own tempo
    StretchChoice stretch;         // empty = the mix default
    double gridNudgeMs = 0;        // this mix only, never written to beatgrid.txt
    std::vector<EditRegion> regions;
    Curve volume, bass, high;
    MixPair next;                  // the pair with the following track, unused on the last track
    bool missing = false;
};

enum class MixFilterKind { BuiltinCompressor, Ladspa };
struct MixFilter {
    MixFilterKind kind = MixFilterKind::BuiltinCompressor;
    bool on = true;
    std::string plugin;                        // LADSPA key, e.g. "caps.so|Compress"
    std::map<std::string, double> params;      // compressor: threshold, ratio, attack, release; LADSPA: control name -> value
};
std::vector<MixFilter> defaultFilters();       // the built-in compressor: -3 dBFS, 10:1, 1 ms, 80 ms

struct Mix {
    std::string name, author, image, playlist;
    double masterBpm = 120;
    StretchChoice stretch;
    double bassHz = 120, highHz = 2500;
    int slopeDb = 24;                          // 12, 24 or 48
    std::vector<MixFilter> filters = defaultFilters();
    std::vector<MixTrack> tracks;
};

// What the model needs to know about a track's audio (read from the track folder by the caller).
struct TrackFacts {
    BeatGrid grid;
    long long frames = 0;                      // at kEditRate
};

constexpr double kMinRampMs = 5;               // no curve ever changes faster than this
constexpr double kMaxStretchWarn = 0.08;       // beyond this tempo change artefacts are likely
constexpr double kMinRatio = 0.5, kMaxRatio = 2.0;

// ---- curves ----------------------------------------------------------------------------------------------------------------------

// The curve with the 5 ms minimum ramp built in: a point is never earlier than the previous one plus the ramp, so a vertical change
// becomes a short ramp that starts at the beat. Use this for per-sample work, then curveGainAt.
std::vector<CurvePoint> effectiveCurve(const Curve& c, double bpm);
double curveGainAt(const std::vector<CurvePoint>& effective, double beat);        // 0..1, empty = 1
double curveGain(const Curve& c, double beat, double bpm);

// Both keep the list sorted; a point is inserted after existing points at the same beat. Values are clamped to 0..100.
// They return the index of the point.
int setPoint(Curve* c, double beat, double value);
int movePoint(Curve* c, int index, double beat, double value);
void removePoint(Curve* c, int index);
bool curveIsDefault(const Curve& c);           // empty, or every point at 100 %

// ---- per track derived data ---------------------------------------------------------------------------------------------------

double defaultMasterBpm(const std::vector<TrackFacts>& tracks);     // the first track's bpm (120 when none has a grid)
double stretchRatio(double masterBpm, const BeatGrid& grid, bool stretchOff);
bool stretchImpossible(double ratio);          // outside 0.5..2
std::string stretchWarning(double ratio);      // "" within 8 %, a warning up to 2x, an error beyond

struct EffectiveTrack {
    bool ok = false;
    std::string error;                         // "" when ok
    double ratio = 1;
    BeatGrid stretchedGrid;                    // nudged, at the master tempo, on the stretched audio
    long long stretchedFrames = 0;
    EditPlan plan;
    long long editedFrames = 0;
    BeatGrid grid;                             // outputGrid(): the grid of the edited track, positions of the model are beats of this
    double bpm = 0;                            // grid.bpm
    double editedBeats = 0;                    // length of the edited track in beats of `grid`
};

EffectiveTrack effectiveTrack(const Mix& mix, const MixTrack& track, const TrackFacts& facts);
std::vector<EffectiveTrack> effectiveTracks(const Mix& mix, const std::vector<TrackFacts>& facts);

// Milliseconds from the start of the edited track to a beat of it (and back).
double trackBeatMs(const EffectiveTrack& t, double beat);
double trackMsBeat(const EffectiveTrack& t, double ms);

// ---- timeline -----------------------------------------------------------------------------------------------------------------

// Start of every track on the mix timeline. Track 0 starts at 0; track i+1 starts so that its inStartBeat sits at the time of track i's outStartBeat.
std::vector<double> trackStartMs(const Mix& mix, const std::vector<EffectiveTrack>& eff);
double mixLengthMs(const std::vector<double>& starts, const std::vector<EffectiveTrack>& eff);        // the latest track end
double beatToMixMs(const std::vector<double>& starts, const std::vector<EffectiveTrack>& eff, int track, double beat);
double mixMsToBeat(const std::vector<double>& starts, const std::vector<EffectiveTrack>& eff, int track, double ms);

// ---- building and changing a mix --------------------------------------------------------------------------------------------

struct MixTrackInput {
    fs::path file;
    TrackFacts facts;
    std::vector<EditRegion> regions;           // optional, applied before the default pairs are placed
};
Mix defaultMix(const std::string& name, const std::vector<MixTrackInput>& tracks);

// Writes the curve points of the type into both tracks of the pair (see the table in the plan) and sets pair.type.
void applyMixType(Mix* mix, int pairIndex, MixType type, const std::vector<EffectiveTrack>& eff);

// Moving a mix period (all clamp to the tracks; beats of the edited tracks). A period start only moves its own side; the derived track starts follow.
double snapBeat(double beat, bool toBars);                       // to the nearest bar line, or beat
void setPairOutStart(Mix* mix, int pair, double beat, const std::vector<EffectiveTrack>& eff);
void setPairInStart(Mix* mix, int pair, double beat, const std::vector<EffectiveTrack>& eff);
void setPairLength(Mix* mix, int pair, double lenBeats, const std::vector<EffectiveTrack>& eff);
// Takes track `index` out of the mix (the playlist is not touched). The pair around the gap is rebuilt with the default period (8 bars, the type of the
// pair before the gap) and the curve points that belonged to the removed pairs are dropped. `factsBefore` is parallel to mix->tracks before the call.
bool removeTrack(Mix* mix, int index, const std::vector<TrackFacts>& factsBefore);
// True when the two tracks of the pair run at tempos more than 2 % apart (only possible when one is not stretched).
bool pairTempoDiffers(const std::vector<EffectiveTrack>& eff, int pair);

// ---- checks -------------------------------------------------------------------------------------------------------------------------

std::string analysedProblem(const fs::path& trackDir);      // "" when waveform.bin exists and the grid has a tempo
bool isAnalysed(const fs::path& trackDir);

// "" when the mix can be rendered; facts parallel to mix.tracks.
std::string validateMix(const Mix& mix, const std::vector<TrackFacts>& facts);

} // namespace trakcrate

#endif
