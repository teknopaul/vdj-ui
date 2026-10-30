#include "artwork.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tc_artwork_path(uint32_t id, char *out, size_t outsz)
{
    snprintf(out, outsz, "/PIONEER/Artwork/%05u/a%u.jpg", id / 20 + 1, id);
}

struct membuf {
    const uint8_t *data;
    size_t len, pos;
};

static int mem_read(void *opaque, uint8_t *buf, int size)
{
    struct membuf *m = opaque;
    size_t n = m->len - m->pos;

    if (n == 0) {
        return AVERROR_EOF;
    }
    if (n > (size_t)size) {
        n = (size_t)size;
    }
    memcpy(buf, m->data + m->pos, n);
    m->pos += n;
    return (int)n;
}

static int64_t mem_seek(void *opaque, int64_t off, int whence)
{
    struct membuf *m = opaque;
    int64_t np;

    if (whence == AVSEEK_SIZE) {
        return (int64_t)m->len;
    }
    whence &= ~AVSEEK_FORCE;
    np = whence == SEEK_SET ? off : whence == SEEK_CUR ? (int64_t)m->pos + off : (int64_t)m->len + off;
    if (np < 0 || np > (int64_t)m->len) {
        return -1;
    }
    m->pos = (size_t)np;
    return np;
}

static void seterr(char *err, size_t sz, const char *m)
{
    if (err != NULL && sz > 0) {
        snprintf(err, sz, "%s", m);
    }
}

/* decodes the first picture of an image file in memory to a frame (any pixel format) */
static AVFrame *decode_image(const uint8_t *src, size_t len, char *err, size_t errsz)
{
    struct membuf m = { src, len, 0 };
    AVFormatContext *fmt = NULL;
    AVIOContext *io = NULL;
    AVCodecContext *cc = NULL;
    AVFrame *frame = NULL, *res = NULL;
    AVPacket *pkt = NULL;
    const AVCodec *codec;
    uint8_t *iobuf = av_malloc(4096);
    int i, sidx = -1;

    if (iobuf == NULL) {
        return NULL;
    }
    io = avio_alloc_context(iobuf, 4096, 0, &m, mem_read, NULL, mem_seek);
    fmt = avformat_alloc_context();
    if (io == NULL || fmt == NULL) {
        av_free(iobuf);
        goto out;
    }
    fmt->pb = io;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&fmt, NULL, NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0) {
        seterr(err, errsz, "not an image libavcodec can read");
        goto out;
    }
    for (i = 0; i < (int)fmt->nb_streams; i++) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            sidx = i;
            break;
        }
    }
    if (sidx < 0 || (codec = avcodec_find_decoder(fmt->streams[sidx]->codecpar->codec_id)) == NULL) {
        seterr(err, errsz, "no decoder for this image");
        goto out;
    }
    cc = avcodec_alloc_context3(codec);
    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (cc == NULL || pkt == NULL || frame == NULL || avcodec_parameters_to_context(cc, fmt->streams[sidx]->codecpar) < 0 ||
        avcodec_open2(cc, codec, NULL) < 0) {
        seterr(err, errsz, "cannot open the image decoder");
        goto out;
    }
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == sidx && avcodec_send_packet(cc, pkt) >= 0 && avcodec_receive_frame(cc, frame) >= 0) {
            res = frame;
            frame = NULL;
            av_packet_unref(pkt);
            break;
        }
        av_packet_unref(pkt);
    }
    if (res == NULL) {
        avcodec_send_packet(cc, NULL);
        if (avcodec_receive_frame(cc, frame) >= 0) {
            res = frame;
            frame = NULL;
        } else {
            seterr(err, errsz, "cannot decode the image");
        }
    }
out:
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&cc);
    if (fmt != NULL) {
        avformat_close_input(&fmt);
    }
    if (io != NULL) {
        av_freep(&io->buffer);
        avio_context_free(&io);
    }
    return res;
}

int tc_artwork_jpeg(const uint8_t *src, size_t len, int size, uint8_t **jpeg, size_t *jpeg_len, char *err, size_t errsz)
{
    AVFrame *img = NULL, *rgba = NULL, *small = NULL;
    struct SwsContext *s1 = NULL, *s2 = NULL;
    AVCodecContext *enc = NULL;
    AVPacket *pkt = NULL;
    uint8_t *sq = NULL;
    int w, h, side, x0, y0, y, rc = -1, sq_stride;
    const AVCodec *codec;
    int old_level;

    if (size < 16 || size > 4096) {
        seterr(err, errsz, "bad size");
        return -1;
    }
    /* libswscale warns about the JPEG range pixel format (YUVJ420P), which the MJPEG encoder wants; that is no news to anyone */
    old_level = av_log_get_level();
    av_log_set_level(AV_LOG_ERROR);
    img = decode_image(src, len, err, errsz);
    if (img == NULL) {
        av_log_set_level(old_level);
        return -1;
    }
    w = img->width;
    h = img->height;
    if (w < 1 || h < 1) {
        seterr(err, errsz, "empty image");
        goto out;
    }
    rgba = av_frame_alloc();
    if (rgba == NULL) {
        goto oom;
    }
    rgba->format = AV_PIX_FMT_RGBA;
    rgba->width = w;
    rgba->height = h;
    s1 = sws_getContext(w, h, img->format, w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
    if (s1 == NULL || av_frame_get_buffer(rgba, 0) < 0) {
        seterr(err, errsz, "cannot convert the image");
        goto out;
    }
    sws_scale(s1, (const uint8_t *const *)img->data, img->linesize, 0, h, rgba->data, rgba->linesize);

    /* centre crop to a square, alpha composed over white, as packed RGB24 */
    side = w < h ? w : h;
    x0 = (w - side) / 2;
    y0 = (h - side) / 2;
    sq_stride = side * 3;
    sq = malloc((size_t)sq_stride * (size_t)side);
    if (sq == NULL) {
        goto oom;
    }
    for (y = 0; y < side; y++) {
        const uint8_t *in = rgba->data[0] + (size_t)(y0 + y) * (size_t)rgba->linesize[0] + (size_t)x0 * 4;
        uint8_t *o = sq + (size_t)y * (size_t)sq_stride;
        int x;

        for (x = 0; x < side; x++, in += 4, o += 3) {
            unsigned a = in[3];

            o[0] = (uint8_t)((in[0] * a + 255 * (255 - a) + 127) / 255);
            o[1] = (uint8_t)((in[1] * a + 255 * (255 - a) + 127) / 255);
            o[2] = (uint8_t)((in[2] * a + 255 * (255 - a) + 127) / 255);
        }
    }
    small = av_frame_alloc();
    if (small == NULL) {
        goto oom;
    }
    small->format = AV_PIX_FMT_YUVJ420P;
    small->width = size;
    small->height = size;
    s2 = sws_getContext(side, side, AV_PIX_FMT_RGB24, size, size, AV_PIX_FMT_YUVJ420P, SWS_AREA, NULL, NULL, NULL);
    if (s2 == NULL || av_frame_get_buffer(small, 0) < 0) {
        seterr(err, errsz, "cannot scale the image");
        goto out;
    }
    {
        const uint8_t *sd[4] = { sq, NULL, NULL, NULL };
        int sl[4] = { sq_stride, 0, 0, 0 };

        sws_scale(s2, sd, sl, 0, side, small->data, small->linesize);
    }

    codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    enc = codec != NULL ? avcodec_alloc_context3(codec) : NULL;
    pkt = av_packet_alloc();
    if (enc == NULL || pkt == NULL) {
        seterr(err, errsz, "no JPEG encoder in libavcodec");
        goto out;
    }
    enc->pix_fmt = AV_PIX_FMT_YUVJ420P;
    enc->width = size;
    enc->height = size;
    enc->time_base = (AVRational){ 1, 25 };
    enc->color_range = AVCOL_RANGE_JPEG;
    enc->flags |= AV_CODEC_FLAG_QSCALE;
    enc->global_quality = FF_QP2LAMBDA * 3;
    small->quality = enc->global_quality;
    small->pts = 0;
    if (avcodec_open2(enc, codec, NULL) < 0 || avcodec_send_frame(enc, small) < 0 || avcodec_send_frame(enc, NULL) < 0 ||
        avcodec_receive_packet(enc, pkt) < 0) {
        seterr(err, errsz, "cannot encode the JPEG");
        goto out;
    }
    *jpeg = malloc((size_t)pkt->size);
    if (*jpeg == NULL) {
        goto oom;
    }
    memcpy(*jpeg, pkt->data, (size_t)pkt->size);
    *jpeg_len = (size_t)pkt->size;
    rc = 0;
    goto out;
oom:
    seterr(err, errsz, "out of memory");
out:
    av_packet_free(&pkt);
    avcodec_free_context(&enc);
    sws_freeContext(s1);
    sws_freeContext(s2);
    av_frame_free(&img);
    av_frame_free(&rgba);
    av_frame_free(&small);
    free(sq);
    av_log_set_level(old_level);
    return rc;
}
