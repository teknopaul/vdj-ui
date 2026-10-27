#include "artwork_edit.h"

#include "audio_probe.h"
#include "track.h"

namespace trakcrate {

namespace {

const char* const kNames[] = {"artwork.jpg", "artwork.jpeg", "artwork.png"};

fs::path historyDir(const fs::path& d) { return d / ".trakcrate-history"; }

bool mark(const fs::path& dir, std::string* err)
{
    Track t;
    if (!loadTrack(dir, &t, err)) return false;
    setOrigin(t.source, "artwork", "edited");
    return saveSource(t, err);
}

void removeAll(const fs::path& dir)
{
    std::error_code ec;
    for (const char* n : kNames) fs::remove(dir / n, ec);
}

bool fail(std::string* err, const std::string& m)
{
    if (err) *err = m;
    return false;
}

} // namespace

std::string imageExtensionOf(const std::string& b)
{
    if (b.size() >= 3 && (unsigned char)b[0] == 0xFF && (unsigned char)b[1] == 0xD8 && (unsigned char)b[2] == 0xFF) return "jpg";
    if (b.size() >= 8 && b.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0) return "png";
    return std::string();
}

fs::path artworkOf(const fs::path& trackDir)
{
    std::error_code ec;
    for (const char* n : kNames)
        if (fs::is_regular_file(trackDir / n, ec)) return trackDir / n;
    return fs::path();
}

bool backupArtwork(const fs::path& trackDir, std::string* err)
{
    const fs::path cur = artworkOf(trackDir);
    if (cur.empty()) return true;
    std::string bytes;
    if (!readFile(cur, &bytes, err)) return false;
    std::error_code ec;
    fs::create_directories(historyDir(trackDir), ec);
    for (const char* n : kNames) fs::remove(historyDir(trackDir) / n, ec);          // one level: the older backup goes
    return atomicWrite(historyDir(trackDir) / cur.filename(), bytes, err);
}

bool replaceArtwork(const fs::path& trackDir, const fs::path& src, std::string* err)
{
    std::string bytes;
    if (!readFile(src, &bytes, err)) return false;
    const std::string ext = imageExtensionOf(bytes);
    if (ext.empty()) return fail(err, "not a JPEG or PNG image: " + src.string());
    if (!backupArtwork(trackDir, err)) return false;
    removeAll(trackDir);
    if (!atomicWrite(trackDir / ("artwork." + ext), bytes, err)) return false;
    return mark(trackDir, err);
}

bool removeArtwork(const fs::path& trackDir, std::string* err)
{
    if (artworkOf(trackDir).empty()) return true;
    if (!backupArtwork(trackDir, err)) return false;
    removeAll(trackDir);
    return mark(trackDir, err);
}

bool reextractArtwork(const fs::path& trackDir, std::string* err)
{
    const fs::path audio = audioFileOf(trackDir);
    if (audio.empty()) return fail(err, "no audio file in " + trackDir.string());
    AudioInfo info;
    if (!probeAudio(audio, &info, err)) return false;
    if (info.picture.empty()) return fail(err, "the audio file has no embedded cover");
    if (!backupArtwork(trackDir, err)) return false;
    removeAll(trackDir);
    if (!atomicWrite(trackDir / ("artwork." + info.pictureExt), info.picture, err)) return false;
    Track t;
    if (!loadTrack(trackDir, &t, err)) return false;
    setOrigin(t.source, "artwork", "generated");              // it is the tag's picture again
    return saveSource(t, err);
}

bool restoreArtwork(const fs::path& trackDir, std::string* err)
{
    fs::path back;
    std::error_code ec;
    for (const char* n : kNames)
        if (fs::is_regular_file(historyDir(trackDir) / n, ec)) back = historyDir(trackDir) / n;
    if (back.empty()) return fail(err, "no earlier artwork to restore");
    std::string bytes;
    if (!readFile(back, &bytes, err)) return false;
    const std::string name = back.filename().string();
    if (!backupArtwork(trackDir, err)) return false;           // the current one becomes the backup (this removes the one being restored, read above)
    removeAll(trackDir);
    if (!atomicWrite(trackDir / name, bytes, err)) return false;
    return mark(trackDir, err);
}

bool markArtworkEdited(const fs::path& trackDir, std::string* err) { return mark(trackDir, err); }

} // namespace trakcrate
