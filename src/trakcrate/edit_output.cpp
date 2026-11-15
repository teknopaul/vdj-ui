#include "edit_output.h"

#include "audio_probe.h"
#include "mp3_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace trakcrate {

namespace {

std::string lower(std::string s)
{
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

std::string trimmed(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool endsWithNoCase(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && lower(s.substr(s.size() - suffix.size())) == lower(suffix);
}

std::string withoutOriginalMix(const std::string& title)
{
    std::string t = trimmed(title);
    static const std::string kOm = "(original mix)";
    if (endsWithNoCase(t, kOm)) t = trimmed(t.substr(0, t.size() - kOm.size()));
    return t;
}

std::string num(double v, int decimals)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

void put16(std::string& s, unsigned v) { s += char(v & 255); s += char((v >> 8) & 255); }
void put32(std::string& s, unsigned long v) { put16(s, unsigned(v & 0xFFFF)); put16(s, unsigned((v >> 16) & 0xFFFF)); }

void infoChunk(std::string& list, const char* id, const std::string& value)
{
    if (value.empty()) return;
    std::string v = value;
    v.push_back('\0');
    list.append(id, 4);
    put32(list, (unsigned long)v.size());
    list += v;
    if (v.size() & 1) list.push_back('\0');
}

std::string avError(int rc)
{
    char buf[128];
    av_strerror(rc, buf, sizeof buf);
    return buf;
}

} // namespace

std::string AudioTags::get(const std::string& key) const
{
    auto it = tags.find(key);
    return it == tags.end() ? std::string() : it->second;
}

bool readAudioTags(const fs::path& audioFile, AudioTags* out, std::string* err)
{
    AudioInfo info;
    if (!probeAudio(audioFile, &info, err)) return false;
    out->tags = info.tags;
    return true;
}

std::string editTitle(const std::string& title, const std::string& user)
{
    const std::string t = withoutOriginalMix(title);
    if (t.empty()) return t;
    const std::string suffix = "(" + user + " Edit)";
    if (endsWithNoCase(t, suffix)) return t;
    return t + " " + suffix;
}

std::string loopTitle(const std::string& title, const std::string& name, const std::string& user)
{
    const std::string t = withoutOriginalMix(title);
    const std::string suffix = "(" + (name.empty() ? user : name + " " + user) + " Edit)";
    return t.empty() ? suffix : t + " " + suffix;
}

std::string fileUser(const std::string& user)
{
    std::string s;
    for (char c : trimmed(user)) {
        if (std::isspace((unsigned char)c) || c == '_' || c == '/' || c == '\\') s += '-';
        else s += c;
    }
    s = lower(sanitizeName(s));
    while (!s.empty() && s[0] == '-') s.erase(0, 1);
    return s.empty() || s == "_" ? "user" : s;
}

const char* formatExt(OutFormat f) { return f == OutFormat::Wav ? "wav" : "mp3"; }

fs::path uniqueEditPath(const fs::path& folder, const std::string& stem, const std::string& user, const std::string& ext)
{
    const std::string base = sanitizeName(stem) + "_" + fileUser(user) + "_edit";
    std::error_code ec;
    for (int n = 1;; ++n) {
        const std::string name = n == 1 ? base : base + "_" + std::to_string(n);
        fs::path p = folder / (name + "." + ext);
        fs::path txt = folder / (name + ".txt");
        if (!fs::exists(p, ec) && !fs::exists(txt, ec)) return p;
    }
}

bool writeWav(const fs::path& path, const DecodedPcm& pcm, double bpm, const AudioTags& tags, std::string* err, int beats)
{
    const size_t frames = pcm.frames();
    std::string head;                                           // everything between "WAVE" and the sample data
    head += "fmt ";
    put32(head, 16);
    put16(head, 1);                                             // PCM
    put16(head, 2);
    put32(head, (unsigned long)pcm.sampleRate);
    put32(head, (unsigned long)pcm.sampleRate * 4);
    put16(head, 4);
    put16(head, 16);
    if (bpm > 0) {
        head += "acid";
        put32(head, 24);
        put32(head, 0x10);                                      // "acidized": a loop (not one shot), tempo and beats are set
        put16(head, 0);                                         // root note, unused
        put16(head, 0x8000);
        put32(head, 0);
        put32(head, (unsigned long)std::max(0, beats));
        put16(head, 4);                                         // meter 4/4
        put16(head, 4);
        const float tempo = float(bpm);
        uint32_t bits;
        std::memcpy(&bits, &tempo, 4);
        put32(head, bits);
    }
    std::string list;
    infoChunk(list, "INAM", tags.get("title"));
    infoChunk(list, "IART", tags.get("artist"));
    infoChunk(list, "IPRD", tags.get("album"));
    infoChunk(list, "ICMT", tags.get("comment"));
    infoChunk(list, "ICRD", tags.get("date"));
    infoChunk(list, "IGNR", tags.get("genre"));
    if (!list.empty()) {
        head += "LIST";
        put32(head, (unsigned long)list.size() + 4);
        head += "INFO";
        head += list;
    }
    const unsigned long dataBytes = (unsigned long)frames * 4;
    std::string hdr = "RIFF";
    put32(hdr, 4 + (unsigned long)head.size() + 8 + dataBytes);
    hdr += "WAVE";
    hdr += head;
    hdr += "data";
    put32(hdr, dataBytes);

    std::string body;
    body.resize(size_t(dataBytes));
    for (size_t i = 0; i < frames * 2; ++i) {
        const float v = std::max(-1.0f, std::min(1.0f, pcm.stereo[i]));
        const int s = int(std::lrintf(v * 32767.0f));
        body[i * 2] = char(s & 255);
        body[i * 2 + 1] = char((s >> 8) & 255);
    }
    std::string tmp = path.string() + ".part";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(hdr.data(), std::streamsize(hdr.size()));
        f.write(body.data(), std::streamsize(body.size()));
        f.flush();
        if (!f) {
            if (err) *err = "cannot write " + tmp;
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        if (err) *err = "cannot rename to " + path.string() + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool writeMp3(const fs::path& path, const DecodedPcm& pcm, int kbps, double bpm, const AudioTags& tags, std::string* err)
{
    Mp3Writer w;
    w.setBpm(bpm);
    if (!w.open(path, kbps, false, tags, pcm.sampleRate, err)) return false;
    if (!w.write(pcm.stereo.data(), pcm.frames(), err)) return false;       // the destructor removes the .part file
    return w.close(err);
}

std::string nowIso()
{
    std::time_t t = std::time(nullptr);
    std::tm tm;
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

std::string describeRegion(int number, const EditRegion& r)
{
    const bool bars = r.startBeat % 4 == 0 && r.lenBeats % 4 == 0;
    std::string s = "#" + std::to_string(number) + " " + (r.kind == EditKind::Cut ? "cut" : "loop") + " ";
    const int first = r.startBeat + 1, last = r.startBeat + r.lenBeats;                  // 1 based beat numbers
    if (bars) s += "bars " + std::to_string(r.startBeat / 4 + 1) + "-" + std::to_string(last / 4);
    else s += "beats " + std::to_string(first) + "-" + std::to_string(last);
    if (r.kind == EditKind::Loop) s += " x" + std::to_string(r.loopTimes);
    auto algo = [](EditAlgo a, double p) {
        switch (a) {
        case EditAlgo::Cut: return "cut " + num(p, 0) + " ms";
        case EditAlgo::Cross: return "cross " + num(p, 2) + " beat";
        case EditAlgo::Fadeout: return "fadeout " + num(p, 2) + " beat";
        case EditAlgo::TailOverlap: return "tailoverlap " + num(p, 0) + " beats";
        default: return std::string("earlycut");
        }
    };
    if (r.algo == EditAlgo::EarlyCut)
        s += " algo=earlycut " + std::to_string(r.earlyShift) + " beats " + (r.earlyAfter ? "after" : "before") + " join=" + algo(r.earlyJoinAlgo, r.earlyJoinParam);
    else
        s += " algo=" + algo(r.algo, r.algoParam);
    return s;
}

bool writeInfoFile(const fs::path& txt, const EditInfo& info, std::string* err)
{
    std::string s = "# trakcrate edit\n";
    s += "name: " + info.name + "\n";
    s += "source: " + info.sourceFile + "\n";
    s += "author: " + info.author + "\n";
    s += "timestamp: " + info.timestamp + "\n";
    s += "source_bpm: " + num(info.sourceBpm, 3) + "\n";
    s += "source_first_beat_ms: " + num(info.sourceFirstBeatMs, 3) + "\n";
    s += "bpm: " + num(info.bpm, 3) + "\n";
    s += "first_beat_ms: " + num(info.firstBeatMs, 3) + "\n";
    s += "duration_s: " + num(info.durationSec, 3) + "\n";
    s += "regions:\n";
    for (const std::string& r : info.regions) s += "  " + r + "\n";
    return atomicWrite(txt, s, err);
}

} // namespace trakcrate
