#ifndef TRAKCRATE_TRACK_H
#define TRAKCRATE_TRACK_H

#include "fsutil.h"

#include <string>
#include <vector>

// The per-track files of the trakcrate master (doc/trakcrate/trakcrate-db-format.md): meta.txt, source.txt, beatgrid.txt,
// cuemarks.txt, vocals.txt. All plain UTF-8 text, written atomically, tolerant of comments and unknown content.
namespace trakcrate {

// "key: value" lines. Comments, blank lines and unknown keys survive a parse/serialise round trip in their original position.
// Newlines and backslashes inside values are escaped as \n and \\.
class KvDoc {
public:
    static KvDoc parse(const std::string& text);
    std::string serialise() const;

    bool has(const std::string& key) const;
    std::string get(const std::string& key, const std::string& def = std::string()) const;
    long long getInt(const std::string& key, long long def = 0) const;
    double getDouble(const std::string& key, double def = 0) const;
    void set(const std::string& key, const std::string& value);     // replaces in place, or appends
    void setInt(const std::string& key, long long value);
    void remove(const std::string& key);
    std::vector<std::string> keys() const;
    void addComment(const std::string& text);                        // appended as "# text"
    bool empty() const { return lines_.empty(); }

private:
    struct Line {
        bool isKv = false;
        std::string raw;      // comment, blank or malformed line, verbatim
        std::string key, value;
    };
    std::vector<Line> lines_;
};

// source.txt: where each group of fields came from. Groups: meta, beatgrid, cues, waveform, artwork.
// Origins: generated, rekordbox, edited. A missing entry reads as "generated".
std::string origin(const KvDoc& source, const std::string& group);
void setOrigin(KvDoc& source, const std::string& group, const std::string& value);

// beatgrid.txt: a constant tempo grid. bpm 0 = no usable grid ("analysed, found none").
struct BeatGrid {
    double bpm = 0;
    long long firstBeatUs = 0;       // time of beat 1 from the start of the audio
    bool empty() const { return bpm <= 0; }
};
BeatGrid beatGridFrom(const KvDoc& doc);
void setBeatGrid(KvDoc& doc, const BeatGrid& grid);

// cuemarks.txt, one mark per line, tab separated: beat  ms  label  hot  colour  (see doc/trakcrate/cuemarks.md).
// beat is NaN when the track has no grid ("-" in the file). hot is 0 or 'A'..'C'. Extra columns are kept.
struct Cuemark {
    double beat = 0;
    double ms = 0;
    std::string label;
    char hot = 0;
    std::string colour;
    std::vector<std::string> extra;
};
struct CuemarkFile {
    std::vector<std::string> comments;      // leading "#" lines, kept
    std::vector<Cuemark> marks;
};
CuemarkFile parseCuemarks(const std::string& text);
std::string serialiseCuemarks(const CuemarkFile& f);

// vocals.txt (format only; there is no analyser yet): start_beat end_beat start_ms end_ms kind confidence
struct VocalRegion {
    double startBeat = 0, endBeat = 0;       // NaN when there is no grid
    double startMs = 0, endMs = 0;
    std::string kind = "vocal";
    double confidence = 1;
    std::vector<std::string> extra;
};
struct VocalsFile {
    std::vector<std::string> comments;
    std::vector<VocalRegion> regions;
};
VocalsFile parseVocals(const std::string& text);
std::string serialiseVocals(const VocalsFile& f);

// A track folder in track-master-copy/<folder>/<track>/.
struct Track {
    fs::path dir;
    KvDoc meta;
    KvDoc source;
    KvDoc grid;                       // beatgrid.txt (use beatGridFrom / setBeatGrid)
    bool hasGrid = false;             // beatgrid.txt exists
    CuemarkFile cuemarks;
    bool hasCuemarks = false;
    VocalsFile vocals;
    bool hasVocals = false;

    std::string title() const { return meta.get("title"); }
    int rating() const { return int(meta.getInt("rating", 0)); }
    std::string comment() const { return meta.get("comment"); }
};

// Reads whatever files exist (a folder with only audio loads fine). False if dir is not a directory.
bool loadTrack(const fs::path& dir, Track* out, std::string* err = nullptr);

// Writes the files that exist in the Track (each atomically). Files that are empty/absent in the Track are left alone, never deleted.
bool saveMeta(const Track& t, std::string* err = nullptr);
bool saveSource(const Track& t, std::string* err = nullptr);
bool saveBeatGrid(const Track& t, std::string* err = nullptr);
bool saveCuemarks(const Track& t, std::string* err = nullptr);
bool saveVocals(const Track& t, std::string* err = nullptr);

// The audio file of a track folder (the first .mp3, .wav or .flac, case-insensitive), empty path if none.
fs::path audioFileOf(const fs::path& trackDir);

} // namespace trakcrate

#endif
