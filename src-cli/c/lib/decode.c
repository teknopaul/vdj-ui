#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

#include "decode.h"

static void av_message(char *err, size_t errsz, const char *what, int rc)
{
    char buf[128];

    if (rc < 0) {
        av_strerror(rc, buf, sizeof buf);
        snprintf(err, errsz, "%s: %s", what, buf);
    } else {
        snprintf(err, errsz, "%s", what);
    }
}

/* converts one decoded frame (or the resampler's tail when frame is NULL) and appends it; -1 on error */
static int convert(SwrContext *swr, const AVFrame *frame, struct tc_pcm *pcm, size_t *cap)
{
    int in = frame ? frame->nb_samples : 0;
    int room = swr_get_out_samples(swr, in), n;
    uint8_t *dst[1];

    if (room <= 0) {
        return 0;
    }
    if ((pcm->frames + (size_t)room) * 2 > *cap) {
        size_t ncap = *cap ? *cap : 1u << 20;
        float *ns;
        while (ncap < (pcm->frames + (size_t)room) * 2) {
            ncap *= 2;
        }
        ns = realloc(pcm->stereo, ncap * sizeof(float));
        if (ns == NULL) {
            return -1;
        }
        pcm->stereo = ns;
        *cap = ncap;
    }
    dst[0] = (uint8_t *)(pcm->stereo + pcm->frames * 2);
    n = swr_convert(swr, dst, room, frame ? (const uint8_t **)frame->extended_data : NULL, in);
    if (n < 0) {
        return -1;
    }
    pcm->frames += (size_t)n;
    return 0;
}

int tc_decode(const char *path, struct tc_pcm *out, char *err, size_t errsz)
{
    AVFormatContext *fmt = NULL;
    AVCodecContext *ctx = NULL;
    SwrContext *swr = NULL;
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;
    const AVCodec *codec = NULL;
    AVChannelLayout in = { 0 }, stereo = { 0 };
    struct tc_pcm pcm = { NULL, 0, TC_SAMPLE_RATE };
    size_t cap = 0;
    int rc, idx, mono, flushing = 0, result = -1;

    av_log_set_level(AV_LOG_QUIET);
    rc = avformat_open_input(&fmt, path, NULL, NULL);
    if (rc < 0) {
        av_message(err, errsz, "cannot open", rc);
        return -1;
    }
    if (avformat_find_stream_info(fmt, NULL) < 0) {
        av_message(err, errsz, "cannot read stream information", 0);
        goto done;
    }
    idx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (idx < 0 || codec == NULL) {
        av_message(err, errsz, "no audio stream", 0);
        goto done;
    }
    ctx = avcodec_alloc_context3(codec);
    if (ctx == NULL || avcodec_parameters_to_context(ctx, fmt->streams[idx]->codecpar) < 0) {
        av_message(err, errsz, "cannot set up the decoder", 0);
        goto done;
    }
    rc = avcodec_open2(ctx, codec, NULL);
    if (rc < 0) {
        av_message(err, errsz, "cannot open the decoder", rc);
        goto done;
    }

    if (ctx->ch_layout.nb_channels > 0 && ctx->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC) {
        av_channel_layout_copy(&in, &ctx->ch_layout);
    } else {
        av_channel_layout_default(&in, ctx->ch_layout.nb_channels > 0 ? ctx->ch_layout.nb_channels : 2);
    }
    av_channel_layout_default(&stereo, 2);
    rc = swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, TC_SAMPLE_RATE, &in, ctx->sample_fmt, ctx->sample_rate, 0, NULL);
    mono = in.nb_channels == 1;
    av_channel_layout_uninit(&in);
    av_channel_layout_uninit(&stereo);
    if (rc < 0) {
        av_message(err, errsz, "cannot set up the resampler", 0);
        goto done;
    }
    /* swresample upmixes mono at -3 dB; give the level back so a mono file measures as loud as it is */
    if (mono) {
        av_opt_set_double(swr, "rematrix_volume", M_SQRT2, 0);
    }
    if (swr_init(swr) < 0) {
        av_message(err, errsz, "cannot set up the resampler", 0);
        goto done;
    }
    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (pkt == NULL || frame == NULL) {
        av_message(err, errsz, "out of memory", 0);
        goto done;
    }

    for (;;) {
        if (!flushing) {
            rc = av_read_frame(fmt, pkt);
            if (rc < 0) {
                flushing = 1;
                avcodec_send_packet(ctx, NULL);
            } else {
                if (pkt->stream_index == idx) {
                    avcodec_send_packet(ctx, pkt);      /* a bad packet is skipped, not fatal */
                }
                av_packet_unref(pkt);
            }
        }
        for (;;) {
            rc = avcodec_receive_frame(ctx, frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
                break;
            }
            if (rc < 0) {
                av_message(err, errsz, "decode error", rc);
                goto done;
            }
            if (convert(swr, frame, &pcm, &cap) != 0) {
                av_message(err, errsz, "resampling failed", 0);
                goto done;
            }
            av_frame_unref(frame);
        }
        if (flushing) {
            break;
        }
    }
    if (convert(swr, NULL, &pcm, &cap) != 0) {     /* the resampler's delayed tail */
        av_message(err, errsz, "resampling failed", 0);
        goto done;
    }
    if (pcm.frames == 0) {
        av_message(err, errsz, "no audio decoded", 0);
        goto done;
    }
    *out = pcm;
    pcm.stereo = NULL;
    result = 0;

done:
    free(pcm.stereo);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    return result;
}
