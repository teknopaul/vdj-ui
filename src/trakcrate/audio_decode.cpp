#include "audio_decode.h"

#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace trakcrate {

namespace {

struct Decoder {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* ctx = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;
    ~Decoder()
    {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        swr_free(&swr);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
    }
};

std::string avError(int rc)
{
    char buf[128];
    av_strerror(rc, buf, sizeof buf);
    return buf;
}

// Converts one decoded frame (or the resampler's tail when frame is null) and appends it to out.
bool convert(SwrContext* swr, const AVFrame* frame, std::vector<float>* out)
{
    int inSamples = frame ? frame->nb_samples : 0;
    int cap = swr_get_out_samples(swr, inSamples);
    if (cap <= 0) return true;
    size_t at = out->size();
    out->resize(at + size_t(cap) * 2);
    uint8_t* dst[1] = {reinterpret_cast<uint8_t*>(out->data() + at)};
    int n = swr_convert(swr, dst, cap, frame ? const_cast<const uint8_t**>(frame->extended_data) : nullptr, inSamples);
    if (n < 0) return false;
    out->resize(at + size_t(n) * 2);
    return true;
}

} // namespace

bool decodeAudio(const fs::path& path, DecodedPcm* out, std::string* err, const std::atomic<bool>* cancel)
{
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    Decoder d;
    int rc = avformat_open_input(&d.fmt, path.c_str(), nullptr, nullptr);
    if (rc < 0) return fail("cannot open: " + avError(rc));
    if (avformat_find_stream_info(d.fmt, nullptr) < 0) return fail("cannot read stream information");
    const AVCodec* codec = nullptr;
    int idx = av_find_best_stream(d.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (idx < 0 || !codec) return fail("no audio stream");
    d.ctx = avcodec_alloc_context3(codec);
    if (!d.ctx || avcodec_parameters_to_context(d.ctx, d.fmt->streams[idx]->codecpar) < 0) return fail("cannot set up the decoder");
    if ((rc = avcodec_open2(d.ctx, codec, nullptr)) < 0) return fail("cannot open the decoder: " + avError(rc));

    AVChannelLayout in = {};
    if (d.ctx->ch_layout.nb_channels > 0 && d.ctx->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_copy(&in, &d.ctx->ch_layout);
    else av_channel_layout_default(&in, d.ctx->ch_layout.nb_channels > 0 ? d.ctx->ch_layout.nb_channels : 2);
    AVChannelLayout stereo = {};
    av_channel_layout_default(&stereo, 2);
    rc = swr_alloc_set_opts2(&d.swr, &stereo, AV_SAMPLE_FMT_FLT, DecodedPcm::kSampleRate, &in, d.ctx->sample_fmt, d.ctx->sample_rate, 0, nullptr);
    const bool monoInput = in.nb_channels == 1;
    av_channel_layout_uninit(&in);
    av_channel_layout_uninit(&stereo);
    if (rc < 0) return fail("cannot set up the resampler");
    // swresample upmixes mono (the centre channel) to stereo at -3 dB (a fixed 1/sqrt(2)); rematrix_volume is a linear gain, so give that back so a mono file keeps its level,
    // or it would look and measure 3 dB quieter than it is
    if (monoInput) av_opt_set_double(d.swr, "rematrix_volume", M_SQRT2, 0);
    if (swr_init(d.swr) < 0) return fail("cannot set up the resampler");

    d.pkt = av_packet_alloc();
    d.frame = av_frame_alloc();
    if (!d.pkt || !d.frame) return fail("out of memory");

    DecodedPcm pcm;
    bool flushing = false;
    for (;;) {
        if (cancel && cancel->load()) return fail("cancelled");
        if (!flushing) {
            rc = av_read_frame(d.fmt, d.pkt);
            if (rc < 0) {
                flushing = true;
                avcodec_send_packet(d.ctx, nullptr);
            } else {
                if (d.pkt->stream_index == idx) avcodec_send_packet(d.ctx, d.pkt);   // a bad packet is skipped, not fatal
                av_packet_unref(d.pkt);
            }
        }
        for (;;) {
            rc = avcodec_receive_frame(d.ctx, d.frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) break;
            if (rc < 0) return fail("decode error: " + avError(rc));
            bool ok = convert(d.swr, d.frame, &pcm.stereo);
            av_frame_unref(d.frame);
            if (!ok) return fail("resampling failed");
        }
        if (flushing) break;
    }
    if (!convert(d.swr, nullptr, &pcm.stereo)) return fail("resampling failed");     // the resampler's delayed tail
    if (pcm.stereo.empty()) return fail("no audio decoded");
    *out = std::move(pcm);
    return true;
}

} // namespace trakcrate
