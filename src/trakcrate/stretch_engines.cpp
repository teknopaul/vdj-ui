// The built-in time stretch engines (stretch_engine.h). Engines that need an outside tool run it without a shell on temporary float WAV files.
#include "stretch_engine.h"

#include <algorithm>
#include <cmath>
#include <samplerate.h>
#include <thread>

#ifdef TC_HAVE_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#endif
#ifdef TC_HAVE_SOUNDTOUCH
#include <soundtouch/SoundTouch.h>
#endif

namespace trakcrate {

namespace {

bool cancelled(const std::atomic<bool>* c) { return c && c->load(); }

bool setErr(std::string* err, const std::string& m)
{
    if (err) *err = m;
    return false;
}

bool badRatio(double ratio, std::string* err)
{
    if (!(ratio >= 0.5 - 1e-9 && ratio <= 2.0 + 1e-9)) return !setErr(err, "the stretch ratio must be between 0.5 and 2");
    return false;
}

StretchOption enumOpt(const char* name, const char* label, const char* help, std::vector<std::string> choices, const char* def)
{
    StretchOption o;
    o.name = name;
    o.label = label;
    o.help = help;
    o.kind = StretchOption::Enum;
    o.choices = std::move(choices);
    o.def = def;
    return o;
}

StretchOption boolOpt(const char* name, const char* label, const char* help, bool def)
{
    StretchOption o;
    o.name = name;
    o.label = label;
    o.help = help;
    o.kind = StretchOption::Bool;
    o.def = def ? "true" : "false";
    return o;
}

StretchOption numOpt(const char* name, const char* label, const char* help, double lo, double hi, const char* def)
{
    StretchOption o;
    o.name = name;
    o.label = label;
    o.help = help;
    o.kind = StretchOption::Number;
    o.lo = lo;
    o.hi = hi;
    o.def = def;
    return o;
}

bool isTrue(const StretchOptions& o, const char* name, bool def)
{
    const std::string v = optionValue(o, name, def ? "true" : "false");
    return v == "true" || v == "1" || v == "on";
}

double numberOpt(const StretchOptions& o, const char* name, double def)
{
    const std::string v = optionValue(o, name, "");
    if (v.empty()) return def;
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    return end == v.c_str() ? def : d;
}

std::string fmtNum(double v)
{
    char b[40];
    std::snprintf(b, sizeof b, "%.9g", v);
    return b;
}

// Temp directory removed on scope exit.
struct TempDir {
    fs::path dir;
    explicit TempDir(const char* prefix) { dir = makeTempDir(prefix); }
    ~TempDir()
    {
        std::error_code ec;
        if (!dir.empty()) fs::remove_all(dir, ec);
    }
};

// Writes `in`, runs the command built from the two paths, reads the result back.
bool runToolEngine(const DecodedPcm& in, double ratio, const std::function<std::vector<std::string>(const std::string&, const std::string&)>& argv,
                   DecodedPcm* out, const StretchProgress& progress, const std::atomic<bool>* cancel, std::string* err)
{
    TempDir t("stretch");
    if (t.dir.empty()) return setErr(err, "cannot create a temporary folder");
    const fs::path inPath = t.dir / "in.wav", outPath = t.dir / "out.wav";
    if (!writeWavFloat(inPath, in, err)) return false;
    if (progress) progress(0.05);
    if (cancelled(cancel)) return setErr(err, "cancelled");
    std::string log;
    if (!runCommand(argv(inPath.string(), outPath.string()), cancel, &log, err)) return false;
    if (progress) progress(0.9);
    DecodedPcm res;
    if (!decodeAudio(outPath, &res, err, cancel)) return false;
    fitFrames(&res, stretchedFrames(in.frames(), ratio));
    *out = std::move(res);
    if (progress) progress(1);
    return true;
}

// ---- wsola ---------------------------------------------------------------------------------------------------------------------

class WsolaEngine : public StretchEngine {
public:
    std::string id() const override { return "wsola"; }
    std::string name() const override { return "WSOLA (built in)"; }
    std::string description() const override
    {
        return "Our own waveform-similarity overlap-add. Always available, fast, keeps pitch; sounds smeared on sustained notes and "
               "tonal material but keeps drums reasonably tight. The lowest quality option.";
    }
    std::vector<StretchOption> options() const override
    {
        return {numOpt("window", "Window (ms)", "Length of the overlapping grains. Longer = smoother tones, softer drums.", 20, 120, "46"),
                numOpt("search", "Search (ms)", "How far either way the best splice point is looked for.", 2, 40, "10")};
    }

    bool stretch(const DecodedPcm& in, double ratio, const StretchOptions& opts, DecodedPcm* out, const StretchProgress& progress,
                 const std::atomic<bool>* cancel, std::string* err) override
    {
        if (badRatio(ratio, err)) return false;
        const int rate = in.sampleRate;
        const int hop = std::max(64, int(numberOpt(opts, "window", 46) * rate / 1000.0 / 2));
        const int search = std::max(1, int(numberOpt(opts, "search", 10) * rate / 1000.0));
        const long long frames = (long long)in.frames();
        const size_t target = stretchedFrames(in.frames(), ratio);
        DecodedPcm res;
        res.sampleRate = rate;
        res.stereo.assign(target * 2, 0.0f);
        if (frames == 0 || std::fabs(ratio - 1) < 1e-9) {
            if (std::fabs(ratio - 1) < 1e-9) res = in;
            *out = std::move(res);
            return true;
        }
        std::vector<float> win(size_t(2 * hop));
        for (int i = 0; i < 2 * hop; ++i) win[size_t(i)] = 0.5f * (1.0f - std::cos(2.0f * float(M_PI) * float(i) / float(2 * hop)));
        std::vector<float> overlap(size_t(hop) * 2, 0.0f);
        auto sample = [&](long long f, int ch) -> float { return (f < 0 || f >= frames) ? 0.0f : in.stereo[size_t(f) * 2 + size_t(ch)]; };

        double pos = 0;                         // nominal source position of the next analysis window
        long long lastStart = -1;
        size_t outPos = 0;
        long long block = 0;
        while (outPos < target) {
            if ((++block & 63) == 0) {
                if (cancelled(cancel)) return setErr(err, "cancelled");
                if (progress) progress(std::min(1.0, double(outPos) / double(target)));
            }
            const long long nominal = (long long)pos;
            int d = 0;
            if (lastStart >= 0) {
                const long long ref = lastStart + hop;
                double best = -1e30;
                for (int k = -search; k <= search; k += 2) {
                    const long long s = nominal + k;
                    if (s < 0) continue;
                    double sum = 0, energy = 1e-9;
                    for (int i = 0; i < hop; i += 4) {
                        const float a = sample(ref + i, 0) + sample(ref + i, 1);
                        const float b = sample(s + i, 0) + sample(s + i, 1);
                        sum += double(a) * b;
                        energy += double(b) * b;
                    }
                    const double score = sum / std::sqrt(energy);
                    if (score > best) {
                        best = score;
                        d = k;
                    }
                }
            }
            const long long start = std::max<long long>(0, nominal + d);
            for (int i = 0; i < hop && outPos + size_t(i) < target; ++i) {
                for (int ch = 0; ch < 2; ++ch) {
                    res.stereo[(outPos + size_t(i)) * 2 + size_t(ch)] = overlap[size_t(i) * 2 + size_t(ch)] + sample(start + i, ch) * win[size_t(i)];
                }
            }
            for (int i = 0; i < hop; ++i)
                for (int ch = 0; ch < 2; ++ch) overlap[size_t(i) * 2 + size_t(ch)] = sample(start + hop + i, ch) * win[size_t(hop + i)];
            lastStart = start;
            outPos += size_t(hop);
            pos += hop * ratio;
        }
        *out = std::move(res);
        if (progress) progress(1);
        return true;
    }
};

// ---- varispeed -------------------------------------------------------------------------------------------------------------------

class VarispeedEngine : public StretchEngine {
public:
    std::string id() const override { return "varispeed"; }
    std::string name() const override { return "Varispeed (resampling)"; }
    std::string description() const override
    {
        return "Plays the track faster or slower like a turntable: perfect transients, but the pitch follows the tempo (about one semitone per 6%). "
               "Best for changes of a few percent.";
    }
    bool preservesPitch() const override { return false; }
    std::vector<StretchOption> options() const override
    {
        return {enumOpt("quality", "Quality", "Resampler quality.", {"best", "medium", "fastest"}, "best")};
    }

    bool stretch(const DecodedPcm& in, double ratio, const StretchOptions& opts, DecodedPcm* out, const StretchProgress& progress,
                 const std::atomic<bool>* cancel, std::string* err) override
    {
        if (badRatio(ratio, err)) return false;
        const std::string q = optionValue(opts, "quality", "best");
        const int conv = q == "fastest" ? SRC_SINC_FASTEST : q == "medium" ? SRC_SINC_MEDIUM_QUALITY : SRC_SINC_BEST_QUALITY;
        int error = 0;
        SRC_STATE* st = src_new(conv, 2, &error);
        if (!st) return setErr(err, std::string("libsamplerate: ") + src_strerror(error));
        const size_t target = stretchedFrames(in.frames(), ratio);
        DecodedPcm res;
        res.sampleRate = in.sampleRate;
        res.stereo.assign((target + 4096) * 2, 0.0f);
        size_t inPos = 0, outPos = 0;
        const size_t total = in.frames();
        const size_t chunk = 65536;
        bool ok = true;
        while (outPos < target && ok) {
            if (cancelled(cancel)) {
                setErr(err, "cancelled");
                ok = false;
                break;
            }
            SRC_DATA d{};
            d.data_in = in.stereo.data() + inPos * 2;
            d.input_frames = long(std::min(chunk, total - inPos));
            d.data_out = res.stereo.data() + outPos * 2;
            d.output_frames = long(std::min<size_t>(chunk * 2, target + 4096 - outPos));
            d.src_ratio = 1.0 / ratio;
            d.end_of_input = inPos + size_t(d.input_frames) >= total ? 1 : 0;
            const int rc = src_process(st, &d);
            if (rc != 0) {
                setErr(err, std::string("libsamplerate: ") + src_strerror(rc));
                ok = false;
                break;
            }
            inPos += size_t(d.input_frames_used);
            outPos += size_t(d.output_frames_gen);
            if (progress) progress(std::min(1.0, double(inPos) / double(std::max<size_t>(1, total))));
            if (d.end_of_input && d.output_frames_gen == 0 && d.input_frames_used == 0) break;
        }
        src_delete(st);
        if (!ok) return false;
        fitFrames(&res, target);
        *out = std::move(res);
        return true;
    }
};

// ---- rubber band (command line) ----------------------------------------------------------------------------------------------------

std::vector<StretchOption> rubberbandOptions(bool r3)
{
    std::vector<StretchOption> o;
    o.push_back(enumOpt("formant", "Formants", "Only matters when the pitch is shifted; harmless for pure tempo changes.", {"shifted", "preserved"}, "shifted"));
    if (!r3) o.push_back(enumOpt("crisp", "Crispness", "0 = smooth, 5 = default, 6 = tight (good for drums).", {"0", "1", "2", "3", "4", "5", "6"}, "5"));
    o.push_back(enumOpt("window", r3 ? "Window" : "Window", r3 ? "Short is a quick draft mode." : "Standard, short or long analysis window.",
                        r3 ? std::vector<std::string>{"standard", "short"} : std::vector<std::string>{"standard", "short", "long"}, "standard"));
    if (!r3) o.push_back(boolOpt("smoothing", "Smoothing", "Window presum and time-domain smoothing.", false));
    o.push_back(enumOpt("channels", "Channels", "Process left and right apart, or together (keeps the centre focused).", {"apart", "together"}, "apart"));
    return o;
}

class RubberbandCliEngine : public StretchEngine {
public:
    explicit RubberbandCliEngine(bool r3) : r3_(r3) {}
    std::string id() const override { return r3_ ? "rubberband-r3" : "rubberband-r2"; }
    std::string name() const override { return r3_ ? "Rubber Band R3 (command line)" : "Rubber Band R2 (command line)"; }
    std::string description() const override
    {
        return r3_ ? "The finer Rubber Band engine, run through the rubberband program. Best quality, slow (CPU heavy)."
                   : "The faster Rubber Band engine, run through the rubberband program. Good on drums with the crispness setting, but it drifts the pitch of deep steady bass (about 110 Hz) on test tones: prefer R3 for bass heavy music.";
    }
    std::vector<StretchOption> options() const override { return rubberbandOptions(r3_); }
    bool available(std::string* why) const override
    {
        if (commandExists("rubberband")) return true;
        if (why) *why = "the rubberband program is not installed (apt install rubberband-cli)";
        return false;
    }
    bool stretch(const DecodedPcm& in, double ratio, const StretchOptions& o, DecodedPcm* out, const StretchProgress& progress,
                 const std::atomic<bool>* cancel, std::string* err) override
    {
        if (badRatio(ratio, err)) return false;
        const bool r3 = r3_;
        return runToolEngine(in, ratio, [&](const std::string& i, const std::string& op) {
            std::vector<std::string> a = {"rubberband", "-q", r3 ? "--fine" : "--fast", "--tempo", fmtNum(ratio)};
            if (optionValue(o, "formant", "shifted") == "preserved") a.push_back("--formant");
            if (!r3) a.push_back("--crisp"), a.push_back(optionValue(o, "crisp", "5"));
            const std::string w = optionValue(o, "window", "standard");
            if (w == "short") a.push_back("--window-short");
            if (w == "long" && !r3) a.push_back("--window-long");
            if (!r3 && isTrue(o, "smoothing", false)) a.push_back("--smoothing");
            if (optionValue(o, "channels", "apart") == "together") a.push_back("--centre-focus");
            a.push_back(i);
            a.push_back(op);
            return a;
        }, out, progress, cancel, err);
    }

private:
    bool r3_;
};

#ifdef TC_HAVE_RUBBERBAND
// ---- rubber band (library, in process) ----------------------------------------------------------------------------------------------

class RubberbandLibEngine : public StretchEngine {
public:
    explicit RubberbandLibEngine(bool r3) : r3_(r3) {}
    std::string id() const override { return r3_ ? "rubberband-lib-r3" : "rubberband-lib-r2"; }
    std::string name() const override { return r3_ ? "Rubber Band R3 (library)" : "Rubber Band R2 (library)"; }
    std::string description() const override
    {
        return r3_ ? "The finer Rubber Band engine, in process: no temporary files. Best quality, CPU heavy."
                   : "The faster Rubber Band engine, in process. Good on drums with the crispness setting, but it drifts the pitch of deep steady bass (about 110 Hz) on test tones: prefer R3 for bass heavy music.";
    }
    std::vector<StretchOption> options() const override { return rubberbandOptions(r3_); }

    bool stretch(const DecodedPcm& in, double ratio, const StretchOptions& o, DecodedPcm* out, const StretchProgress& progress,
                 const std::atomic<bool>* cancel, std::string* err) override
    {
        using RB = RubberBand::RubberBandStretcher;
        if (badRatio(ratio, err)) return false;
        int opt = RB::OptionProcessOffline | (r3_ ? RB::OptionEngineFiner : RB::OptionEngineFaster);
        if (optionValue(o, "formant", "shifted") == "preserved") opt |= RB::OptionFormantPreserved;
        const std::string w = optionValue(o, "window", "standard");
        if (w == "short") opt |= RB::OptionWindowShort;
        if (w == "long" && !r3_) opt |= RB::OptionWindowLong;
        if (!r3_) {
            switch (std::atoi(optionValue(o, "crisp", "5").c_str())) {
            case 0: opt |= RB::OptionTransientsSmooth | RB::OptionPhaseIndependent | RB::OptionWindowLong; break;
            case 1: opt |= RB::OptionDetectorSoft | RB::OptionPhaseIndependent | RB::OptionWindowLong; break;
            case 2: opt |= RB::OptionTransientsSmooth | RB::OptionPhaseIndependent; break;
            case 3: opt |= RB::OptionTransientsSmooth; break;
            case 4: opt |= RB::OptionTransientsMixed; break;
            case 6: opt |= RB::OptionPhaseIndependent | RB::OptionWindowShort; break;
            default: break;
            }
            if (isTrue(o, "smoothing", false)) opt |= RB::OptionSmoothingOn;
        }
        if (optionValue(o, "channels", "apart") == "together") opt |= RB::OptionChannelsTogether;

        const size_t frames = in.frames();
        RB rb(size_t(in.sampleRate), 2, opt, 1.0 / ratio, 1.0);
        rb.setExpectedInputDuration(frames);
        const size_t target = stretchedFrames(frames, ratio);
        const size_t block = 8192;
        std::vector<float> l(block), r(block);
        DecodedPcm res;
        res.sampleRate = in.sampleRate;
        res.stereo.reserve((target + 8192) * 2);
        auto feed = [&](bool study) -> bool {
            for (size_t pos = 0; pos < frames || (pos == 0 && frames == 0); pos += block) {
                if (cancelled(cancel)) return false;
                const size_t n = std::min(block, frames - pos);
                for (size_t i = 0; i < n; ++i) {
                    l[i] = in.stereo[(pos + i) * 2];
                    r[i] = in.stereo[(pos + i) * 2 + 1];
                }
                const float* planes[2] = {l.data(), r.data()};
                const bool last = pos + n >= frames;
                if (study) rb.study(planes, n, last);
                else rb.process(planes, n, last);
                if (!study) drain(rb, res);
                if (progress) progress((study ? 0.0 : 0.5) + 0.5 * double(pos + n) / double(std::max<size_t>(1, frames)) * (study ? 0.4 : 1.0));
                if (frames == 0) break;
            }
            return true;
        };
        if (!feed(true) || !feed(false)) return setErr(err, "cancelled");
        for (int spins = 0; !cancelled(cancel);) {
            const int avail = rb.available();
            if (avail < 0) break;
            if (avail > 0) {
                drain(rb, res);
                spins = 0;
            } else if (++spins > 3000) {
                return setErr(err, "Rubber Band did not finish");
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        if (cancelled(cancel)) return setErr(err, "cancelled");
        fitFrames(&res, target);
        *out = std::move(res);
        if (progress) progress(1);
        return true;
    }

private:
    static void drain(RubberBand::RubberBandStretcher& rb, DecodedPcm& res)
    {
        std::vector<float> a, b;
        int avail;
        while ((avail = rb.available()) > 0) {
            a.resize(size_t(avail));
            b.resize(size_t(avail));
            float* planes[2] = {a.data(), b.data()};
            const size_t got = rb.retrieve(planes, size_t(avail));
            for (size_t i = 0; i < got; ++i) {
                res.stereo.push_back(a[i]);
                res.stereo.push_back(b[i]);
            }
        }
    }
    bool r3_;
};
#endif

// ---- sox ---------------------------------------------------------------------------------------------------------------------

class SoxEngine : public StretchEngine {
public:
    std::string id() const override { return "sox-tempo"; }
    std::string name() const override { return "SoX tempo"; }
    std::string description() const override { return "SoX's WSOLA 'tempo' effect, run through the sox program. Good on music, smears less than our WSOLA."; }
    std::vector<StretchOption> options() const override
    {
        return {enumOpt("mode", "Mode", "Tuned for music, speech or a plain linear setting.", {"music", "speech", "linear"}, "music"),
                numOpt("segment", "Segment (ms)", "0 = SoX default.", 0, 200, "0"), numOpt("search", "Search (ms)", "0 = SoX default.", 0, 100, "0"),
                numOpt("overlap", "Overlap (ms)", "0 = SoX default.", 0, 100, "0")};
    }
    bool available(std::string* why) const override
    {
        if (commandExists("sox")) return true;
        if (why) *why = "the sox program is not installed (apt install sox)";
        return false;
    }
    bool stretch(const DecodedPcm& in, double ratio, const StretchOptions& o, DecodedPcm* out, const StretchProgress& progress,
                 const std::atomic<bool>* cancel, std::string* err) override
    {
        if (badRatio(ratio, err)) return false;
        return runToolEngine(in, ratio, [&](const std::string& i, const std::string& op) {
            std::vector<std::string> a = {"sox", i, "-e", "floating-point", "-b", "32", op, "tempo"};
            const std::string m = optionValue(o, "mode", "music");
            a.push_back(m == "speech" ? "-s" : m == "linear" ? "-l" : "-m");
            a.push_back(fmtNum(ratio));
            const double seg = numberOpt(o, "segment", 0), sea = numberOpt(o, "search", 0), ov = numberOpt(o, "overlap", 0);
            if (seg > 0 || sea > 0 || ov > 0) {
                a.push_back(fmtNum(seg > 0 ? seg : (m == "speech" ? 35 : m == "linear" ? 20 : 82)));
                if (sea > 0 || ov > 0) a.push_back(fmtNum(sea > 0 ? sea : (m == "speech" ? 30 : m == "linear" ? 20 : 14)));
                if (ov > 0) a.push_back(fmtNum(ov));
            }
            return a;
        }, out, progress, cancel, err);
    }
};

// ---- ffmpeg ------------------------------------------------------------------------------------------------------------------

bool ffmpegHasFilter(const char* filter)
{
    static std::mutex mu;
    static std::map<std::string, bool> cache;
    std::lock_guard<std::mutex> g(mu);
    auto it = cache.find(filter);
    if (it != cache.end()) return it->second;
    bool has = false;
    if (commandExists("ffmpeg")) {
        std::string log;
        if (runCommand({"ffmpeg", "-hide_banner", "-filters"}, nullptr, &log, nullptr)) has = log.find(std::string(" ") + filter + " ") != std::string::npos;
    }
    cache[filter] = has;
    return has;
}

class FfmpegAtempoEngine : public StretchEngine {
public:
    std::string id() const override { return "ffmpeg-atempo"; }
    std::string name() const override { return "ffmpeg atempo"; }
    std::string description() const override { return "The ffmpeg 'atempo' filter (a WSOLA). Plain quality, no settings."; }
    bool available(std::string* why) const override
    {
        if (ffmpegHasFilter("atempo")) return true;
        if (why) *why = "ffmpeg with the atempo filter is not installed";
        return false;
    }
    bool stretch(const DecodedPcm& in, double ratio, const StretchOptions&, DecodedPcm* out, const StretchProgress& progress,
                 const std::atomic<bool>* cancel, std::string* err) override
    {
        if (badRatio(ratio, err)) return false;
        return runToolEngine(in, ratio, [&](const std::string& i, const std::string& op) {
            return std::vector<std::string>{"ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", i, "-af", "atempo=" + fmtNum(ratio), "-c:a", "pcm_f32le", op};
        }, out, progress, cancel, err);
    }
};

class FfmpegRubberbandEngine : public StretchEngine {
public:
    std::string id() const override { return "ffmpeg-rubberband"; }
    std::string name() const override { return "ffmpeg rubberband filter"; }
    std::string description() const override { return "Rubber Band (R2) through the ffmpeg 'rubberband' filter."; }
    std::vector<StretchOption> options() const override
    {
        return {enumOpt("transients", "Transients", "How attacks are handled.", {"crisp", "mixed", "smooth"}, "crisp"),
                enumOpt("window", "Window", "Analysis window.", {"standard", "short", "long"}, "standard"),
                enumOpt("smoothing", "Smoothing", "Time-domain smoothing.", {"off", "on"}, "off")};
    }
    bool available(std::string* why) const override
    {
        if (ffmpegHasFilter("rubberband")) return true;
        if (why) *why = "ffmpeg with the rubberband filter is not installed";
        return false;
    }
    bool stretch(const DecodedPcm& in, double ratio, const StretchOptions& o, DecodedPcm* out, const StretchProgress& progress,
                 const std::atomic<bool>* cancel, std::string* err) override
    {
        if (badRatio(ratio, err)) return false;
        const std::string f = "rubberband=tempo=" + fmtNum(ratio) + ":transients=" + optionValue(o, "transients", "crisp") +
                              ":window=" + optionValue(o, "window", "standard") + ":smoothing=" + optionValue(o, "smoothing", "off");
        return runToolEngine(in, ratio, [&](const std::string& i, const std::string& op) {
            return std::vector<std::string>{"ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", i, "-af", f, "-c:a", "pcm_f32le", op};
        }, out, progress, cancel, err);
    }
};

#ifdef TC_HAVE_SOUNDTOUCH
// Written against the SoundTouch 2.x C++ API; not built or tested on the machine this was written on (no headers).
class SoundTouchEngine : public StretchEngine {
public:
    std::string id() const override { return "soundtouch"; }
    std::string name() const override { return "SoundTouch"; }
    std::string description() const override { return "SoundTouch's WSOLA with beat-friendly defaults. Good value, light on CPU."; }
    std::vector<StretchOption> options() const override
    {
        return {numOpt("sequence", "Sequence (ms)", "0 = automatic.", 0, 200, "0"), numOpt("seekwindow", "Seek window (ms)", "0 = automatic.", 0, 100, "0"),
                numOpt("overlap", "Overlap (ms)", "0 = automatic.", 0, 50, "0"), boolOpt("quickseek", "Quick seek", "Faster, slightly worse.", false)};
    }
    bool stretch(const DecodedPcm& in, double ratio, const StretchOptions& o, DecodedPcm* out, const StretchProgress& progress,
                 const std::atomic<bool>* cancel, std::string* err) override
    {
        if (badRatio(ratio, err)) return false;
        soundtouch::SoundTouch st;
        st.setSampleRate(uint(in.sampleRate));
        st.setChannels(2);
        st.setTempo(float(ratio));
        if (numberOpt(o, "sequence", 0) > 0) st.setSetting(SETTING_SEQUENCE_MS, int(numberOpt(o, "sequence", 0)));
        if (numberOpt(o, "seekwindow", 0) > 0) st.setSetting(SETTING_SEEKWINDOW_MS, int(numberOpt(o, "seekwindow", 0)));
        if (numberOpt(o, "overlap", 0) > 0) st.setSetting(SETTING_OVERLAP_MS, int(numberOpt(o, "overlap", 0)));
        st.setSetting(SETTING_USE_QUICKSEEK, isTrue(o, "quickseek", false) ? 1 : 0);
        DecodedPcm res;
        res.sampleRate = in.sampleRate;
        const size_t frames = in.frames(), block = 8192;
        std::vector<float> buf(block * 2);
        auto drain = [&]() {
            uint n;
            while ((n = st.receiveSamples(buf.data(), uint(block))) > 0) res.stereo.insert(res.stereo.end(), buf.begin(), buf.begin() + n * 2);
        };
        for (size_t pos = 0; pos < frames; pos += block) {
            if (cancelled(cancel)) return setErr(err, "cancelled");
            st.putSamples(in.stereo.data() + pos * 2, uint(std::min(block, frames - pos)));
            drain();
            if (progress) progress(double(pos) / double(frames));
        }
        st.flush();
        drain();
        fitFrames(&res, stretchedFrames(frames, ratio));
        *out = std::move(res);
        return true;
    }
};
#endif

} // namespace

void registerBuiltinStretchEngines(StretchRegistry* reg)
{
#ifdef TC_HAVE_RUBBERBAND
    reg->add(std::make_unique<RubberbandLibEngine>(true));
#endif
    reg->add(std::make_unique<RubberbandCliEngine>(true));
#ifdef TC_HAVE_RUBBERBAND
    reg->add(std::make_unique<RubberbandLibEngine>(false));
#endif
    reg->add(std::make_unique<RubberbandCliEngine>(false));
#ifdef TC_HAVE_SOUNDTOUCH
    reg->add(std::make_unique<SoundTouchEngine>());
#endif
    reg->add(std::make_unique<SoxEngine>());
    reg->add(std::make_unique<FfmpegRubberbandEngine>());
    reg->add(std::make_unique<FfmpegAtempoEngine>());
    reg->add(std::make_unique<WsolaEngine>());
    reg->add(std::make_unique<VarispeedEngine>());
}

} // namespace trakcrate
