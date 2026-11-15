#ifndef TRAKCRATE_MIX_RENDER_H
#define TRAKCRATE_MIX_RENDER_H

#include "mix_engine.h"

#include <atomic>
#include <functional>
#include <string>

// Renders a studio mix to one mp3 (plan studio-mix_PLAN.md, phase 5). No Qt; LADSPA filters come in as a processor factory.
namespace trakcrate {

struct MixRenderOptions {
    int kbps = 256;
    bool vbr = true;                                    // average bit rate mode around kbps
    StretchCache* stretchCache = nullptr;               // null: the default cache folder
    std::function<std::vector<MixProcessor>()> ladspaFactory;    // the LADSPA filters of mix.filters as processors; may be empty
};

// stage: "checking", "stretching track 3 of 12", "rendering", "finishing"; done / total in 0..1 steps of that stage.
using MixRenderProgress = std::function<void(const std::string& stage, double done, double total)>;

// Grid and length of one track: `audioFile` is the file in the track folder. False with a message when the folder or file cannot be read.
bool readTrackFacts(const fs::path& audioFile, TrackFacts* out, std::string* err);

// Grid and length of every track of the mix, read from the track folders (the folder of MixTrack::file). False with a message on a missing folder.
bool readMixFacts(const Mix& mix, std::vector<TrackFacts>* facts, std::string* err);

// <folder>/<Name>.mp3, then <Name>_2.mp3 ...; never an existing file (or .part).
fs::path uniqueMixOutPath(const fs::path& folder, const std::string& name);

// Checks (every track analysed, validateMix), makes sure every stretch is cached, renders in blocks and writes the mp3 with title (mix name), artist
// (author), album (playlist), comment and the optional cover. Nothing is written on failure or cancel. *outFile is the file made.
bool renderMixToMp3(const Mix& mix, const fs::path& root, const fs::path& outFolder, const MixRenderOptions& opts, const MixRenderProgress& progress,
                    const std::atomic<bool>* cancel, std::string* err, fs::path* outFile);

} // namespace trakcrate

#endif
