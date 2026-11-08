#include "edit_library.h"

#include "analysis.h"
#include "grid_edit.h"
#include "importer.h"

#include <algorithm>

namespace trakcrate {

namespace {

fs::path canon(const fs::path& p)
{
    std::error_code ec;
    fs::path c = fs::weakly_canonical(p, ec);
    return ec ? p.lexically_normal() : c;
}

} // namespace

EditImportResult importEditToLibrary(const Library& lib, const fs::path& originalTrackDir, const fs::path& renderedFile, const fs::path& infoFile,
                                     const BeatGrid& outputGrid, const std::atomic<bool>* cancel)
{
    EditImportResult res;
    std::error_code ec;
    const std::string folder = originalTrackDir.lexically_normal().parent_path().filename().string();
    if (folder.empty() || !isSafeName(folder)) { res.error = "cannot tell the folder of the original track"; return res; }

    // importFolder takes folders: stage the one file in a folder of its own
    std::string err;
    const fs::path stage = makeTempDir("trakcrate-edit", fs::path(), &err);
    if (stage.empty()) { res.error = err; return res; }
    struct Cleanup {
        fs::path p;
        ~Cleanup() { std::error_code e; fs::remove_all(p, e); }
    } cleanup{stage};
    const fs::path staged = stage / renderedFile.filename();
    fs::copy_file(renderedFile, staged, fs::copy_options::none, ec);
    if (ec) { res.error = "cannot stage the edit: " + ec.message(); return res; }

    ImportOptions opts;
    opts.mode = ImportMode::Move;
    opts.folder = folder;
    opts.noPlaylist = true;
    opts.dedupe = false;
    opts.analyse = false;
    const ImportReport rep = importFolder(lib, stage, opts, nullptr, cancel);
    if (rep.cancelled) { res.error = "cancelled"; return res; }
    if (!rep.ok && rep.items.empty()) { res.error = rep.error.empty() ? "import failed" : rep.error; return res; }
    const ImportItem* item = nullptr;
    for (const ImportItem& it : rep.items)
        if (it.status == "imported") item = &it;
    if (!item) {
        res.error = "import failed";
        for (const ImportItem& it : rep.items)
            if (it.status == "failed") res.error += ": " + it.detail;
        return res;
    }
    res.trackDir = item->trackDir;
    auto undo = [&](const std::string& why) {
        fs::remove_all(res.trackDir, ec);
        res.trackDir.clear();
        res.error = why;
        return res;
    };

    AnalyseOptions ao;
    ao.beatgrid = false;                                            // the grid of an edit is known exactly
    const AnalyseResult ar = analyseTrack(res.trackDir, ao, cancel);
    if (!ar.ok) return undo("analysis failed: " + ar.error);

    Track t;
    if (!loadTrack(res.trackDir, &t, &err)) return undo(err);
    t.source.set("edit_of", originalTrackDir.filename().string());
    if (!saveGridEdit(&t, outputGrid, &err, "edited")) return undo(err);
    if (!infoFile.empty()) fs::copy_file(infoFile, res.trackDir / "edit.txt", fs::copy_options::overwrite_existing, ec);

    if (!lib.hasPlaylist(folder)) {
        res.note = "no playlist named " + folder;
    } else {
        const std::vector<fs::path> order = lib.playlistTracks(folder);
        const fs::path orig = canon(originalTrackDir);
        int at = -1;
        for (size_t i = 0; i < order.size(); ++i)
            if (canon(order[i]) == orig) { at = int(i); break; }
        if (at < 0) {
            res.note = "the original is not in playlist " + folder;
        } else if (!lib.addToPlaylist(folder, res.trackDir, at, &err)) {
            res.note = "could not add to playlist " + folder + ": " + err;
        } else {
            res.playlist = folder;
            res.playlistIndex = at;
        }
    }
    res.ok = true;
    return res;
}

} // namespace trakcrate
