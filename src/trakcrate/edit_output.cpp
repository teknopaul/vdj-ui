#include "edit_output.h"

#include "audio_probe.h"

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
    struct Ctx {
        AVFormatContext* oc = nullptr;
        AVCodecContext* enc = nullptr;
        AVFrame* frame = nullptr;
        AVPacket* pkt = nullptr;
        ~Ctx()
        {
            av_packet_free(&pkt);
            av_frame_free(&frame);
            avcodec_free_context(&enc);
            if (oc) {
                if (oc->pb) avio_closep(&oc->pb);
                avformat_free_context(oc);
            }
        }
    } c;
    const std::string tmp = path.string() + ".part";
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        std::error_code ec;
        fs::remove(tmp, ec);
        return false;
    };
    const AVCodec* codec = avcodec_find_encoder_by_name("libmp3lame");
    if (!codec) return fail("this ffmpeg has no libmp3lame encoder");
    int rc = avformat_alloc_output_context2(&c.oc, nullptr, "mp3", tmp.c_str());
    if (rc < 0 || !c.oc) return fail("cannot set up the mp3 writer: " + avError(rc));
    AVStream* st = avformat_new_stream(c.oc, nullptr);
    c.enc = avcodec_alloc_context3(codec);
    if (!st || !c.enc) return fail("out of memory");
    AVSampleFormat fmt = codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
    for (const AVSampleFormat* f = codec->sample_fmts; f && *f != AV_SAMPLE_FMT_NONE; ++f)
        if (*f == AV_SAMPLE_FMT_FLTP) fmt = *f;
    if (fmt != AV_SAMPLE_FMT_FLTP && fmt != AV_SAMPLE_FMT_S16P && fmt != AV_SAMPLE_FMT_S32P) return fail("unsupported encoder sample format");
    c.enc->sample_fmt = fmt;
    c.enc->sample_rate = pcm.sampleRate;
    c.enc->bit_rate = (int64_t)kbps * 1000;
    av_channel_layout_default(&c.enc->ch_layout, 2);
    c.enc->time_base = AVRational{1, pcm.sampleRate};
    if ((rc = avcodec_open2(c.enc, codec, nullptr)) < 0) return fail("cannot open the mp3 encoder: " + avError(rc));
    avcodec_parameters_from_context(st->codecpar, c.enc);
    st->time_base = c.enc->time_base;

    static const char* const kSkip[] = {"encoder", "bpm", "tbpm", "major_brand", "minor_version", "compatible_brands", "creation_time"};
    for (const auto& kv : tags.tags) {
        if (kv.second.empty() || std::find_if(std::begin(kSkip), std::end(kSkip), [&](const char* k) { return kv.first == k; }) != std::end(kSkip)) continue;
        av_dict_set(&c.oc->metadata, kv.first.c_str(), kv.second.c_str(), 0);
    }
    if (bpm > 0) av_dict_set(&c.oc->metadata, "TBPM", num(std::round(bpm * 100) / 100, 2).c_str(), 0);

    if ((rc = avio_open(&c.oc->pb, tmp.c_str(), AVIO_FLAG_WRITE)) < 0) return fail("cannot write " + tmp + ": " + avError(rc));
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "id3v2_version", "3", 0);
    rc = avformat_write_header(c.oc, &opts);
    av_dict_free(&opts);
    if (rc < 0) return fail("cannot write the mp3 header: " + avError(rc));

    c.frame = av_frame_alloc();
    c.pkt = av_packet_alloc();
    if (!c.frame || !c.pkt) return fail("out of memory");
    const int fs_ = c.enc->frame_size > 0 ? c.enc->frame_size : 1152;
    const size_t total = pcm.frames();
    auto drain = [&]() -> bool {
        for (;;) {
            int r = avcodec_receive_packet(c.enc, c.pkt);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
            if (r < 0) return false;
            av_packet_rescale_ts(c.pkt, c.enc->time_base, st->time_base);
            c.pkt->stream_index = st->index;
            r = av_interleaved_write_frame(c.oc, c.pkt);
            if (r < 0) return false;
        }
    };
    for (size_t at = 0; at < total; at += size_t(fs_)) {
        const int n = int(std::min<size_t>(size_t(fs_), total - at));
        av_frame_unref(c.frame);
        c.frame->nb_samples = n;
        c.frame->format = fmt;
        c.frame->sample_rate = pcm.sampleRate;
        av_channel_layout_copy(&c.frame->ch_layout, &c.enc->ch_layout);
        c.frame->pts = (int64_t)at;
        if (av_frame_get_buffer(c.frame, 0) < 0) return fail("out of memory");
        for (int ch = 0; ch < 2; ++ch) {
            for (int k = 0; k < n; ++k) {
                const float v = std::max(-1.0f, std::min(1.0f, pcm.stereo[(at + size_t(k)) * 2 + size_t(ch)]));
                if (fmt == AV_SAMPLE_FMT_FLTP) reinterpret_cast<float*>(c.frame->data[ch])[k] = v;
                else if (fmt == AV_SAMPLE_FMT_S16P) reinterpret_cast<int16_t*>(c.frame->data[ch])[k] = int16_t(std::lrintf(v * 32767.0f));
                else reinterpret_cast<int32_t*>(c.frame->data[ch])[k] = int32_t(std::llrintf(v * 2147483647.0f));
            }
        }
        if (avcodec_send_frame(c.enc, c.frame) < 0 || !drain()) return fail("mp3 encoding failed");
    }
    avcodec_send_frame(c.enc, nullptr);
    if (!drain()) return fail("mp3 encoding failed");
    if ((rc = av_write_trailer(c.oc)) < 0) return fail("cannot finish the mp3: " + avError(rc));
    avio_closep(&c.oc->pb);
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) return fail("cannot rename to " + path.string() + ": " + ec.message());
    return true;
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
