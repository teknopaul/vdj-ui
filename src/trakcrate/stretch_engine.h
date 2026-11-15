#ifndef TRAKCRATE_STRETCH_ENGINE_H
#define TRAKCRATE_STRETCH_ENGINE_H

#include "audio_decode.h"
#include "stretch_choice.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Pluggable offline time stretching for studio mixes (plan studio-mix_PLAN.md, phase 3). No Qt.
// ratio = target bpm / source bpm: the output has frames / ratio frames, > 1 speeds up.
namespace trakcrate {

struct StretchOption {
    enum Kind { Enum, Bool, Number };
    std::string name, label, help;
    Kind kind = Enum;
    std::vector<std::string> choices;       // Enum
    double lo = 0, hi = 0;                  // Number
    std::string def;                        // the default as text ("true" / "false" for Bool)
};

using StretchProgress = std::function<void(double fraction)>;       // 0..1, may be empty

class StretchEngine {
public:
    virtual ~StretchEngine() = default;
    virtual std::string id() const = 0;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual std::vector<StretchOption> options() const { return {}; }
    virtual bool available(std::string* why) const { (void)why; return true; }
    virtual bool preservesPitch() const { return true; }
    virtual int version() const { return 1; }                       // bump when the output of the engine changes
    virtual std::string fingerprint() const { return std::string(); }    // extra text for the cache key (an external command line)

    // Always returns round(frames / ratio) frames. Ratios 0.5..2. `cancel` and `progress` may be null.
    virtual bool stretch(const DecodedPcm& in, double ratio, const StretchOptions& opts, DecodedPcm* out, const StretchProgress& progress,
                         const std::atomic<bool>* cancel, std::string* err) = 0;
};

class StretchRegistry {
public:
    struct Entry {
        StretchEngine* engine;
        bool available;
        std::string why;        // when not available
    };

    static StretchRegistry& instance();                // builtins are registered on first use

    // False (and nothing added) when the id is taken.
    bool add(std::unique_ptr<StretchEngine> engine);
    bool remove(const std::string& id);
    StretchEngine* find(const std::string& id) const;
    std::vector<Entry> list() const;                    // order of registration
    // The best available engine (rubberband R3 first, wsola last).
    std::string defaultEngineId() const;

    // The choice with unknown options dropped and missing ones filled with the engine's defaults. An unknown engine keeps its options.
    StretchChoice normalize(const StretchChoice& c) const;
    // A track's choice, else the mix default, else the default engine.
    StretchChoice resolveChoice(const StretchChoice& track, const StretchChoice& mixDefault) const;

    void registerBuiltins();                            // idempotent

private:
    mutable std::mutex mu_;
    std::vector<std::shared_ptr<StretchEngine>> engines_;
    bool builtins_ = false;
};

// "engine|name=value|..." with the options in name order.
std::string choiceKey(const StretchChoice& c);

// The value of an option, else the fallback.
std::string optionValue(const StretchOptions& o, const std::string& name, const std::string& def);

// ---- helpers shared by the engines -----------------------------------------------------------------------------------------------

// Runs argv without a shell, output (stdout + stderr) captured. Killed when `cancel` is set. False on a non-zero exit (err = the last line of output).
bool runCommand(const std::vector<std::string>& argv, const std::atomic<bool>* cancel, std::string* output, std::string* err);
bool commandExists(const std::string& name);            // on PATH
// 32 bit float stereo WAV at the PCM's sample rate.
bool writeWavFloat(const fs::path& path, const DecodedPcm& pcm, std::string* err);
// Pads with silence or trims at the end so that the PCM has exactly `frames` frames.
void fitFrames(DecodedPcm* pcm, size_t frames);
size_t stretchedFrames(size_t frames, double ratio);

// *.stretch files (external command engines). Engines found are added to the registry; every problem is appended to `problems` (not fatal).
std::vector<std::string> defaultStretchDirs(const fs::path& libraryRoot);       // <root>/stretchers, ~/.config/vdj-ui/stretchers
void loadExternalEngines(const std::vector<fs::path>& dirs, StretchRegistry* reg, std::vector<std::string>* problems);
// Parses one file; null engine and a message in err when it is bad.
std::unique_ptr<StretchEngine> parseExternalEngine(const std::string& text, std::string* err);

void registerBuiltinStretchEngines(StretchRegistry* reg);

} // namespace trakcrate

#endif
