#ifndef TRAKCRATE_EXPORTER_H
#define TRAKCRATE_EXPORTER_H

#include "cuemark_render.h"
#include "library.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

// Exports playlists of the library as a rekordbox format drive (doc/trakcrate/rekordbox-db-output.md). The metadata (database, analysis
// files, artwork, settings) is built in a staging directory on tmpfs and checked there; then everything is copied to the destination in one
// sequential pass, each file written once, audio first and export.pdb last, so an interrupted export leaves no database; then the destination
// can be read back and compared by SHA-256 (optional). Data is not synced file by file: one sync of the drive comes before export.pdb and one after. The destination must be an empty directory. Nothing is ever written anywhere else.
namespace trakcrate {

// Turns the bytes of an image file into a square JPEG of `size` pixels (the exporter asks for 240 and 80). Qt-free code cannot do that itself.
using ArtworkEncoder = std::function<bool(const std::string& source, int size, std::string* jpeg, std::string* err)>;

// A list of tracks that is not a playlist of the library (an entry of a user defined pendrive): its name on the drive and its tracks in order.
struct ExportList {
    std::string name;
    std::vector<fs::path> tracks;
};

struct ExportOptions {
    std::vector<std::string> playlists;      // the playlists (library "playlists") to put on the drive, in this order
    std::vector<ExportList> lists;           // further lists, after the playlists (see Library::driveEntries)
    fs::path dest;                           // an empty directory, or one that does not exist yet (or, with `update`, a drive written by an earlier export)
    // Update a drive that already holds an export: audio and small files are only written when they differ (each through a temporary name and a
    // rename, so a file is old or new, never half), nothing is deleted, export.pdb is built in memory and written last after the old one has been
    // backed up as export.pdb.bak (and .bak.2, .bak.3: the three newest are kept). Cancel stops before the backup; once export.pdb is being
    // replaced the export always finishes, a database must be whole.
    bool update = false;
    std::string templatePdb;                 // bytes of the template export.pdb (required)
    fs::path stagingBase;                    // where the staging directory goes; empty = /dev/shm, else $XDG_RUNTIME_DIR, else the system temp
    // Checksums are off by default: hashing costs CPU, reading the drive back doubles the time on a slow stick. Turning on either makes every file hashed.
    bool verify = false;                     // read the destination back and compare checksums
    bool allowUnanalysed = false;            // tracks without analysis get a flat waveform and no beat grid instead of refusing the export
    uint64_t minFreeBytes = 1u << 20;        // space that must remain free on the destination

    // compatibility testing: which optional pieces to include (all on by default)
    bool includeSettings = true;             // PIONEER/DEVSETTING.DAT, MYSETTING.DAT, djprofile.nxs and rekordbox/RBFLTR.DAT, exportExt.pdb: players need them all
    // Which files those are: a settings set of the library (settings_sets.h; empty = the library's default set, and a library without sets gives the
    // compiled in files). Any of the byte fields below that is not empty overrides the set's file. Production callers leave includeSettings true.
    std::string settingsSet;
    bool settingsBuiltIn = false;            // ignore every set: the compiled in files
    // What to do with those five files when the drive already has them (update): AddMissing keeps the drive's own and only adds absent ones,
    // Replace overwrites them with these. A fresh destination gets all five either way. USBMNG.DAT is always only added when missing.
    enum class SettingsPolicy { AddMissing, Replace };
    SettingsPolicy settingsPolicy = SettingsPolicy::AddMissing;
    bool raiseLimits = false;                // apply the track/artist/album limits of newer decks instead of the lower ones of older decks
    bool includeArtwork = true;
    bool includePvbr = true, includePwv4 = true, includePwv5 = true;
    CuemarkRenderOptions cuemarks;           // the spikes drawn into the exported waveforms (never into the master)
    bool writeManifest = false;              // SHA256SUMS and trakcrate-manifest.txt in the root of the drive
    std::string devSetting, mySetting;       // replacement settings files (bytes); empty = the built in ones
    std::string djprofile, rbfltr, exportExt;   // replacements for PIONEER/djprofile.nxs, rekordbox/RBFLTR.DAT, rekordbox/exportExt.pdb; empty = the built in ones
    // Files copied from a real drive (a folder holding PIONEER/): other tracks' data, only to find out which files a player needs.
    fs::path optionalFilesFrom;
    bool copyExportExt = false, copyRbfltr = false, copyUsbmng = false, copyDjprofile = false;

    ArtworkEncoder encodeArtwork;            // null: no artwork on the drive
};

// A file on the drive that the new export.pdb no longer refers to: audio under Contents/, artwork, analysis files.
struct Orphan {
    std::string rel;                         // relative to the destination
    uint64_t size = 0;
};

struct ExportResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::vector<std::string> warnings;
    int tracks = 0, playlists = 0, files = 0;
    uint64_t bytesWritten = 0;
    std::vector<std::string> writeOrder;     // paths relative to the destination, in the order they were written
    std::string stagingUsed;                 // the directory used (removed again)
    bool stagedOnTmpfs = false;
    bool verified = false;
    int skipped = 0;                         // update: files already identical on the drive
    std::vector<Orphan> orphans;             // update: files left from earlier exports that nothing refers to any more (nothing is deleted, see deleteOrphans)
    std::vector<std::string> settingsWritten;   // the settings files (relative to the destination) this export wrote
    std::vector<std::string> settingsKept;      // update with AddMissing: the settings files the drive already had and that were left alone
    std::string backup;                      // update: the backup made of the old export.pdb (relative to the destination), empty if none
};

// How many tracks, artists and albums a deck can hold in a pendrive's database before it runs out of memory (doc/trakcrate/rekordbox-db-output.md).
// Only pendrive exports are limited, never the trakcrate library. The lower limits are the default; the higher ones are for newer decks.
struct DeckLimits {
    int tracks = 0, artists = 0, albums = 0;
};
DeckLimits deckLimits(bool raised);
// Why a pendrive with this many tracks, artists and albums is refused (empty = fine). overDefault / overRaised say whether it is above the lower /
// the higher limits. The text names the numbers and tells the user to fix the lists in trakcrate, or to raise the limits when only the lower ones are hit.
std::vector<std::string> checkDeckLimits(int tracks, int artists, int albums, bool raised, bool* overDefault = nullptr, bool* overRaised = nullptr);

struct ExportSummary {
    int tracks = 0;
    int artists = 0, albums = 0;             // distinct, as the database will hold them
    bool overDefaultLimits = false;          // above the limits of older decks (refused unless raiseLimits)
    bool overRaisedLimits = false;           // above the limits of newer decks too (always refused)
    uint64_t audioBytes = 0, metadataBytes = 0;   // metadata is an estimate
    std::vector<std::string> problems;       // would refuse the export: missing audio, tracks not analysed ...
};

// done files, total files, the path being written.
using ExportProgress = std::function<void(int done, int total, const std::string& current)>;

ExportResult exportDrive(const Library& lib, const ExportOptions& opts, const ExportProgress& progress = nullptr, const std::atomic<bool>* cancel = nullptr);

// Removes the given orphans from dest (and folders that become empty). Only plain files below Contents/, PIONEER/Artwork/ and PIONEER/USBANLZ/ are
// ever touched, whatever the list says. Returns the bytes freed; `failed` gets the paths that could not be removed.
uint64_t deleteOrphans(const fs::path& dest, const std::vector<Orphan>& orphans, std::vector<std::string>* failed = nullptr);

// What an export of these playlists would involve, without writing anything.
ExportSummary summariseExport(const Library& lib, const ExportOptions& opts);

// An export writes four names at the top of the destination: Contents, PIONEER, SHA256SUMS and trakcrate-manifest.txt. Anything else on a stick
// (photos, a .Trash-1000 folder) is none of its business and is left alone. These are the ones of the four that already exist in `dest`
// (compared without regard to case, as on FAT), which an export refuses to write over; empty when dest does not exist.
std::vector<std::string> exportConflicts(const fs::path& dest);

// A file name safe on FAT/exFAT: characters Windows forbids replaced by '_', no trailing dots or spaces, reserved device names avoided.
std::string fatSafeName(const std::string& name);

// The folder name rekordbox gives an artist or album under Contents/ (measured on the 2195 tracks of a real export, 2184 match exactly and the rest
// differ only in letter case, see rekordboxLayoutPath): characters Windows forbids become '_', at most 48 characters (code points) are kept,
// trailing spaces go, a final '.' becomes '_' ("K.O." is "K.O_"), and nothing at all is `fallback` ("UnknownArtist", "UnknownAlbum").
std::string rekordboxFolderName(const std::string& name, const char* fallback);

} // namespace trakcrate

#endif
