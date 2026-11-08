#ifndef TRAKCRATE_EDIT_JOB_H
#define TRAKCRATE_EDIT_JOB_H

#include "edit_library.h"
#include "edit_output.h"
#include "edit_render.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

// The whole job of making an edit, without Qt, so the dialogs only collect settings and run these on a worker thread: decode and render the regions
// (cached while the regions stay the same), then write the file and, for the library, import it (edit_library.h).
namespace trakcrate {

struct EditRequest {
    fs::path trackDir;                 // the original track in the library; only ever read
    BeatGrid grid;                     // its grid
    std::vector<EditRegion> regions;
};

// The decoded original and the last render, kept between the preview and the render dialog. Used by one worker at a time.
struct EditRenderCache {
    fs::path sourceFile;
    std::shared_ptr<DecodedPcm> source;
    std::shared_ptr<DecodedPcm> result;
    EditPlan plan;
    BeatGrid outGrid;
    std::string key;                   // the request the result belongs to
};

// Decodes the original into the cache (once per file). False with err on failure or cancel.
bool ensureSource(EditRenderCache* cache, const fs::path& trackDir, std::string* err, const std::atomic<bool>* cancel = nullptr);
// Decodes the original (once) and renders the regions unless the cache already holds that render. False with err on failure or cancel.
bool ensureRendered(EditRenderCache* cache, const EditRequest& req, std::string* err, const std::atomic<bool>* cancel = nullptr);
// Where a position of the original (ms) is in the rendered edit: in the first segment that holds it (the last one with `last`, the end of a
// loop's repeats). A position inside a cut maps to where the audio after the cut starts; past the end to the end.
double sourceToOutputMs(const EditPlan& plan, double sourceMs, bool last = false, int rate = kEditRate);
// The other way: where in the original the rendered edit is at an output position (ms), for the moving time line. Past the end: the end of the last part.
double outputToSourceMs(const EditPlan& plan, double outputMs, int rate = kEditRate);
// Output positions (ms) of the joins of a plan, for the preview's "next join" buttons.
std::vector<double> joinOutputMs(const EditPlan& plan, int rate = kEditRate);

struct EditOutputParams {
    bool intoLibrary = true;           // else a file in `folder`
    fs::path folder;
    OutFormat format = OutFormat::Wav;
    int mp3Kbps = 256;
    std::string author;
};

struct EditOutcome {
    bool ok = false;
    std::string error;
    fs::path audioFile;                // file-only: the file written (also kept beside it: the .txt); library: the audio file inside the new track
    fs::path trackDir;                 // library only
    std::string playlist;
    int playlistIndex = -1;
    std::string note;
};

// What the original is called in file names: the file name it was imported with, else its folder name, without the extension.
std::string originalStem(const fs::path& trackDir);
// The name the output file will get ("Foo_alice_edit.wav"); for file-only output this is unique in the folder.
std::string plannedFileName(const EditRequest& req, const EditOutputParams& p);
// "Added to playlist House, before Foo" / "No playlist named House" for the dialog.
std::string describePlaylistTarget(const Library& lib, const fs::path& originalTrackDir);

// Writes the rendered cache as the output the params ask for. Requires ensureRendered to have succeeded for req.
EditOutcome writeEditOutput(const EditRenderCache& cache, const EditRequest& req, const EditOutputParams& p, const Library* lib, const std::atomic<bool>* cancel = nullptr);

// A temp .wav of the render for the preview player (tempo tag included); the caller deletes it.
bool writePreviewWav(const EditRenderCache& cache, const fs::path& path, std::string* err);

std::string requestKey(const EditRequest& req);

// Acid loops: one pass of a section of the original, made safe to repeat (renderLoopSample), saved as a sample file with its tempo in the metadata
// (the RIFF acid chunk for .wav, TBPM for .mp3). Never imported into the library.
struct LoopExportParams {
    std::string name;                  // asked for when saving; part of the file name and the title "Foo (<name> <author> Edit)"
    std::string author;
    long long startBeat = 0;
    long long lenBeats = 4;
    EditAlgo algo = EditAlgo::Cut;     // Cut, Cross or Fadeout
    double algoParam = 10;
    OutFormat format = OutFormat::Wav;
    int mp3Kbps = 256;
    fs::path folder;
};
struct LoopOutcome {
    bool ok = false;
    std::string error;
    fs::path file;
};
// "<original stem>_<name>.wav" (unique in the folder when one is given).
std::string plannedLoopFileName(const fs::path& trackDir, const LoopExportParams& p);
LoopOutcome exportLoop(EditRenderCache* cache, const fs::path& trackDir, const BeatGrid& grid, const LoopExportParams& p, const std::atomic<bool>* cancel = nullptr);
// The loop sample repeated `repeats` times into a .wav, for the preview (the sample is click safe, so the repeats join seamlessly).
bool writeLoopPreviewWav(const DecodedPcm& sample, const fs::path& path, int repeats, std::string* err);

} // namespace trakcrate

#endif
