#ifndef RB_LIBRARY_H
#define RB_LIBRARY_H

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include <cdj/anlz.h>
#include <cdj/pdb.h>
}

// In-memory model of a rekordbox export (PIONEER/rekordbox/export.pdb plus PIONEER/USBANLZ and PIONEER/Artwork),
// as written by rekordbox PC to a pendrive. It is what the dbserver answers menu and metadata requests from.
// Qt-free; parsing is done by libcdj (installed <cdj/pdb.h> and <cdj/anlz.h>).

namespace vdj {
namespace db {

struct RbTrack {
    uint32_t id = 0;
    uint32_t artistId = 0, albumId = 0, genreId = 0, labelId = 0, keyId = 0, remixerId = 0, artworkId = 0;
    uint32_t bitrate = 0, sampleRate = 0, fileSize = 0, trackNumber = 0;
    uint32_t bpmX100 = 0;
    uint16_t durationSecs = 0, year = 0, playCount = 0, discNumber = 0;
    uint8_t  colorId = 0, rating = 0;
    std::string title, filename, filePath, comment, dateAdded, releaseDate, mixName, analyzePath;
};

struct RbNamed {
    uint32_t    id = 0;
    std::string name;
};

struct RbAlbum {
    uint32_t    id = 0;
    uint32_t    artistId = 0;
    std::string name;
};

struct RbPlaylistNode {
    uint32_t    id = 0;
    uint32_t    parentId = 0;       // 0 = root
    uint32_t    sortOrder = 0;
    bool        isFolder = false;
    std::string name;
};

// The analysis files (.DAT/.EXT/.2EX) of one track. Pointers handed out stay valid while this object lives.
class TrackAnalysis {
public:
    explicit TrackAnalysis(anlz_track_t* t) : t_(t) {}
    ~TrackAnalysis() { if (t_) anlz_track_close(t_); }
    TrackAnalysis(const TrackAnalysis&) = delete;
    TrackAnalysis& operator=(const TrackAnalysis&) = delete;

    const anlz_t* file(anlz_file_t which) const { return anlz_track_file(t_, which); }
    bool beatGrid(anlz_beatgrid_t* out) const { return anlz_track_beatgrid(t_, out) == ANLZ_OK; }
    // Cues of the wanted kind; false when the files have no cue tags at all (an empty list is still true).
    bool cues(anlz_cue_kind_t kind, std::vector<anlz_cue_t>* out) const;
    // The waveform entries point into the file buffers: valid while this object lives.
    bool waveform(anlz_wave_kind_t kind, anlz_waveform_t* out) const { return anlz_track_waveform(t_, kind, out) == ANLZ_OK; }

private:
    anlz_track_t* t_;
};

// Sort codes of the "sort" argument of track menus (track_metadata.html).
enum RbSort {
    SortTitle = 0x00, SortTitle2 = 0x01, SortArtist = 0x02, SortAlbum = 0x03, SortBpm = 0x04, SortRating = 0x05,
    SortGenre = 0x06, SortComment = 0x07, SortTime = 0x08, SortRemixer = 0x09, SortLabel = 0x0a,
    SortOriginalArtist = 0x0b, SortKey = 0x0c, SortBitrate = 0x0d, SortPlayCount = 0x10, SortDateAdded = 0x11
};

class RbLibrary {
public:
    RbLibrary() = default;
    RbLibrary(const RbLibrary&) = delete;
    RbLibrary& operator=(const RbLibrary&) = delete;

    // exportRoot holds PIONEER/rekordbox/export.pdb. Returns false with a message in *err.
    bool load(const std::string& exportRoot, std::string* err = nullptr);
    bool loaded() const { return loaded_; }
    const std::string& root() const { return root_; }

    size_t trackCount() const { return tracks_.size(); }
    size_t playlistCount() const { return playlistNodes_.size(); }

    // row lookups, nullptr / "" when the id is unknown
    const RbTrack* track(uint32_t id) const;
    const RbAlbum* album(uint32_t id) const;
    std::string artistName(uint32_t id) const { return nameOf(artists_, id); }
    std::string genreName(uint32_t id) const { return nameOf(genres_, id); }
    std::string labelName(uint32_t id) const { return nameOf(labels_, id); }
    std::string keyName(uint32_t id) const { return nameOf(keys_, id); }
    std::string colorName(uint32_t id) const { return nameOf(colors_, id); }
    std::string albumName(uint32_t id) const;

    // all name tables, sorted by name (case-insensitive)
    std::vector<RbNamed> artists() const { return sortedNames(artists_); }
    std::vector<RbNamed> genres() const { return sortedNames(genres_); }
    std::vector<RbNamed> labels() const { return sortedNames(labels_); }
    std::vector<RbNamed> keys() const { return sortedNames(keys_); }
    std::vector<RbNamed> colors() const { return sortedNames(colors_); }
    std::vector<RbAlbum> albums() const;        // sorted by name

    // track id lists, ordered by an RbSort code (title as tie-break)
    std::vector<uint32_t> allTracks(int sort = SortTitle) const;
    std::vector<uint32_t> tracksForArtist(uint32_t artistId, int sort = SortTitle) const;
    std::vector<uint32_t> tracksForAlbum(uint32_t albumId, int sort = SortTitle) const;
    std::vector<uint32_t> tracksForGenre(uint32_t genreId, int sort = SortTitle) const;
    std::vector<uint32_t> tracksForLabel(uint32_t labelId, int sort = SortTitle) const;
    std::vector<uint32_t> tracksForKey(uint32_t keyId, int sort = SortTitle) const;
    std::vector<uint32_t> tracksForColor(uint32_t colorId, int sort = SortTitle) const;
    std::vector<uint32_t> tracksForRating(uint32_t rating, int sort = SortTitle) const;
    std::vector<uint32_t> tracksForArtistAlbum(uint32_t artistId, uint32_t albumId, int sort = SortTitle) const;

    // derived name lists
    std::vector<RbNamed> artistsForGenre(uint32_t genreId) const;
    std::vector<RbAlbum> albumsForArtist(uint32_t artistId) const;
    std::vector<RbAlbum> albumsForGenreArtist(uint32_t genreId, uint32_t artistId) const;

    // playlist tree: children of a folder (0 = root) in sort order; tracks of a playlist in entry order
    std::vector<RbPlaylistNode> playlistChildren(uint32_t parentId) const;
    std::vector<uint32_t> playlistTracks(uint32_t playlistId) const;

    // case-insensitive substring search
    std::vector<uint32_t> searchTracks(const std::string& needle, int sort = SortTitle) const;   // title, artist, album
    std::vector<RbNamed>  searchArtists(const std::string& needle) const;
    std::vector<RbAlbum>  searchAlbums(const std::string& needle) const;

    // History playlists are not exported by this model yet.
    std::vector<RbNamed> history() const { return {}; }

    // Analysis files of a track (cached, a few at a time). nullptr when the track has no readable analysis.
    std::shared_ptr<TrackAnalysis> analysis(uint32_t trackId) const;

    // Contents of the artwork file of an artwork id (row of the pdb artwork table); empty when missing.
    std::vector<uint8_t> artwork(uint32_t artworkId) const;

private:
    using NameMap = std::map<uint32_t, std::string>;

    static std::string nameOf(const NameMap& m, uint32_t id);
    static std::vector<RbNamed> sortedNames(const NameMap& m);
    std::vector<uint32_t> sortIds(std::vector<uint32_t> ids, int sort) const;
    std::string sortKey(const RbTrack& t, int sort) const;

    bool loaded_ = false;
    std::string root_;
    std::map<uint32_t, RbTrack> tracks_;
    std::map<uint32_t, RbAlbum> albums_;
    NameMap artists_, genres_, labels_, keys_, colors_, artwork_;
    std::vector<RbPlaylistNode> playlistNodes_;
    std::map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> playlistEntries_;   // playlist -> (entry_index, track)

    mutable std::mutex analysisLock_;
    mutable std::list<std::pair<uint32_t, std::shared_ptr<TrackAnalysis>>> analysisCache_;   // front = most recent
};

std::string lowerAscii(const std::string& s);

} // namespace db
} // namespace vdj

#endif // RB_LIBRARY_H
