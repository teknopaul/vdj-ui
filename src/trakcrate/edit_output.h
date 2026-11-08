#ifndef TRAKCRATE_EDIT_OUTPUT_H
#define TRAKCRATE_EDIT_OUTPUT_H

#include "audio_decode.h"
#include "edit_plan.h"
#include "fsutil.h"

#include <map>
#include <string>
#include <vector>

// Writing a rendered edit, no Qt: .wav (with the Acid tempo chunk) and CBR .mp3 files with tags, the title and file name rules, and the
// plain text file that describes the edit. The source audio is only ever read.
namespace trakcrate {

// Tags in ffmpeg's lower case names (title, artist, album, genre, date, track, comment ...), as probeAudio reports them.
struct AudioTags {
    std::map<std::string, std::string> tags;
    std::string get(const std::string& key) const;
};
bool readAudioTags(const fs::path& audioFile, AudioTags* out, std::string* err = nullptr);

// "Foo (Original Mix)" -> "Foo (Alice Edit)"; a title already ending "(Alice Edit)" is kept; an empty title stays empty. user is the display name.
std::string editTitle(const std::string& title, const std::string& user);
// "Foo (Original Mix)" -> "Foo (name Alice Edit)", for acid loops.
std::string loopTitle(const std::string& title, const std::string& name, const std::string& user);
// The user as written in file names: lower case, one path component, no spaces ("Alice B" -> "alice-b"); "user" when nothing is left.
std::string fileUser(const std::string& user);

enum class OutFormat { Wav, Mp3 };
constexpr int kMp3Kbps[] = {128, 192, 256, 320};
const char* formatExt(OutFormat f);                                 // "wav", "mp3"

// <folder>/<stem>_<user>_edit.<ext>, or _edit_2, _edit_3 ... when that file or its .txt exists. Never an existing path.
fs::path uniqueEditPath(const fs::path& folder, const std::string& stem, const std::string& user, const std::string& ext);

// 16 bit PCM, RIFF "acid" chunk with the tempo (omitted when bpm <= 0; beats = the length in beats, 0 when not known) and a LIST/INFO chunk from the tags.
bool writeWav(const fs::path& path, const DecodedPcm& pcm, double bpm, const AudioTags& tags, std::string* err = nullptr, int beats = 0);
// libmp3lame, constant bitrate, ID3v2.3 tags, TBPM from bpm (rounded to 2 decimals) when bpm > 0. Written to <path>.part, then renamed.
bool writeMp3(const fs::path& path, const DecodedPcm& pcm, int kbps, double bpm, const AudioTags& tags, std::string* err = nullptr);

struct EditInfo {
    std::string name;                  // the output file name
    std::string sourceFile;            // the original's file name
    std::string author;
    std::string timestamp;             // ISO 8601
    double sourceBpm = 0;
    double sourceFirstBeatMs = 0;
    double bpm = 0;
    double firstBeatMs = 0;            // of the output
    double durationSec = 0;
    std::vector<std::string> regions;  // describeRegion() lines
};
bool writeInfoFile(const fs::path& txt, const EditInfo& info, std::string* err = nullptr);

// "#1 cut bars 3-4 algo=cross 1 beat", "#2 loop beats 65-68 x4 algo=fadeout 0.5 beat" (bars when the region starts on a bar line).
std::string describeRegion(int number, const EditRegion& r);
std::string nowIso();

} // namespace trakcrate

#endif
