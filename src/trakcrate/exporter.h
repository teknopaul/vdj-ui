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
// is read back and compared by SHA-256. The destination must be an empty directory. Nothing is ever written anywhere else.
namespace trakcrate {

// Turns the bytes of an image file into a square JPEG of `size` pixels (the exporter asks for 240 and 80). Qt-free code cannot do that itself.
using ArtworkEncoder = std::function<bool(const std::string& source, int size, std::string* jpeg, std::string* err)>;

// A list of tracks that is not a playlist of the library (an entry of a user defined pendrive): its name on the drive and its tracks in order.
struct ExportList {
    std::string name;
    std::vector<fs::path> tracks;
};

struct ExportOptions {
    std::vector<std::string> playlists;      // the playlists (library "pendrives") to put on the drive, in this order
    std::vector<ExportList> lists;           // further lists, after the playlists (see Library::driveEntries)
    fs::path dest;                           // an empty directory, or one that does not exist yet
    std::string templatePdb;                 // bytes of the template export.pdb (required)
    fs::path stagingBase;                    // where the staging directory goes; empty = /dev/shm, else $XDG_RUNTIME_DIR, else the system temp
    bool verify = true;                      // read the destination back and compare checksums
    bool allowUnanalysed = false;            // tracks without analysis get a flat waveform and no beat grid instead of refusing the export
    uint64_t minFreeBytes = 1u << 20;        // space that must remain free on the destination

    // compatibility testing: which optional pieces to include (all on by default)
    bool includeSettings = true;             // PIONEER/DEVSETTING.DAT, MYSETTING.DAT, djprofile.nxs and rekordbox/RBFLTR.DAT, exportExt.pdb: players need them all
    bool includeArtwork = true;
    bool includePvbr = true, includePwv4 = true, includePwv5 = true;
    CuemarkRenderOptions cuemarks;           // the spikes drawn into the exported waveforms (never into the master)
    bool writeManifest = true;               // SHA256SUMS and trakcrate-manifest.txt in the root of the drive
    std::string devSetting, mySetting;       // replacement settings files (bytes); empty = the built in ones
    std::string djprofile, rbfltr, exportExt;   // replacements for PIONEER/djprofile.nxs, rekordbox/RBFLTR.DAT, rekordbox/exportExt.pdb; empty = the built in ones
    // Files copied from a real drive (a folder holding PIONEER/): other tracks' data, only to find out which files a player needs.
    fs::path optionalFilesFrom;
    bool copyExportExt = false, copyRbfltr = false, copyUsbmng = false, copyDjprofile = false;

    ArtworkEncoder encodeArtwork;            // null: no artwork on the drive
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
};

struct ExportSummary {
    int tracks = 0;
    uint64_t audioBytes = 0, metadataBytes = 0;   // metadata is an estimate
    std::vector<std::string> problems;       // would refuse the export: missing audio, tracks not analysed ...
};

// done files, total files, the path being written.
using ExportProgress = std::function<void(int done, int total, const std::string& current)>;

ExportResult exportDrive(const Library& lib, const ExportOptions& opts, const ExportProgress& progress = nullptr, const std::atomic<bool>* cancel = nullptr);

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
