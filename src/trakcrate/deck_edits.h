#ifndef TRAKCRATE_DECK_EDITS_H
#define TRAKCRATE_DECK_EDITS_H

#include "library.h"
#include "track.h"

#include "pqtz_offset.h"

#include <cdj/anlz.h>

#include <string>
#include <vector>

// Beat grid and cue point edits made on a deck, copied back into the trakcrate library (doc/deck-edits.md,
// .planning/plans/deck-edits-copy-back_PLAN.md). Reading a deck's (or a stick's) analysis files never writes anything; applyDeckEdits() is the
// only writer and touches beatgrid.txt, cuemarks.txt and source.txt of the chosen track folders, after a snapshot into .trakcrate-history.
namespace trakcrate {

// ---- decoding, shared with the drive import (rb_import.cpp) ----
// The first tempo of the grid of an opened ANLZ file (trakcrate keeps one constant tempo); *variable is set when the tempo changes within the track.
bool gridFromAnlz(anlz_track_t* a, BeatGrid* grid, bool* variable, int offsetMs = 0);   // offsetMs: pqtzGridOffsetMs() of the file
// All cue points, memory cues and loops as cuemarks (the beat column is filled from the grid when it has one).
std::vector<Cuemark> cuesFromAnlz(anlz_track_t* a, const BeatGrid& grid);

struct DeckAnalysis {
    bool ok = false;                 // the analysis files were opened
    bool hasGrid = false;
    BeatGrid grid;
    bool variableTempo = false;
    std::vector<Cuemark> cues;
    std::string error;
};
// root: the folder holding PIONEER/; analyzePath: the database's path of the .DAT file.
DeckAnalysis readDeckAnalysis(const fs::path& root, const std::string& analyzePath);

// ---- which library track is it ----
struct LibraryEntry {
    fs::path dir;
    std::string title, artist, stem;       // stem: the track folder's name, the exporter's file name under Contents/ is made from it
};
std::vector<LibraryEntry> indexLibrary(const Library& lib);

struct DeckTrackKey {
    std::string title, artist;
    std::string fileStem;                  // file name of the audio on the deck without the extension
};
struct Match {
    fs::path dir;                          // empty when there is no single match
    std::string how;                       // "title, artist and file name", "title and artist", "file name", ...
    std::vector<fs::path> candidates;      // more than one: ambiguous
};
Match matchLibraryTrack(const std::vector<LibraryEntry>& index, const DeckTrackKey& key);

// ---- what would change ----
struct GridDiff {
    bool changed = false;
    bool hadGrid = false;
    double oldBpm = 0, newBpm = 0;
    double oldFirstMs = 0, newFirstMs = 0;
    double shiftMs = 0;                    // new first beat minus the old one
};
struct CueDiff {
    bool changed = false;
    int added = 0, moved = 0, removed = 0;
};
GridDiff diffGrid(const Track& t, const DeckAnalysis& deck);
CueDiff diffCues(const Track& t, const DeckAnalysis& deck);

struct CopyBackOptions {
    bool grid = false, cues = false;
    bool dryRun = false;
    std::string from;                      // recorded in source.txt, for example "deck 2" 
};
struct CopyBackResult {
    bool ok = false;
    bool changed = false;                  // something was (or in a dry run would be) written
    GridDiff grid;
    CueDiff cues;
    fs::path snapshot;                     // empty for a dry run or when nothing changed
    std::string error;
};
// Replaces the track's grid and/or its cue list by the deck's. Nothing is written when there is no difference.
CopyBackResult applyDeckEdits(const fs::path& trackDir, const DeckAnalysis& deck, const CopyBackOptions& opts);

} // namespace trakcrate

#endif
