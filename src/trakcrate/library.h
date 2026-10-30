#ifndef TRAKCRATE_LIBRARY_H
#define TRAKCRATE_LIBRARY_H

#include "fsutil.h"
#include "track.h"

#include <string>
#include <vector>

namespace trakcrate {

// The master database: a plain directory tree (doc/trakcrate/trakcrate-db-format.md)
//
//   <root>/user-settings/
//   <root>/track-master-copy/<folder>/<track>/        audio, meta.txt, source.txt, beatgrid.txt, cuemarks.txt, artwork.jpg ...
//   <root>/pendrives/<name>/symlinks/000_<track>      symlinks to track folders; the numeric prefix is the order
//
// A playlist is a pendrives/<name> entry. Names given by callers are single path components (see isSafeName); anything else is refused.
class Library {
public:
    Library() = default;

    // Creates the directory skeleton (an existing library is left as it is). root itself is created when missing.
    static bool create(const fs::path& root, std::string* err = nullptr);
    // Opens an existing library (needs root/track-master-copy).
    static bool open(const fs::path& root, Library* out, std::string* err = nullptr);

    bool isOpen() const { return !root_.empty(); }
    const fs::path& root() const { return root_; }
    fs::path masterDir() const { return root_ / "track-master-copy"; }
    fs::path pendrivesDir() const { return root_ / "pendrives"; }
    fs::path settingsDir() const { return root_ / "user-settings"; }
    fs::path drivesDir() const { return root_ / "drives"; }

    // Folders and tracks, in natural order. A track is a directory inside a folder.
    std::vector<std::string> listFolders() const;
    std::vector<std::string> listTracks(const std::string& folder) const;
    fs::path trackDir(const std::string& folder, const std::string& track) const { return masterDir() / folder / track; }

    // Creates master/<folder>/<name> (and the folder). When the name is taken, " 2", " 3", ... is appended. Returns the new empty directory.
    fs::path createTrackDir(const std::string& folder, const std::string& name, std::string* err = nullptr) const;

    // Playlists.
    std::vector<std::string> listPlaylists() const;
    bool hasPlaylist(const std::string& name) const;
    bool createPlaylist(const std::string& name, std::string* err = nullptr) const;
    bool deletePlaylist(const std::string& name, std::string* err = nullptr) const;
    // Track folders in play order (the targets of the symlinks; a dangling link is returned as the path it points at).
    std::vector<fs::path> playlistTracks(const std::string& name) const;
    // Inserts a track folder at position (0 = first, -1 or past the end = last) and renumbers the prefixes (width = max(3, digits of the highest index)).
    bool addToPlaylist(const std::string& name, const fs::path& trackDir, int position = -1, std::string* err = nullptr) const;
    // Many tracks in one go (one renumbering pass instead of one per track): the same as calling addToPlaylist for each, but fast for imports.
    bool addTracksToPlaylist(const std::string& name, const std::vector<fs::path>& trackDirs, int position = -1, std::string* err = nullptr) const;
    bool removeFromPlaylist(const std::string& name, int index, std::string* err = nullptr) const;
    // Rewrites the prefixes so they are consecutive and have the right width. Keeps the current order.
    bool renumberPlaylist(const std::string& name, std::string* err = nullptr) const;

    // Moves entry `from` to position `to` (both 0 based) and renumbers.
    bool moveInPlaylist(const std::string& name, int from, int to, std::string* err = nullptr) const;

    // Moves a set of entries as one block, keeping their order among themselves, so that the block starts where position `to` was: `to` counts in the
    // list as it is now (0 = the start, size = the end), the rows are 0 based. Renumbers. False for a bad row or position, or no rows.
    bool moveEntries(const std::string& name, const std::vector<int>& rows, int to, std::string* err = nullptr) const;

    // ---- user defined pendrives ----
    // drives/<name>/ is a small library of its own (drives/<name>/pendrives/<NNN_entry>/symlinks/...): its "playlists" are the entries put on the
    // pendrive, in order. An entry is a snapshot of a master folder (kind "folder", tracks in name order) or of a playlist (kind "playlist", its order),
    // so reordering or removing tracks on a pendrive never changes the library's own folders and playlists. Entries become playlists when exported.
    struct DriveEntry {
        std::string dir;         // "000_House": the playlist name inside the drive's small library (the number is the order)
        std::string name;        // "House": the playlist name on the exported drive
        std::string kind;        // "folder" or "playlist"
        std::string source;      // the folder or playlist it was made from
        int tracks = 0;
    };
    std::vector<std::string> listDrives() const;
    bool hasDrive(const std::string& name) const;
    bool createDrive(const std::string& name, std::string* err = nullptr) const;
    bool renameDrive(const std::string& from, const std::string& to, std::string* err = nullptr) const;
    bool deleteDrive(const std::string& name, std::string* err = nullptr) const;
    std::vector<DriveEntry> driveEntries(const std::string& drive) const;
    // The small library of a drive (use its playlistTracks(entry.dir) to read an entry's tracks).
    Library driveLibrary(const std::string& drive) const;
    // New entry at the end; the name is made unique (" 2", ...). Returns false on a bad drive or name.
    bool addDriveEntry(const std::string& drive, const std::string& name, const std::string& kind, const std::string& source,
                       const std::vector<fs::path>& tracks, std::string* err = nullptr) const;
    bool addFolderToDrive(const std::string& drive, const std::string& folder, std::string* err = nullptr) const;
    bool addPlaylistToDrive(const std::string& drive, const std::string& playlist, std::string* err = nullptr) const;
    bool removeDriveEntry(const std::string& drive, int index, std::string* err = nullptr) const;
    bool moveDriveEntry(const std::string& drive, int from, int to, std::string* err = nullptr) const;
    bool moveDriveTrack(const std::string& drive, int entry, int from, int to, std::string* err = nullptr) const;
    bool removeDriveTrack(const std::string& drive, int entry, int index, std::string* err = nullptr) const;

    // What removing these track folders from the library would break: the playlists and pendrive entries that link to them.
    struct Impact {
        std::vector<std::string> playlists;       // playlist names
        std::vector<std::string> driveEntries;    // "pendrive / entry"
        int links = 0;                            // links that would go
        bool any() const { return links > 0; }
    };
    Impact impactOfRemoving(const std::vector<fs::path>& trackDirs) const;
    // Removes every link to these track folders from all playlists and pendrives (and renumbers them). Call before deleting the folders.
    bool forgetTracks(const std::vector<fs::path>& trackDirs, std::string* err = nullptr) const;

    // ---- disk usage (an estimate) ----
    // What a track folder takes on a pendrive: its audio file plus a fixed allowance for the analysis files and artwork the exporter adds.
    // The audio is what counts; a folder without audio is 0 (nothing is exported for it).
    static uint64_t estimateTrackBytes(const fs::path& trackDir);
    // The sum over the distinct track folders (a track on two playlists is exported once). Adds the database and settings allowance when non empty.
    static uint64_t estimateBytes(const std::vector<fs::path>& trackDirs);

private:
    fs::path symlinksDir(const std::string& name) const { return pendrivesDir() / name / "symlinks"; }
    std::vector<std::string> playlistEntries(const std::string& name) const;       // link names in play order
    bool writeOrder(const std::string& name, const std::vector<std::string>& entries, std::string* err) const;

    fs::path root_;
};

// "23.34Gb": decimal gigabytes (as drives are sold), two decimals, for the total of the folders.
std::string formatFolderSize(uint64_t bytes);
// Whole gigabytes, rounded UP (1.000001Gb needs a 2Gb drive): a pendrive's real capacity is imprecise, one byte over does not fit, and the
// player writes to the drive when it is inserted. 0 for 0 bytes.
uint64_t pendriveGigabytes(uint64_t bytes);
// "2Gb"
std::string formatPendriveSize(uint64_t bytes);

} // namespace trakcrate

#endif
