#include "mix_model.h"

#include "track.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace trakcrate {

namespace {

const char* const kTypeNames[kMixTypeCount] = {"cross-fade", "cross-fade-drop", "bass-drop", "rough-cut", "drop-cut", "stop-cut", "tail-cut", "bass-switch"};

double clampv(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }

std::string fmt(const char* f, double a, double b = 0)
{
    char buf[160];
    std::snprintf(buf, sizeof buf, f, a, b);
    return buf;
}

std::string trackTag(size_t i) { return "Track " + std::to_string(i + 1) + ": "; }

// Positions of a type: which curve of which side gets which points (beats relative to the pair, resolved by the caller).
struct P { double beat, value; };

struct TypePoints {
    std::vector<P> outVol, inVol, outBass, inBass;
    bool hasOutVol = false, hasInVol = false, hasOutBass = false, hasInBass = false;
};

TypePoints pointsFor(MixType type, double o0, double i0, double L)
{
    const double oE = o0 + L, iE = i0 + L;
    TypePoints t;
    auto set = [](std::vector<P>& dst, bool& has, std::vector<P> pts) {
        dst = std::move(pts);
        has = true;
    };
    const std::vector<P> inSilent = {{i0, 0}, {iE, 0}, {iE, 100}};
    switch (type) {
    case MixType::CrossFade:
        set(t.outVol, t.hasOutVol, {{o0, 100}, {oE, 0}});
        set(t.inVol, t.hasInVol, {{i0, 0}, {iE, 100}});
        break;
    case MixType::CrossFadeDrop:
        set(t.outVol, t.hasOutVol, {{o0, 100}, {oE, 0}});
        set(t.inVol, t.hasInVol, {{i0, 0}, {iE, 100}});
        set(t.inBass, t.hasInBass, inSilent);
        break;
    case MixType::BassDrop:
        set(t.outVol, t.hasOutVol, {{o0, 100}, {oE, 100}, {oE, 0}});
        set(t.inVol, t.hasInVol, {{i0, 0}, {iE, 100}});
        set(t.outBass, t.hasOutBass, {{o0, 100}, {oE, 0}});
        set(t.inBass, t.hasInBass, inSilent);
        break;
    case MixType::RoughCut:
        set(t.outVol, t.hasOutVol, {{o0, 100}, {oE, 100}, {oE, 0}});
        set(t.inVol, t.hasInVol, inSilent);
        break;
    case MixType::DropCut:
        set(t.outVol, t.hasOutVol, {{o0, 100}, {oE - 4, 100}, {oE, 0}});
        set(t.inVol, t.hasInVol, inSilent);
        break;
    case MixType::StopCut:
        set(t.outVol, t.hasOutVol, {{o0, 100}, {oE - 2, 100}, {oE - 1, 0}});
        set(t.inVol, t.hasInVol, inSilent);
        break;
    case MixType::TailCut:
        set(t.outVol, t.hasOutVol, {{o0, 100}, {oE, 100}, {oE + 8, 0}});
        set(t.inVol, t.hasInVol, inSilent);
        break;
    case MixType::BassSwitch: {
        const double half = 4.0 * std::lround(L / 8);
        const double mo = o0 + half, mi = i0 + half;
        set(t.outVol, t.hasOutVol, {{o0, 100}, {oE, 100}, {oE, 0}});
        set(t.inVol, t.hasInVol, {{i0 - 8, 0}, {i0, 100}});
        set(t.outBass, t.hasOutBass, {{o0, 100}, {mo, 100}, {mo, 0}});
        set(t.inBass, t.hasInBass, {{i0 - 8, 0}, {mi, 0}, {mi, 100}});
        break;
    }
    }
    return t;
}

// Replaces the points of `c` inside [w0, w1] by `pts` (clamped to [0, maxBeat]); a curve the type does not write is reset to 100 % there.
void writeWindow(Curve* c, double w0, double w1, double maxBeat, const std::vector<P>* pts)
{
    auto& v = c->points;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const CurvePoint& p) { return p.beat >= w0 - 1e-9 && p.beat <= w1 + 1e-9; }), v.end());
    if (pts) {
        for (const P& p : *pts) setPoint(c, clampv(p.beat, 0, maxBeat), p.value);
    } else if (!curveIsDefault(*c)) {
        const double a = clampv(w0, 0, maxBeat), b = clampv(w1, 0, maxBeat);
        if (b > a) {
            setPoint(c, a, 100);
            setPoint(c, b, 100);
        }
    }
}

} // namespace

const char* mixTypeName(MixType t) { return kTypeNames[int(t)]; }

bool mixTypeFromName(const std::string& name, MixType* out)
{
    for (int i = 0; i < kMixTypeCount; ++i) {
        if (name == kTypeNames[i]) {
            if (out) *out = MixType(i);
            return true;
        }
    }
    return false;
}

std::vector<MixFilter> defaultFilters()
{
    MixFilter f;
    f.kind = MixFilterKind::BuiltinCompressor;
    f.on = true;
    f.params = {{"threshold", -3}, {"ratio", 10}, {"attack", 1}, {"release", 80}};
    return {f};
}

// ---- curves ------------------------------------------------------------------------------------------------------------------------

std::vector<CurvePoint> effectiveCurve(const Curve& c, double bpm)
{
    std::vector<CurvePoint> out = c.points;
    const double ramp = bpm > 0 ? kMinRampMs / 1000.0 * bpm / 60.0 : 0;
    for (size_t k = 1; k < out.size(); ++k) {
        const double floorBeat = out[k].value != out[k - 1].value ? out[k - 1].beat + ramp : out[k - 1].beat;
        out[k].beat = std::max(out[k].beat, floorBeat);
    }
    return out;
}

double curveGainAt(const std::vector<CurvePoint>& e, double beat)
{
    if (e.empty()) return 1;
    if (beat <= e.front().beat) return e.front().value / 100.0;
    if (beat >= e.back().beat) return e.back().value / 100.0;
    // First point strictly after the beat; e is sorted by beat.
    auto it = std::upper_bound(e.begin(), e.end(), beat, [](double b, const CurvePoint& p) { return b < p.beat; });
    const CurvePoint& hi = *it;
    const CurvePoint& lo = *(it - 1);
    const double span = hi.beat - lo.beat;
    const double f = span > 0 ? (beat - lo.beat) / span : 1;
    return (lo.value + (hi.value - lo.value) * f) / 100.0;
}

double curveGain(const Curve& c, double beat, double bpm) { return curveGainAt(effectiveCurve(c, bpm), beat); }

int setPoint(Curve* c, double beat, double value)
{
    CurvePoint p{beat, clampv(value, 0, 100)};
    auto it = std::upper_bound(c->points.begin(), c->points.end(), beat, [](double b, const CurvePoint& q) { return b < q.beat; });
    const int idx = int(it - c->points.begin());
    c->points.insert(it, p);
    return idx;
}

int movePoint(Curve* c, int index, double beat, double value)
{
    if (index < 0 || index >= int(c->points.size())) return -1;
    c->points.erase(c->points.begin() + index);
    return setPoint(c, beat, value);
}

void removePoint(Curve* c, int index)
{
    if (index >= 0 && index < int(c->points.size())) c->points.erase(c->points.begin() + index);
}

bool curveIsDefault(const Curve& c)
{
    for (const CurvePoint& p : c.points)
        if (p.value != 100) return false;
    return true;
}

// ---- per track ------------------------------------------------------------------------------------------------------------------

double defaultMasterBpm(const std::vector<TrackFacts>& tracks)
{
    if (!tracks.empty() && tracks.front().grid.bpm > 0) return tracks.front().grid.bpm;
    for (const TrackFacts& f : tracks)
        if (f.grid.bpm > 0) return f.grid.bpm;
    return 120;
}

double stretchRatio(double masterBpm, const BeatGrid& grid, bool stretchOff)
{
    if (stretchOff || grid.bpm <= 0 || masterBpm <= 0) return 1;
    return masterBpm / grid.bpm;
}

bool stretchImpossible(double ratio) { return ratio < kMinRatio - 1e-9 || ratio > kMaxRatio + 1e-9; }

std::string stretchWarning(double ratio)
{
    const double pct = (ratio - 1) * 100;
    if (stretchImpossible(ratio)) return fmt("a tempo change of %+.1f%% is not possible (limit 0.5x to 2x)", pct);
    if (std::fabs(ratio - 1) > kMaxStretchWarn) return fmt("a tempo change of %+.1f%% will probably be audible (artefacts)", pct);
    return std::string();
}

EffectiveTrack effectiveTrack(const Mix& mix, const MixTrack& track, const TrackFacts& facts)
{
    EffectiveTrack e;
    e.stretchedGrid = facts.grid;
    e.stretchedFrames = e.editedFrames = facts.frames;
    e.grid = facts.grid;
    e.bpm = facts.grid.bpm;
    if (facts.grid.empty()) {
        e.error = "no beat grid";
        return e;
    }
    e.ratio = stretchRatio(mix.masterBpm, facts.grid, track.stretchOff);
    BeatGrid g = facts.grid;
    g.firstBeatUs += std::llround(track.gridNudgeMs * 1000);
    g.firstBeatUs = std::llround(double(g.firstBeatUs) / e.ratio);
    if (!track.stretchOff) g.bpm = mix.masterBpm;
    e.stretchedGrid = g;
    e.stretchedFrames = std::llround(double(facts.frames) / e.ratio);

    std::string verr = validateRegions(g, track.regions, e.stretchedFrames);
    e.plan = planEdit(g, verr.empty() ? track.regions : std::vector<EditRegion>(), e.stretchedFrames);
    if (!verr.empty()) e.error = verr;
    e.editedFrames = planOutputFrames(e.plan);
    e.grid = outputGrid(g, e.plan);
    e.bpm = e.grid.bpm;
    e.editedBeats = (double(e.editedFrames) / kEditRate - double(e.grid.firstBeatUs) / 1e6) * e.bpm / 60.0;
    e.ok = e.error.empty();
    return e;
}

std::vector<EffectiveTrack> effectiveTracks(const Mix& mix, const std::vector<TrackFacts>& facts)
{
    std::vector<EffectiveTrack> out;
    for (size_t i = 0; i < mix.tracks.size() && i < facts.size(); ++i) out.push_back(effectiveTrack(mix, mix.tracks[i], facts[i]));
    return out;
}

double trackBeatMs(const EffectiveTrack& t, double beat)
{
    if (t.bpm <= 0) return 0;
    return double(t.grid.firstBeatUs) / 1000.0 + beat * 60000.0 / t.bpm;
}

double trackMsBeat(const EffectiveTrack& t, double ms)
{
    if (t.bpm <= 0) return 0;
    return (ms - double(t.grid.firstBeatUs) / 1000.0) * t.bpm / 60000.0;
}

// ---- timeline -------------------------------------------------------------------------------------------------------------------------

std::vector<double> trackStartMs(const Mix& mix, const std::vector<EffectiveTrack>& eff)
{
    std::vector<double> s;
    const size_t n = std::min(mix.tracks.size(), eff.size());
    for (size_t i = 0; i < n; ++i) {
        if (i == 0) {
            s.push_back(0);
            continue;
        }
        const MixPair& p = mix.tracks[i - 1].next;
        s.push_back(s[i - 1] + trackBeatMs(eff[i - 1], p.outStartBeat) - trackBeatMs(eff[i], p.inStartBeat));
    }
    return s;
}

double mixLengthMs(const std::vector<double>& starts, const std::vector<EffectiveTrack>& eff)
{
    double end = 0;
    for (size_t i = 0; i < starts.size() && i < eff.size(); ++i) end = std::max(end, starts[i] + double(eff[i].editedFrames) * 1000.0 / kEditRate);
    return end;
}

double beatToMixMs(const std::vector<double>& starts, const std::vector<EffectiveTrack>& eff, int track, double beat)
{
    return starts[size_t(track)] + trackBeatMs(eff[size_t(track)], beat);
}

double mixMsToBeat(const std::vector<double>& starts, const std::vector<EffectiveTrack>& eff, int track, double ms)
{
    return trackMsBeat(eff[size_t(track)], ms - starts[size_t(track)]);
}

// ---- building ---------------------------------------------------------------------------------------------------------------------------

Mix defaultMix(const std::string& name, const std::vector<MixTrackInput>& tracks)
{
    Mix mix;
    mix.name = name;
    std::vector<TrackFacts> facts;
    for (const MixTrackInput& in : tracks) {
        MixTrack t;
        t.file = in.file;
        t.regions = in.regions;
        mix.tracks.push_back(t);
        facts.push_back(in.facts);
    }
    mix.masterBpm = defaultMasterBpm(facts);
    const std::vector<EffectiveTrack> eff = effectiveTracks(mix, facts);

    for (size_t i = 0; i + 1 < mix.tracks.size(); ++i) {
        MixPair& p = mix.tracks[i].next;
        const double shortest = std::min(eff[i].editedBeats, eff[i + 1].editedBeats);
        p.lenBeats = std::min(32.0, std::max(4.0, 4.0 * std::floor(shortest / 4.0)));
        p.outStartBeat = std::max(0.0, 4.0 * std::floor((eff[i].editedBeats - p.lenBeats) / 4.0));
        p.inStartBeat = 0;
        p.type = MixType::CrossFade;
        applyMixType(&mix, int(i), MixType::CrossFade, eff);
    }
    return mix;
}

void applyMixType(Mix* mix, int pairIndex, MixType type, const std::vector<EffectiveTrack>& eff)
{
    if (pairIndex < 0 || size_t(pairIndex) + 1 >= mix->tracks.size() || size_t(pairIndex) + 1 >= eff.size()) return;
    MixTrack& out = mix->tracks[size_t(pairIndex)];
    MixTrack& in = mix->tracks[size_t(pairIndex) + 1];
    MixPair& p = out.next;
    const double o0 = p.outStartBeat, i0 = p.inStartBeat, L = p.lenBeats;
    const TypePoints t = pointsFor(type, o0, i0, L);
    const double outMax = eff[size_t(pairIndex)].editedBeats, inMax = eff[size_t(pairIndex) + 1].editedBeats;
    const double ow0 = o0, ow1 = o0 + L + 8, iw0 = i0 - 8, iw1 = i0 + L;

    writeWindow(&out.volume, ow0, ow1, outMax, t.hasOutVol ? &t.outVol : nullptr);
    writeWindow(&out.bass, ow0, ow1, outMax, t.hasOutBass ? &t.outBass : nullptr);
    writeWindow(&out.high, ow0, ow1, outMax, nullptr);
    writeWindow(&in.volume, iw0, iw1, inMax, t.hasInVol ? &t.inVol : nullptr);
    writeWindow(&in.bass, iw0, iw1, inMax, t.hasInBass ? &t.inBass : nullptr);
    writeWindow(&in.high, iw0, iw1, inMax, nullptr);
    p.type = type;
}

namespace {
void dropPoints(MixTrack* t, bool before, double beat)
{
    for (Curve* c : {&t->volume, &t->bass, &t->high})
        c->points.erase(std::remove_if(c->points.begin(), c->points.end(), [&](const CurvePoint& p) { return before ? p.beat <= beat : p.beat >= beat; }), c->points.end());
}
}

bool removeTrack(Mix* mix, int index, const std::vector<TrackFacts>& factsBefore)
{
    if (index < 0 || size_t(index) >= mix->tracks.size() || mix->tracks.size() < 2 || factsBefore.size() != mix->tracks.size()) return false;
    const MixPair removedNext = mix->tracks[size_t(index)].next;
    const MixType prevType = index > 0 ? mix->tracks[size_t(index) - 1].next.type : MixType::CrossFade;
    const double prevOut = index > 0 ? mix->tracks[size_t(index) - 1].next.outStartBeat : 0;
    std::vector<TrackFacts> facts = factsBefore;
    facts.erase(facts.begin() + index);
    mix->tracks.erase(mix->tracks.begin() + index);
    const int n = int(mix->tracks.size());
    if (index < n) dropPoints(&mix->tracks[size_t(index)], true, removedNext.inStartBeat + removedNext.lenBeats);       // the incoming side of the gap
    if (index > 0) dropPoints(&mix->tracks[size_t(index) - 1], false, prevOut);                                          // the outgoing side
    if (index > 0 && index < n) {
        const std::vector<EffectiveTrack> eff = effectiveTracks(*mix, facts);
        const size_t i = size_t(index) - 1;
        MixPair& p = mix->tracks[i].next;
        const double shortest = std::min(eff[i].editedBeats, eff[i + 1].editedBeats);
        p.lenBeats = std::min(32.0, std::max(4.0, 4.0 * std::floor(shortest / 4.0)));
        p.outStartBeat = std::max(0.0, 4.0 * std::floor((eff[i].editedBeats - p.lenBeats) / 4.0));
        p.inStartBeat = 0;
        applyMixType(mix, int(i), prevType, eff);
    }
    return true;
}

double snapBeat(double beat, bool toBars) { return toBars ? 4.0 * std::round(beat / 4.0) : std::round(beat); }

void setPairOutStart(Mix* mix, int pair, double beat, const std::vector<EffectiveTrack>& eff)
{
    if (pair < 0 || size_t(pair) + 1 >= mix->tracks.size() || size_t(pair) + 1 >= eff.size()) return;
    MixPair& p = mix->tracks[size_t(pair)].next;
    p.outStartBeat = clampv(beat, 0, std::max(0.0, eff[size_t(pair)].editedBeats - 1));
    p.lenBeats = std::max(1.0, std::min(p.lenBeats, eff[size_t(pair)].editedBeats - p.outStartBeat));
}

void setPairInStart(Mix* mix, int pair, double beat, const std::vector<EffectiveTrack>& eff)
{
    if (pair < 0 || size_t(pair) + 1 >= mix->tracks.size() || size_t(pair) + 1 >= eff.size()) return;
    MixPair& p = mix->tracks[size_t(pair)].next;
    p.inStartBeat = clampv(beat, 0, std::max(0.0, eff[size_t(pair) + 1].editedBeats - 1));
    p.lenBeats = std::max(1.0, std::min(p.lenBeats, eff[size_t(pair) + 1].editedBeats - p.inStartBeat));
}

void setPairLength(Mix* mix, int pair, double lenBeats, const std::vector<EffectiveTrack>& eff)
{
    if (pair < 0 || size_t(pair) + 1 >= mix->tracks.size() || size_t(pair) + 1 >= eff.size()) return;
    MixPair& p = mix->tracks[size_t(pair)].next;
    const double room = std::min(eff[size_t(pair)].editedBeats - p.outStartBeat, eff[size_t(pair) + 1].editedBeats - p.inStartBeat);
    p.lenBeats = std::max(1.0, std::min(lenBeats, room));
}

bool pairTempoDiffers(const std::vector<EffectiveTrack>& eff, int pair)
{
    if (pair < 0 || size_t(pair) + 1 >= eff.size() || eff[size_t(pair)].bpm <= 0 || eff[size_t(pair) + 1].bpm <= 0) return false;
    return std::fabs(eff[size_t(pair)].bpm / eff[size_t(pair) + 1].bpm - 1) > 0.02;
}

// ---- checks ------------------------------------------------------------------------------------------------------------------------------

std::string analysedProblem(const fs::path& trackDir)
{
    Track t;
    if (!loadTrack(trackDir, &t)) return "track folder not found";
    if (!t.hasGrid) return "not analysed: no beat grid";
    if (beatGridFrom(t.grid).empty()) return "not analysed: no tempo found";
    std::error_code ec;
    if (!fs::exists(trackDir / "waveform.bin", ec)) return "not analysed: no waveform";
    return std::string();
}

bool isAnalysed(const fs::path& trackDir) { return analysedProblem(trackDir).empty(); }

std::string validateMix(const Mix& mix, const std::vector<TrackFacts>& facts)
{
    if (mix.tracks.empty()) return "the mix has no tracks";
    if (facts.size() != mix.tracks.size()) return "internal error: track facts do not match the mix";
    if (!(mix.masterBpm >= 40 && mix.masterBpm <= 300)) return "the master tempo must be between 40 and 300 bpm";
    if (mix.slopeDb != 12 && mix.slopeDb != 24 && mix.slopeDb != 48) return "the EQ slope must be 12, 24 or 48 dB/octave";
    if (!(mix.bassHz > 0 && mix.bassHz < mix.highHz)) return "the bass crossover must be below the high crossover";

    const std::vector<EffectiveTrack> eff = effectiveTracks(mix, facts);
    const std::vector<double> starts = trackStartMs(mix, eff);
    for (size_t i = 0; i < mix.tracks.size(); ++i) {
        const MixTrack& t = mix.tracks[i];
        if (t.missing) return trackTag(i) + "the audio file is missing";
        if (!eff[i].error.empty()) return trackTag(i) + eff[i].error;
        const double ratio = eff[i].ratio;
        if (stretchImpossible(ratio)) return trackTag(i) + stretchWarning(ratio);
        if (!(t.trim >= 0 && t.trim <= 200)) return trackTag(i) + "the volume trim must be 0 to 200 %";
        const Curve* curves[3] = {&t.volume, &t.bass, &t.high};
        for (const Curve* c : curves) {
            for (size_t k = 0; k < c->points.size(); ++k) {
                const CurvePoint& p = c->points[k];
                if (!(p.value >= 0 && p.value <= 100)) return trackTag(i) + "a curve value is outside 0 to 100 %";
                if (!std::isfinite(p.beat)) return trackTag(i) + "a curve point has no position";
                if (k && p.beat < c->points[k - 1].beat) return trackTag(i) + "curve points are not in order";
            }
        }
        if (i + 1 < mix.tracks.size()) {
            const MixPair& p = t.next;
            if (!(p.lenBeats > 0)) return trackTag(i) + "the mix period must be longer than zero";
            const double eps = 0.02;
            if (p.outStartBeat < -eps || p.outStartBeat + p.lenBeats > eff[i].editedBeats + eps) return trackTag(i) + "the mix period is not inside the track";
            if (p.inStartBeat < -eps || p.inStartBeat + p.lenBeats > eff[i + 1].editedBeats + eps)
                return trackTag(i + 1) + "the mix period is not inside the track";
        }
        if (starts[i] < -0.5) return trackTag(i) + "would start before the mix begins; move the mix period";
    }
    return std::string();
}

} // namespace trakcrate
