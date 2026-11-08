#include "deck_edits.h"

#include "scripts.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>

namespace trakcrate {

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string hexColour(const anlz_cue_t& c)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c.red, c.green, c.blue);
    return buf;
}

std::string isoNow()
{
    char buf[32];
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

constexpr double kTolMs = 1.0;

} // namespace

bool gridFromAnlz(anlz_track_t* a, BeatGrid* grid, bool* variable)
{
    anlz_beatgrid_t bg;
    if (anlz_track_beatgrid(a, &bg) != ANLZ_OK || bg.count == 0) return false;
    anlz_beat_t first, b;
    anlz_beat_at(&bg, 0, &first);
    grid->bpm = first.bpm_x100 / 100.0;
    grid->firstBeatUs = (long long)first.time_signed_ms * 1000 + first.sub_ms_us;
    bool var = false;
    for (uint32_t i = 1; i < bg.count && !var; ++i)
        if (anlz_beat_at(&bg, i, &b) && b.bpm_x100 != first.bpm_x100) var = true;
    if (variable) *variable = var;
    return true;
}

std::vector<Cuemark> cuesFromAnlz(anlz_track_t* a, const BeatGrid& g)
{
    std::vector<Cuemark> out;
    anlz_cue_list_t list{};
    if (anlz_track_cues(a, ANLZ_CUES_ALL, &list) == ANLZ_OK && list.count > 0) {
        const double periodMs = g.empty() ? 0 : 60000.0 / g.bpm;
        for (uint32_t i = 0; i < list.count; ++i) {
            const anlz_cue_t& c = list.cues[i];
            Cuemark m;
            m.ms = c.time_ms;
            m.beat = periodMs > 0 ? (c.time_ms - g.firstBeatUs / 1000.0) / periodMs : std::nan("");
            m.label = c.comment;
            if (c.hot_cue >= 1 && c.hot_cue <= 3) m.hot = char('A' + c.hot_cue - 1);
            else if (c.hot_cue > 3 && m.label.empty()) m.label = std::string("hot ") + char('A' + c.hot_cue - 1);
            if (c.has_rgb) m.colour = hexColour(c);
            if (c.is_loop) m.extra.push_back("loop_end_ms=" + std::to_string(c.loop_time_ms));
            out.push_back(m);
        }
    }
    anlz_cue_list_free(&list);
    return out;
}

DeckAnalysis readDeckAnalysis(const fs::path& root, const std::string& analyzePath)
{
    DeckAnalysis d;
    anlz_track_t* a = nullptr;
    if (analyzePath.empty() || anlz_track_open(root.string().c_str(), analyzePath.c_str(), &a) != ANLZ_OK || !a) {
        d.error = "analysis files not readable: " + analyzePath;
        return d;
    }
    d.ok = true;
    d.hasGrid = gridFromAnlz(a, &d.grid, &d.variableTempo);
    d.cues = cuesFromAnlz(a, d.hasGrid ? d.grid : BeatGrid());
    anlz_track_close(a);
    return d;
}

std::vector<LibraryEntry> indexLibrary(const Library& lib)
{
    std::vector<LibraryEntry> out;
    for (const std::string& f : lib.listFolders()) {
        for (const std::string& t : lib.listTracks(f)) {
            LibraryEntry e;
            e.dir = lib.trackDir(f, t);
            e.stem = t;
            std::string text;
            if (readFile(e.dir / "meta.txt", &text)) {
                const KvDoc m = KvDoc::parse(text);
                e.title = m.get("title");
                e.artist = m.get("artist");
            }
            if (readFile(e.dir / "source.txt", &text)) {
                const std::string from = KvDoc::parse(text).get("imported_from");      // "KINGSTON:/Contents/A/B/x.mp3"
                const size_t colon = from.find(':');
                if (colon != std::string::npos) e.importedPath = from.substr(colon + 1);
            }
            out.push_back(std::move(e));
        }
    }
    return out;
}

Match matchLibraryTrack(const std::vector<LibraryEntry>& index, const DeckTrackKey& key)
{
    Match m;
    const std::string title = lower(key.title), artist = lower(key.artist), stem = lower(key.fileStem);
    auto pick = [&](const char* how, auto&& pred) {
        std::vector<fs::path> hits;
        for (const LibraryEntry& e : index)
            if (pred(e)) hits.push_back(e.dir);
        if (hits.empty()) return false;
        m.how = how;
        if (hits.size() == 1) m.dir = hits[0];
        else m.candidates = hits;
        return true;
    };
    auto sameName = [&](const LibraryEntry& e) { return !title.empty() && lower(e.title) == title; };
    auto sameArtist = [&](const LibraryEntry& e) { return lower(e.artist) == artist; };
    auto sameStem = [&](const LibraryEntry& e) { return !stem.empty() && lower(e.stem) == stem; };
    const std::string path = lower(key.filePath);
    if (!path.empty() && pick("path on the drive it was imported from", [&](const LibraryEntry& e) { return lower(e.importedPath) == path; })) return m;
    if (pick("title, artist and file name", [&](const LibraryEntry& e) { return sameName(e) && sameArtist(e) && sameStem(e); })) return m;
    if (pick("title and artist", [&](const LibraryEntry& e) { return sameName(e) && sameArtist(e); })) return m;
    if (pick("file name", sameStem)) return m;
    if (artist.empty() && pick("title", sameName)) return m;      // the deck row has no artist
    return m;
}

GridDiff diffGrid(const Track& t, const DeckAnalysis& deck)
{
    GridDiff d;
    if (!deck.hasGrid) return d;
    const BeatGrid old = t.hasGrid ? beatGridFrom(t.grid) : BeatGrid();
    d.hadGrid = !old.empty();
    d.oldBpm = old.bpm;
    d.newBpm = deck.grid.bpm;
    d.oldFirstMs = old.firstBeatUs / 1000.0;
    d.newFirstMs = deck.grid.firstBeatUs / 1000.0;
    d.shiftMs = (deck.grid.firstBeatUs - old.firstBeatUs) / 1000.0;
    d.changed = old.empty() || std::fabs(d.newBpm - d.oldBpm) >= 0.005 || std::fabs(d.shiftMs) >= kTolMs;
    return d;
}

CueDiff diffCues(const Track& t, const DeckAnalysis& deck)
{
    CueDiff d;
    if (!deck.ok) return d;
    const std::vector<Cuemark>& have = t.cuemarks.marks;
    auto near = [](const Cuemark& a, const Cuemark& b) { return std::fabs(a.ms - b.ms) < kTolMs; };
    std::vector<bool> used(have.size(), false);
    for (const Cuemark& c : deck.cues) {
        size_t i = 0;
        for (; i < have.size(); ++i)
            if (!used[i] && near(have[i], c) && have[i].hot == c.hot) break;
        if (i < have.size()) { used[i] = true; continue; }
        // a hot cue with the same letter elsewhere has moved
        size_t j = 0;
        for (; c.hot && j < have.size(); ++j)
            if (!used[j] && have[j].hot == c.hot) break;
        if (c.hot && j < have.size()) { used[j] = true; ++d.moved; }
        else ++d.added;
    }
    for (size_t i = 0; i < have.size(); ++i)
        if (!used[i]) ++d.removed;
    d.changed = d.added || d.moved || d.removed;
    return d;
}

CopyBackResult applyDeckEdits(const fs::path& trackDir, const DeckAnalysis& deck, const CopyBackOptions& opts)
{
    CopyBackResult r;
    if (!deck.ok) { r.error = deck.error.empty() ? "no analysis" : deck.error; return r; }
    Track t;
    if (!loadTrack(trackDir, &t, &r.error)) return r;
    const bool doGrid = opts.grid && deck.hasGrid;
    if (opts.grid) r.grid = diffGrid(t, deck);        // also filled when nothing changes: the preview shows both sides
    const bool doCues = opts.cues && !(opts.skipEmptyCues && deck.cues.empty());
    if (doCues) r.cues = diffCues(t, deck);
    r.changed = (doGrid && r.grid.changed) || (doCues && r.cues.changed);
    r.ok = true;
    if (!r.changed || opts.dryRun) return r;

    r.snapshot = snapshotTrack(trackDir, "deck-copy-back", &r.error);
    if (r.snapshot.empty()) { r.ok = false; return r; }

    if (doGrid && r.grid.changed) {
        if (!t.hasGrid) t.grid.addComment("trakcrate beatgrid (from a deck)");
        t.hasGrid = true;
        setBeatGrid(t.grid, deck.grid);
        t.grid.set("analyser", "deck");
        setOrigin(t.source, "beatgrid", "edited");
        t.source.set("beatgrid_from", opts.from + " " + isoNow());
        // ms wins over beat in cuemarks.txt: the beat column follows the new grid
        const double periodMs = 60000.0 / deck.grid.bpm;
        for (Cuemark& m : t.cuemarks.marks) m.beat = (m.ms - deck.grid.firstBeatUs / 1000.0) / periodMs;
    }
    if (doCues && r.cues.changed) {
        const BeatGrid g = t.hasGrid ? beatGridFrom(t.grid) : BeatGrid();
        const double periodMs = g.empty() ? 0 : 60000.0 / g.bpm;
        t.cuemarks.marks = deck.cues;
        for (Cuemark& m : t.cuemarks.marks)
            m.beat = periodMs > 0 ? (m.ms - g.firstBeatUs / 1000.0) / periodMs : std::nan("");
        if (t.cuemarks.comments.empty()) t.cuemarks.comments.push_back("# copied back from a deck");
        t.hasCuemarks = true;
        setOrigin(t.source, "cues", "edited");
        t.source.set("cues_from", opts.from + " " + isoNow());
    }
    const bool good = (!(doGrid && r.grid.changed) || saveBeatGrid(t, &r.error)) &&
                      (!(doCues && r.cues.changed) || saveCuemarks(t, &r.error)) && saveSource(t, &r.error);
    if (!good) r.ok = false;
    return r;
}

} // namespace trakcrate
