#include "mp3_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace trakcrate {

namespace {

std::string avErr(int rc)
{
    char b[128];
    av_strerror(rc, b, sizeof b);
    return b;
}

std::string num2(double v)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", v);
    std::string t = b;
    while (t.back() == '0') t.pop_back();          // "120.50" -> "120.5", "120.00" -> "120"
    if (t.back() == '.') t.pop_back();
    return t;
}

} // namespace

struct Mp3Writer::Impl {
    AVFormatContext* oc = nullptr;
    AVCodecContext* enc = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    AVPacket* picPkt = nullptr;                    // the cover, written once after the header
    AVStream* st = nullptr;
    AVSampleFormat fmt = AV_SAMPLE_FMT_FLTP;
    fs::path path, tmp, picture;
    double bpm = 0;
    int rate = 44100, frameSize = 1152;
    std::vector<float> pending;                    // interleaved frames waiting for a full encoder frame
    long long pts = 0;
    bool opened = false;

    ~Impl() { free(); }
    void free()
    {
        av_packet_free(&pkt);
        av_packet_free(&picPkt);
        av_frame_free(&frame);
        avcodec_free_context(&enc);
        if (oc) {
            if (oc->pb) avio_closep(&oc->pb);
            avformat_free_context(oc);
            oc = nullptr;
        }
    }
    void removeTmp()
    {
        std::error_code ec;
        if (!tmp.empty()) fs::remove(tmp, ec);
    }
    bool drain()
    {
        for (;;) {
            int r = avcodec_receive_packet(enc, pkt);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
            if (r < 0) return false;
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            pkt->stream_index = st->index;
            r = av_interleaved_write_frame(oc, pkt);
            if (r < 0) return false;
        }
    }
    bool encode(const float* src, int n)
    {
        av_frame_unref(frame);
        frame->nb_samples = n;
        frame->format = fmt;
        frame->sample_rate = rate;
        av_channel_layout_copy(&frame->ch_layout, &enc->ch_layout);
        frame->pts = pts;
        if (av_frame_get_buffer(frame, 0) < 0) return false;
        for (int ch = 0; ch < 2; ++ch) {
            for (int k = 0; k < n; ++k) {
                const float v = std::max(-1.0f, std::min(1.0f, src[size_t(k) * 2 + size_t(ch)]));
                if (fmt == AV_SAMPLE_FMT_FLTP) reinterpret_cast<float*>(frame->data[ch])[k] = v;
                else if (fmt == AV_SAMPLE_FMT_S16P) reinterpret_cast<int16_t*>(frame->data[ch])[k] = int16_t(std::lrintf(v * 32767.0f));
                else reinterpret_cast<int32_t*>(frame->data[ch])[k] = int32_t(std::llrintf(v * 2147483647.0f));
            }
        }
        pts += n;
        return avcodec_send_frame(enc, frame) >= 0 && drain();
    }
};

Mp3Writer::Mp3Writer() : d_(new Impl) {}
Mp3Writer::~Mp3Writer() { abort(); }

void Mp3Writer::setBpm(double bpm) { d_->bpm = bpm; }
void Mp3Writer::setPicture(const fs::path& imageFile) { d_->picture = imageFile; }
bool Mp3Writer::isOpen() const { return d_->opened; }

bool Mp3Writer::open(const fs::path& path, int kbps, bool vbr, const AudioTags& tags, int sampleRate, std::string* err)
{
    Impl& d = *d_;
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        d.removeTmp();
        d.free();
        d.opened = false;
        return false;
    };
    d.path = path;
    d.tmp = path.string() + ".part";
    d.rate = sampleRate;
    const AVCodec* codec = avcodec_find_encoder_by_name("libmp3lame");
    if (!codec) return fail("this ffmpeg has no libmp3lame encoder");
    int rc = avformat_alloc_output_context2(&d.oc, nullptr, "mp3", d.tmp.c_str());
    if (rc < 0 || !d.oc) return fail("cannot set up the mp3 writer: " + avErr(rc));
    d.st = avformat_new_stream(d.oc, nullptr);
    if (!d.st) return fail("out of memory");

    d.fmt = codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
    for (const AVSampleFormat* f = codec->sample_fmts; f && *f != AV_SAMPLE_FMT_NONE; ++f)
        if (*f == AV_SAMPLE_FMT_FLTP) d.fmt = *f;
    if (d.fmt != AV_SAMPLE_FMT_FLTP && d.fmt != AV_SAMPLE_FMT_S16P && d.fmt != AV_SAMPLE_FMT_S32P) return fail("unsupported encoder sample format");

    // First try the requested mode; the encoder is reopened in the best VBR quality when it refuses ABR.
    for (int attempt = 0; attempt < (vbr ? 2 : 1); ++attempt) {
        avcodec_free_context(&d.enc);
        d.enc = avcodec_alloc_context3(codec);
        if (!d.enc) return fail("out of memory");
        d.enc->sample_fmt = d.fmt;
        d.enc->sample_rate = sampleRate;
        d.enc->bit_rate = (int64_t)kbps * 1000;
        av_channel_layout_default(&d.enc->ch_layout, 2);
        d.enc->time_base = AVRational{1, sampleRate};
        if (vbr && attempt == 0) {
            if (av_opt_set(d.enc->priv_data, "abr", "1", 0) < 0) continue;
        } else if (vbr) {
            d.enc->flags |= AV_CODEC_FLAG_QSCALE;
            d.enc->global_quality = 0;
        }
        if ((rc = avcodec_open2(d.enc, codec, nullptr)) >= 0) break;
        if (attempt + 1 >= (vbr ? 2 : 1)) return fail("cannot open the mp3 encoder: " + avErr(rc));
    }
    if (!d.enc || !avcodec_is_open(d.enc)) return fail("cannot open the mp3 encoder");
    avcodec_parameters_from_context(d.st->codecpar, d.enc);
    d.st->time_base = d.enc->time_base;
    d.frameSize = d.enc->frame_size > 0 ? d.enc->frame_size : 1152;

    static const char* const kSkip[] = {"encoder", "bpm", "tbpm", "major_brand", "minor_version", "compatible_brands", "creation_time"};
    for (const auto& kv : tags.tags) {
        if (kv.second.empty() || std::find_if(std::begin(kSkip), std::end(kSkip), [&](const char* k) { return kv.first == k; }) != std::end(kSkip)) continue;
        av_dict_set(&d.oc->metadata, kv.first.c_str(), kv.second.c_str(), 0);
    }
    if (d.bpm > 0) av_dict_set(&d.oc->metadata, "TBPM", num2(std::round(d.bpm * 100) / 100).c_str(), 0);

    // The cover: a second stream whose attached picture the mp3 muxer writes as APIC.
    if (!d.picture.empty()) {
        AVFormatContext* ic = nullptr;
        if ((rc = avformat_open_input(&ic, d.picture.c_str(), nullptr, nullptr)) < 0) return fail("cannot read the cover image: " + avErr(rc));
        struct Close { AVFormatContext** c; ~Close() { avformat_close_input(c); } } closer{&ic};
        if (avformat_find_stream_info(ic, nullptr) < 0 || ic->nb_streams < 1) return fail("the cover image is not a picture");
        const AVCodecParameters* cp = ic->streams[0]->codecpar;
        if (cp->codec_type != AVMEDIA_TYPE_VIDEO || (cp->codec_id != AV_CODEC_ID_MJPEG && cp->codec_id != AV_CODEC_ID_PNG))
            return fail("the cover image must be a jpeg or png");
        AVStream* ps = avformat_new_stream(d.oc, nullptr);
        if (!ps) return fail("out of memory");
        avcodec_parameters_copy(ps->codecpar, cp);
        ps->time_base = AVRational{1, 90000};
        ps->disposition = AV_DISPOSITION_ATTACHED_PIC;
        d.picPkt = av_packet_alloc();
        if (av_read_frame(ic, d.picPkt) < 0) return fail("cannot read the cover image");
        d.picPkt->stream_index = ps->index;
        d.picPkt->pts = d.picPkt->dts = 0;
        d.picPkt->flags |= AV_PKT_FLAG_KEY;
    }

    if ((rc = avio_open(&d.oc->pb, d.tmp.c_str(), AVIO_FLAG_WRITE)) < 0) return fail("cannot write " + d.tmp.string() + ": " + avErr(rc));
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "id3v2_version", "3", 0);
    rc = avformat_write_header(d.oc, &opts);
    av_dict_free(&opts);
    if (rc < 0) return fail("cannot write the mp3 header: " + avErr(rc));
    if (d.picPkt && (rc = av_interleaved_write_frame(d.oc, d.picPkt)) < 0) return fail("cannot write the cover image: " + avErr(rc));
    av_packet_free(&d.picPkt);
    d.frame = av_frame_alloc();
    d.pkt = av_packet_alloc();
    if (!d.frame || !d.pkt) return fail("out of memory");
    d.pts = 0;
    d.pending.clear();
    d.opened = true;
    return true;
}

bool Mp3Writer::write(const float* stereo, size_t frames, std::string* err)
{
    Impl& d = *d_;
    if (!d.opened) {
        if (err) *err = "the mp3 writer is not open";
        return false;
    }
    d.pending.insert(d.pending.end(), stereo, stereo + frames * 2);
    size_t used = 0;
    const size_t fsz = size_t(d.frameSize);
    while (d.pending.size() / 2 - used >= fsz) {
        if (!d.encode(d.pending.data() + used * 2, d.frameSize)) {
            if (err) *err = "mp3 encoding failed";
            return false;
        }
        used += fsz;
    }
    d.pending.erase(d.pending.begin(), d.pending.begin() + long(used * 2));
    return true;
}

bool Mp3Writer::close(std::string* err)
{
    Impl& d = *d_;
    if (!d.opened) return false;
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        d.removeTmp();
        d.free();
        d.opened = false;
        return false;
    };
    if (!d.pending.empty() && !d.encode(d.pending.data(), int(d.pending.size() / 2))) return fail("mp3 encoding failed");
    d.pending.clear();
    avcodec_send_frame(d.enc, nullptr);
    if (!d.drain()) return fail("mp3 encoding failed");
    int rc = av_write_trailer(d.oc);
    if (rc < 0) return fail("cannot finish the mp3: " + avErr(rc));
    avio_closep(&d.oc->pb);
    d.free();
    d.opened = false;
    std::error_code ec;
    fs::rename(d.tmp, d.path, ec);
    if (ec) return fail("cannot rename to " + d.path.string() + ": " + ec.message());
    return true;
}

void Mp3Writer::abort()
{
    Impl& d = *d_;
    if (!d.opened) return;
    d.free();
    d.removeTmp();
    d.opened = false;
}

} // namespace trakcrate
