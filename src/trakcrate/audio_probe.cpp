#include "audio_probe.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

#include <algorithm>
#include <cctype>

namespace trakcrate {

static void copyTags(const AVDictionary* d, std::map<std::string, std::string>* out)
{
    const AVDictionaryEntry* e = nullptr;
    while ((e = av_dict_get(d, "", e, AV_DICT_IGNORE_SUFFIX)) != nullptr) {
        std::string k = e->key;
        std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (out->find(k) == out->end()) (*out)[k] = e->value;       // first one wins
    }
}

bool probeAudio(const fs::path& path, AudioInfo* out, std::string* err)
{
    AVFormatContext* fmt = nullptr;
    int rc = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
    if (rc < 0) {
        if (err) { char buf[128]; av_strerror(rc, buf, sizeof buf); *err = std::string("cannot open: ") + buf; }
        return false;
    }
    struct Closer { AVFormatContext** f; ~Closer() { avformat_close_input(f); } } closer{&fmt};
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        if (err) *err = "cannot read stream information";
        return false;
    }
    int audio = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio < 0) {
        if (err) *err = "no audio stream";
        return false;
    }
    const AVStream* st = fmt->streams[audio];
    const AVCodecParameters* cp = st->codecpar;

    AudioInfo info;
    info.container = fmt->iformat && fmt->iformat->name ? fmt->iformat->name : "";
    size_t comma = info.container.find(',');
    if (comma != std::string::npos) info.container.resize(comma);    // "mp3", not "mp3,mp2,..."
    if (fmt->duration > 0) info.durationSec = double(fmt->duration) / AV_TIME_BASE;
    else if (st->duration > 0) info.durationSec = double(st->duration) * av_q2d(st->time_base);
    info.sampleRate = cp->sample_rate;
    info.channels = cp->ch_layout.nb_channels;
    int bits = av_get_bits_per_sample(cp->codec_id);
    info.bitDepth = bits > 0 ? bits : (cp->bits_per_raw_sample > 0 ? cp->bits_per_raw_sample : 16);
    info.bitrate = long(cp->bit_rate > 0 ? cp->bit_rate : fmt->bit_rate);
    if (info.sampleRate <= 0 || info.channels <= 0) {
        if (err) *err = "not usable audio (no sample rate or channels)";
        return false;
    }

    copyTags(fmt->metadata, &info.tags);
    copyTags(st->metadata, &info.tags);
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVStream* s = fmt->streams[i];
        if (!(s->disposition & AV_DISPOSITION_ATTACHED_PIC) || s->attached_pic.size <= 0) continue;
        info.picture.assign(reinterpret_cast<const char*>(s->attached_pic.data), size_t(s->attached_pic.size));
        info.pictureExt = s->codecpar->codec_id == AV_CODEC_ID_PNG ? "png" : "jpg";
        break;
    }
    *out = info;
    return true;
}

} // namespace trakcrate
