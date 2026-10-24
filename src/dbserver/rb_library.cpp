#include "rb_library.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace vdj {
namespace db {

namespace {

constexpr size_t kAnalysisCacheSize = 8;
constexpr size_t kMaxArtworkBytes = 16u * 1024 * 1024;

std::string str(const char* s) { return s ? std::string(s) : std::string(); }

struct Loader {
    RbLibrary*                          lib = nullptr;
    std::map<uint32_t, RbTrack>*        tracks = nullptr;
    std::map<uint32_t, RbAlbum>*        albums = nullptr;
    std::vector<RbPlaylistNode>*        nodes = nullptr;
    std::map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>>* entries = nullptr;
};

int trackCb(const pdb_track_t* t, void* ud)
{
    auto* m = static_cast<std::map<uint32_t, RbTrack>*>(ud);
    RbTrack r;
    r.id = t->id;
    r.artistId = t->artist_id; r.albumId = t->album_id; r.genreId = t->genre_id; r.labelId = t->label_id;
    r.keyId = t->key_id; r.remixerId = t->remixer_id; r.artworkId = t->artwork_id;
    r.bitrate = t->bitrate; r.sampleRate = t->sample_rate; r.fileSize = t->file_size; r.trackNumber = t->track_number;
    r.bpmX100 = t->bpm_x100;
    r.durationSecs = t->duration_secs; r.year = t->year; r.playCount = t->play_count; r.discNumber = t->disc_number;
    r.colorId = t->color_id; r.rating = t->rating;
    r.title = str(t->title); r.filename = str(t->filename); r.filePath = str(t->file_path); r.comment = str(t->comment);
    r.dateAdded = str(t->date_added); r.releaseDate = str(t->release_date); r.mixName = str(t->mix_name);
    r.analyzePath = str(t->analyze_path);
    (*m)[r.id] = std::move(r);
    return 0;
}

int nameCb(const pdb_named_t* r, void* ud)
{
    (*static_cast<std::map<uint32_t, std::string>*>(ud))[r->id] = str(r->name);
    return 0;
}

int albumCb(const pdb_album_t* r, void* ud)
{
    RbAlbum a;
    a.id = r->id; a.artistId = r->artist_id; a.name = str(r->name);
    (*static_cast<std::map<uint32_t, RbAlbum>*>(ud))[a.id] = std::move(a);
    return 0;
}

int playlistCb(const pdb_playlist_t* r, void* ud)
{
    RbPlaylistNode n;
    n.id = r->id; n.parentId = r->parent_id; n.sortOrder = r->sort_order; n.isFolder = r->is_folder != 0; n.name = str(r->name);
    static_cast<std::vector<RbPlaylistNode>*>(ud)->push_back(std::move(n));
    return 0;
}

int entryCb(const pdb_playlist_entry_t* r, void* ud)
{
    (*static_cast<std::map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>>*>(ud))[r->playlist_id].emplace_back(r->entry_index, r->track_id);
    return 0;
}

bool safeRelativePath(const std::string& p)
{
    if (p.empty() || p[0] != '/' || p.find('\\') != std::string::npos) return false;
    size_t i = 0;
    while (i < p.size()) {            // reject ".." components (the database is untrusted input)
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        if (p.compare(i, j - i, "..") == 0) return false;
        i = j + 1;
    }
    return true;
}

bool readFile(const std::string& path, size_t maxBytes, std::vector<uint8_t>* out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out->clear();
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        if (out->size() + n > maxBytes) { std::fclose(f); out->clear(); return false; }
        out->insert(out->end(), buf, buf + n);
    }
    std::fclose(f);
    return true;
}

} // namespace

std::string lowerAscii(const std::string& s)
{
    std::string o = s;
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return o;
}

bool TrackAnalysis::cues(anlz_cue_kind_t kind, std::vector<anlz_cue_t>* out) const
{
    anlz_cue_list_t list{};
    if (anlz_track_cues(t_, kind, &list) != ANLZ_OK) return false;
    out->assign(list.cues, list.cues + list.count);
    anlz_cue_list_free(&list);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------

bool RbLibrary::load(const std::string& exportRoot, std::string* err)
{
    loaded_ = false;
    tracks_.clear(); albums_.clear(); artists_.clear(); genres_.clear(); labels_.clear(); keys_.clear(); colors_.clear();
    artwork_.clear(); playlistNodes_.clear(); playlistEntries_.clear();
    {
        std::lock_guard<std::mutex> g(analysisLock_);
        analysisCache_.clear();
    }
    root_ = exportRoot;
    while (root_.size() > 1 && root_.back() == '/') root_.pop_back();

    const std::string pdbPath = root_ + "/PIONEER/rekordbox/export.pdb";
    uint32_t len = 0;
    uint8_t* buf = pdb_read_file(pdbPath.c_str(), &len);
    if (!buf) {
        if (err) *err = "cannot read " + pdbPath;
        return false;
    }
    int rc = pdb_parse(buf, len, trackCb, &tracks_);
    if (rc == 0) {
        pdb_parse_artists(buf, len, nameCb, &artists_);
        pdb_parse_genres(buf, len, nameCb, &genres_);
        pdb_parse_keys(buf, len, nameCb, &keys_);
        pdb_parse_labels(buf, len, nameCb, &labels_);
        pdb_parse_colors(buf, len, nameCb, &colors_);
        pdb_parse_artwork(buf, len, nameCb, &artwork_);
        pdb_parse_albums(buf, len, albumCb, &albums_);
        pdb_parse_playlists(buf, len, playlistCb, &playlistNodes_);
        pdb_parse_playlist_entries(buf, len, entryCb, &playlistEntries_);
    }
    std::free(buf);
    if (rc != 0) {
        if (err) *err = "cannot parse " + pdbPath;
        return false;
    }
    for (auto& kv : playlistEntries_) std::sort(kv.second.begin(), kv.second.end());
    loaded_ = true;
    return true;
}

const RbTrack* RbLibrary::track(uint32_t id) const
{
    auto it = tracks_.find(id);
    return it == tracks_.end() ? nullptr : &it->second;
}

const RbAlbum* RbLibrary::album(uint32_t id) const
{
    auto it = albums_.find(id);
    return it == albums_.end() ? nullptr : &it->second;
}

std::string RbLibrary::nameOf(const NameMap& m, uint32_t id)
{
    auto it = m.find(id);
    return it == m.end() ? std::string() : it->second;
}

std::string RbLibrary::albumName(uint32_t id) const
{
    const RbAlbum* a = album(id);
    return a ? a->name : std::string();
}

std::vector<RbNamed> RbLibrary::sortedNames(const NameMap& m)
{
    std::vector<RbNamed> v;
    for (const auto& kv : m) v.push_back({kv.first, kv.second});
    std::stable_sort(v.begin(), v.end(), [](const RbNamed& a, const RbNamed& b) {
        const std::string la = lowerAscii(a.name), lb = lowerAscii(b.name);
        return la != lb ? la < lb : a.id < b.id;
    });
    return v;
}

std::vector<RbAlbum> RbLibrary::albums() const
{
    std::vector<RbAlbum> v;
    for (const auto& kv : albums_) v.push_back(kv.second);
    std::stable_sort(v.begin(), v.end(), [](const RbAlbum& a, const RbAlbum& b) {
        const std::string la = lowerAscii(a.name), lb = lowerAscii(b.name);
        return la != lb ? la < lb : a.id < b.id;
    });
    return v;
}

// ---- sorting -------------------------------------------------------------------------------------------------------

namespace {
// numbers sort numerically: pad to a fixed width so one string compare does it
std::string numKey(uint64_t n)
{
    char b[24];
    std::snprintf(b, sizeof b, "%020llu", static_cast<unsigned long long>(n));
    return b;
}
} // namespace

std::string RbLibrary::sortKey(const RbTrack& t, int sort) const
{
    switch (sort) {
    case SortArtist:         return lowerAscii(artistName(t.artistId));
    case SortAlbum:          return lowerAscii(albumName(t.albumId));
    case SortBpm:            return numKey(t.bpmX100);
    case SortRating:         return numKey(t.rating);
    case SortGenre:          return lowerAscii(genreName(t.genreId));
    case SortComment:        return lowerAscii(t.comment);
    case SortTime:           return numKey(t.durationSecs);
    case SortRemixer:        return lowerAscii(artistName(t.remixerId));
    case SortLabel:          return lowerAscii(labelName(t.labelId));
    case SortOriginalArtist: return lowerAscii(artistName(t.artistId));
    case SortKey:            return lowerAscii(keyName(t.keyId));
    case SortBitrate:        return numKey(t.bitrate);
    case SortPlayCount:      return numKey(t.playCount);
    case SortDateAdded:      return t.dateAdded;
    default:                 return lowerAscii(t.title);
    }
}

std::vector<uint32_t> RbLibrary::sortIds(std::vector<uint32_t> ids, int sort) const
{
    struct Item { std::string primary, title; uint32_t id; };
    std::vector<Item> items;
    items.reserve(ids.size());
    for (uint32_t id : ids) {
        const RbTrack* t = track(id);
        if (t) items.push_back({sortKey(*t, sort), lowerAscii(t->title), id});
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.primary != b.primary) return a.primary < b.primary;
        if (a.title != b.title) return a.title < b.title;
        return a.id < b.id;
    });
    ids.clear();
    for (const Item& i : items) ids.push_back(i.id);
    return ids;
}

namespace {
template <typename Pred>
std::vector<uint32_t> pick(const std::map<uint32_t, RbTrack>& tracks, Pred p)
{
    std::vector<uint32_t> v;
    for (const auto& kv : tracks)
        if (p(kv.second)) v.push_back(kv.first);
    return v;
}
} // namespace

std::vector<uint32_t> RbLibrary::allTracks(int sort) const
{
    return sortIds(pick(tracks_, [](const RbTrack&) { return true; }), sort);
}

std::vector<uint32_t> RbLibrary::tracksForArtist(uint32_t id, int sort) const
{
    return sortIds(pick(tracks_, [&](const RbTrack& t) { return t.artistId == id; }), sort);
}

std::vector<uint32_t> RbLibrary::tracksForAlbum(uint32_t id, int sort) const
{
    return sortIds(pick(tracks_, [&](const RbTrack& t) { return t.albumId == id; }), sort);
}

std::vector<uint32_t> RbLibrary::tracksForGenre(uint32_t id, int sort) const
{
    return sortIds(pick(tracks_, [&](const RbTrack& t) { return t.genreId == id; }), sort);
}

std::vector<uint32_t> RbLibrary::tracksForLabel(uint32_t id, int sort) const
{
    return sortIds(pick(tracks_, [&](const RbTrack& t) { return t.labelId == id; }), sort);
}

std::vector<uint32_t> RbLibrary::tracksForKey(uint32_t id, int sort) const
{
    return sortIds(pick(tracks_, [&](const RbTrack& t) { return t.keyId == id; }), sort);
}

std::vector<uint32_t> RbLibrary::tracksForColor(uint32_t id, int sort) const
{
    return sortIds(pick(tracks_, [&](const RbTrack& t) { return t.colorId == id; }), sort);
}

std::vector<uint32_t> RbLibrary::tracksForRating(uint32_t rating, int sort) const
{
    return sortIds(pick(tracks_, [&](const RbTrack& t) { return t.rating == rating; }), sort);
}

std::vector<uint32_t> RbLibrary::tracksForArtistAlbum(uint32_t artistId, uint32_t albumId, int sort) const
{
    return sortIds(pick(tracks_, [&](const RbTrack& t) { return t.artistId == artistId && t.albumId == albumId; }), sort);
}

// ---- derived lists -------------------------------------------------------------------------------------------------

std::vector<RbNamed> RbLibrary::artistsForGenre(uint32_t genreId) const
{
    std::map<uint32_t, bool> seen;
    for (const auto& kv : tracks_)
        if (kv.second.genreId == genreId) seen[kv.second.artistId] = true;
    std::vector<RbNamed> v;
    for (const auto& kv : seen) v.push_back({kv.first, artistName(kv.first)});
    std::stable_sort(v.begin(), v.end(), [](const RbNamed& a, const RbNamed& b) {
        const std::string la = lowerAscii(a.name), lb = lowerAscii(b.name);
        return la != lb ? la < lb : a.id < b.id;
    });
    return v;
}

std::vector<RbAlbum> RbLibrary::albumsForArtist(uint32_t artistId) const
{
    std::map<uint32_t, bool> seen;
    for (const auto& kv : tracks_)
        if (kv.second.artistId == artistId) seen[kv.second.albumId] = true;
    std::vector<RbAlbum> v;
    for (const auto& kv : seen) {
        const RbAlbum* a = album(kv.first);
        RbAlbum row;
        if (a) row = *a;
        else row.id = kv.first;
        v.push_back(row);
    }
    std::stable_sort(v.begin(), v.end(), [](const RbAlbum& a, const RbAlbum& b) {
        const std::string la = lowerAscii(a.name), lb = lowerAscii(b.name);
        return la != lb ? la < lb : a.id < b.id;
    });
    return v;
}

std::vector<RbAlbum> RbLibrary::albumsForGenreArtist(uint32_t genreId, uint32_t artistId) const
{
    std::map<uint32_t, bool> seen;
    for (const auto& kv : tracks_)
        if (kv.second.genreId == genreId && kv.second.artistId == artistId) seen[kv.second.albumId] = true;
    std::vector<RbAlbum> v;
    for (const auto& kv : seen) {
        const RbAlbum* a = album(kv.first);
        RbAlbum row;
        if (a) row = *a;
        else row.id = kv.first;
        v.push_back(row);
    }
    std::stable_sort(v.begin(), v.end(), [](const RbAlbum& a, const RbAlbum& b) {
        const std::string la = lowerAscii(a.name), lb = lowerAscii(b.name);
        return la != lb ? la < lb : a.id < b.id;
    });
    return v;
}

std::vector<RbPlaylistNode> RbLibrary::playlistChildren(uint32_t parentId) const
{
    std::vector<RbPlaylistNode> v;
    for (const RbPlaylistNode& n : playlistNodes_)
        if (n.parentId == parentId) v.push_back(n);
    std::stable_sort(v.begin(), v.end(), [](const RbPlaylistNode& a, const RbPlaylistNode& b) {
        return a.sortOrder != b.sortOrder ? a.sortOrder < b.sortOrder : a.id < b.id;
    });
    return v;
}

std::vector<uint32_t> RbLibrary::playlistTracks(uint32_t playlistId) const
{
    std::vector<uint32_t> v;
    auto it = playlistEntries_.find(playlistId);
    if (it == playlistEntries_.end()) return v;
    for (const auto& e : it->second)
        if (track(e.second)) v.push_back(e.second);
    return v;
}

// ---- search --------------------------------------------------------------------------------------------------------

std::vector<uint32_t> RbLibrary::searchTracks(const std::string& needle, int sort) const
{
    const std::string n = lowerAscii(needle);
    return sortIds(pick(tracks_, [&](const RbTrack& t) {
        return lowerAscii(t.title).find(n) != std::string::npos || lowerAscii(artistName(t.artistId)).find(n) != std::string::npos ||
               lowerAscii(albumName(t.albumId)).find(n) != std::string::npos;
    }), sort);
}

std::vector<RbNamed> RbLibrary::searchArtists(const std::string& needle) const
{
    const std::string n = lowerAscii(needle);
    std::vector<RbNamed> v;
    for (const RbNamed& a : artists())
        if (lowerAscii(a.name).find(n) != std::string::npos) v.push_back(a);
    return v;
}

std::vector<RbAlbum> RbLibrary::searchAlbums(const std::string& needle) const
{
    const std::string n = lowerAscii(needle);
    std::vector<RbAlbum> v;
    for (const RbAlbum& a : albums())
        if (lowerAscii(a.name).find(n) != std::string::npos) v.push_back(a);
    return v;
}

// ---- analysis and artwork ------------------------------------------------------------------------------------------

std::shared_ptr<TrackAnalysis> RbLibrary::analysis(uint32_t trackId) const
{
    {
        std::lock_guard<std::mutex> g(analysisLock_);
        for (auto it = analysisCache_.begin(); it != analysisCache_.end(); ++it) {
            if (it->first == trackId) {
                analysisCache_.splice(analysisCache_.begin(), analysisCache_, it);
                return it->second;
            }
        }
    }
    const RbTrack* t = track(trackId);
    if (!t || t->analyzePath.empty()) return nullptr;

    anlz_track_t* at = nullptr;
    if (anlz_track_open(root_.c_str(), t->analyzePath.c_str(), &at) != ANLZ_OK || !at) return nullptr;
    auto ta = std::make_shared<TrackAnalysis>(at);

    std::lock_guard<std::mutex> g(analysisLock_);
    analysisCache_.emplace_front(trackId, ta);
    if (analysisCache_.size() > kAnalysisCacheSize) analysisCache_.pop_back();
    return ta;
}

std::vector<uint8_t> RbLibrary::artwork(uint32_t artworkId) const
{
    std::vector<uint8_t> data;
    auto it = artwork_.find(artworkId);
    if (it == artwork_.end() || !safeRelativePath(it->second)) return data;
    readFile(root_ + it->second, kMaxArtworkBytes, &data);
    return data;
}

} // namespace db
} // namespace vdj
