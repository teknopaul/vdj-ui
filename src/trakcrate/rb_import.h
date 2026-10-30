#ifndef TRAKCRATE_RB_IMPORT_H
#define TRAKCRATE_RB_IMPORT_H

#include "importer.h"

#include <set>

// Importing a real rekordbox pendrive (doc/trakcrate/import-from-rekordbox.md). Read only on the drive. Real data wins over anything we would
// generate: metadata, beat grid, cue points, waveforms and artwork are taken from the drive and marked "rekordbox" in source.txt, so analysis
// leaves them alone. The original ANLZ files are kept byte for byte in <track>/anlz/.
namespace trakcrate {

struct RbImportOptions {
    ImportMode mode = ImportMode::Copy;      // Move is not allowed on a drive: treated as Copy
    bool dedupe = true;                      // audio already in the master (same SHA-256) is not stored again
    bool playlists = true;                   // recreate the drive's playlists (folders are joined: "Folder - Playlist")
    std::set<uint32_t> playlistOff;          // ids (listRekordboxPlaylists) of playlists NOT recreated as trakcrate playlists
    // Where the audio goes. By default every playlist of the drive becomes a master folder named after it, which keeps the tracks in the groups the DJ
    // knows. A track in several playlists is stored once, in the first (natural name order) playlist that is switched on as a folder: playlists that
    // only repeat other playlists should be listed in folderOff. Tracks in no folder playlist, and every track of a drive without playlists, go to
    // the folder of their location on the drive (Contents/A/B/x.mp3 -> "A - B") when looseByPath is set, else into the one folder named `folder`.
    bool playlistFolders = true;
    std::set<uint32_t> folderOff;
    bool looseByPath = false;
    bool beatgrid = true, cues = true, waveform = true, artwork = true;
    bool keepAnlz = true;                    // <track>/anlz/ANLZ0000.DAT|EXT|2EX copied verbatim
    bool settings = false;                   // DEVSETTING.DAT and MYSETTING.DAT into user-settings/ (checked first), used by later exports
    bool deviceFiles = false;                // RBFLTR.DAT, USBMNG.DAT, exportExt.pdb into user-settings/ (device specific, off by default)
    bool profile = false;                    // djprofile.nxs into user-settings/ (holds the user's name, off by default)
    std::string folder;                      // master folder for tracks directly under Contents/ (default "rekordbox")
    bool dryRun = false;
};

struct RbImportReport {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    int tracks = 0;                          // in the drive's database
    int imported = 0, duplicates = 0, missingAudio = 0, failed = 0;
    int withGrid = 0, withCues = 0, withWaveform = 0, withArtwork = 0, withAnlz = 0;
    int playlists = 0;
    int folders = 0;                         // master folders created
    int settingsFiles = 0;
    std::vector<std::string> warnings;       // per item problems, a variable tempo grid, a playlist that already existed, ...
};

struct RbPlaylistInfo {
    uint32_t id = 0;                         // the drive's playlist id
    std::string name;                        // the path: "Folder - Playlist"
    int tracks = 0;
};
// True when the library already has this playlist (hasPlaylist) / a master folder of this name. The dialog leaves those unticked.
// Ticking an existing playlist adds the tracks it lacks and leaves the others alone.
bool rekordboxPlaylistKnown(const Library& lib, const RbPlaylistInfo& p);
bool rekordboxFolderKnown(const Library& lib, const RbPlaylistInfo& p);
// The playlists (not the folders) of a drive's database, in natural name order. Empty for a drive without playlists; err says if the database is unreadable.
std::vector<RbPlaylistInfo> listRekordboxPlaylists(const fs::path& root, std::string* err = nullptr);

// True when root looks like a rekordbox drive (PIONEER/rekordbox/export.pdb exists).
bool isRekordboxDrive(const fs::path& root);

RbImportReport importRekordboxDrive(const Library& lib, const fs::path& root, const RbImportOptions& opts, const ImportProgress& progress = nullptr,
                                    const std::atomic<bool>* cancel = nullptr);

} // namespace trakcrate

#endif
