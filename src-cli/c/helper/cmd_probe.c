/*
 * probe FILE [--artwork-dir DIR]: what is in an audio file, without decoding it (ffmpeg's libavformat).
 *
 * Prints meta.txt style lines on stdout: container, duration_ms, sample_rate, channels, bit_depth, bitrate,
 * then the tags that are known keys of meta.txt (title, artist, album, genre, label, year, track_number,
 * comment, bpm_tag). With --artwork-dir the embedded cover is written to DIR/artwork.jpg or .png (atomically)
 * and "artwork: <file name>" is printed. Exit 1 with a message when the file is not usable audio.
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>

#include "../lib/kv.h"
#include "../lib/status.h"

static const char *tag(const AVFormatContext *fmt, const AVStream *st, const char *name)
{
    const AVDictionaryEntry *e = av_dict_get(fmt->metadata, name, NULL, AV_DICT_IGNORE_SUFFIX | AV_DICT_MATCH_CASE);

    if (e == NULL) {
        e = av_dict_get(fmt->metadata, name, NULL, AV_DICT_IGNORE_SUFFIX);
    }
    if (e == NULL && st != NULL) {
        e = av_dict_get(st->metadata, name, NULL, AV_DICT_IGNORE_SUFFIX);
    }
    return (e != NULL && e->value[0] != '\0') ? e->value : NULL;
}

/* leading digits of "7/12" or "2019-03-01" */
static void print_leading_int(const char *key, const char *value)
{
    long n;
    char *end;

    n = strtol(value, &end, 10);
    if (end != value && n >= 0) {
        printf("%s: %ld\n", key, n);
    }
}

static int write_artwork(const char *dir, const AVPacket *pic, int is_png)
{
    char path[4096], tmp[4096];
    const char *name = is_png ? "artwork.png" : "artwork.jpg";
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    snprintf(tmp, sizeof tmp, "%s/.%s.tmp.%d", dir, name, (int)getpid());
    f = fopen(tmp, "wb");
    if (f == NULL) {
        fprintf(stderr, "trakcrate-helper probe: cannot write %s: %s\n", tmp, strerror(errno));
        return -1;
    }
    if (fwrite(pic->data, 1, (size_t)pic->size, f) != (size_t)pic->size || fclose(f) != 0) {
        unlink(tmp);
        fprintf(stderr, "trakcrate-helper probe: cannot write %s\n", tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    printf("artwork: %s\n", name);
    return 0;
}

int cmd_probe(int argc, char **argv)
{
    const char *file = NULL, *artdir = NULL;
    AVFormatContext *fmt = NULL;
    const AVStream *st;
    const AVCodecParameters *cp;
    const char *v;
    char container[64];
    char *comma;
    int i, audio, rc, bits;
    long long duration_ms = 0;
    long bitrate;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--artwork-dir") == 0 && i + 1 < argc) {
            artdir = argv[++i];
        } else if (argv[i][0] == '-' || file != NULL) {
            fprintf(stderr, "usage: trakcrate-helper probe FILE [--artwork-dir DIR]\n");
            return TC_USAGE;
        } else {
            file = argv[i];
        }
    }
    if (file == NULL) {
        fprintf(stderr, "usage: trakcrate-helper probe FILE [--artwork-dir DIR]\n");
        return TC_USAGE;
    }

    av_log_set_level(AV_LOG_QUIET);
    rc = avformat_open_input(&fmt, file, NULL, NULL);
    if (rc < 0) {
        char buf[128];
        av_strerror(rc, buf, sizeof buf);
        fprintf(stderr, "trakcrate-helper probe: %s: cannot open: %s\n", file, buf);
        return TC_FAIL;
    }
    if (avformat_find_stream_info(fmt, NULL) < 0) {
        fprintf(stderr, "trakcrate-helper probe: %s: cannot read stream information\n", file);
        avformat_close_input(&fmt);
        return TC_FAIL;
    }
    audio = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio < 0) {
        fprintf(stderr, "trakcrate-helper probe: %s: no audio stream\n", file);
        avformat_close_input(&fmt);
        return TC_FAIL;
    }
    st = fmt->streams[audio];
    cp = st->codecpar;
    if (cp->sample_rate <= 0 || cp->ch_layout.nb_channels <= 0) {
        fprintf(stderr, "trakcrate-helper probe: %s: not usable audio (no sample rate or channels)\n", file);
        avformat_close_input(&fmt);
        return TC_FAIL;
    }

    snprintf(container, sizeof container, "%s", fmt->iformat && fmt->iformat->name ? fmt->iformat->name : "");
    comma = strchr(container, ',');
    if (comma != NULL) {
        *comma = '\0';
    }
    if (fmt->duration > 0) {
        duration_ms = fmt->duration / (AV_TIME_BASE / 1000);
    } else if (st->duration > 0) {
        duration_ms = (long long)(st->duration * av_q2d(st->time_base) * 1000.0);
    }
    bits = av_get_bits_per_sample(cp->codec_id);
    if (bits <= 0) {
        bits = cp->bits_per_raw_sample > 0 ? cp->bits_per_raw_sample : 16;
    }
    bitrate = (long)(cp->bit_rate > 0 ? cp->bit_rate : fmt->bit_rate);

    tc_kv_print(stdout, "container", container);
    printf("duration_ms: %lld\n", duration_ms);
    printf("sample_rate: %d\n", cp->sample_rate);
    printf("channels: %d\n", cp->ch_layout.nb_channels);
    printf("bit_depth: %d\n", bits);
    printf("bitrate: %ld\n", bitrate);

    if ((v = tag(fmt, st, "title")) != NULL) tc_kv_print(stdout, "title", v);
    if ((v = tag(fmt, st, "artist")) != NULL) tc_kv_print(stdout, "artist", v);
    if ((v = tag(fmt, st, "album")) != NULL) tc_kv_print(stdout, "album", v);
    if ((v = tag(fmt, st, "genre")) != NULL) tc_kv_print(stdout, "genre", v);
    if ((v = tag(fmt, st, "label")) != NULL || (v = tag(fmt, st, "publisher")) != NULL) tc_kv_print(stdout, "label", v);
    if ((v = tag(fmt, st, "comment")) != NULL) tc_kv_print(stdout, "comment", v);
    if ((v = tag(fmt, st, "date")) != NULL || (v = tag(fmt, st, "year")) != NULL) print_leading_int("year", v);
    if ((v = tag(fmt, st, "track")) != NULL) print_leading_int("track_number", v);
    if ((v = tag(fmt, st, "tbpm")) != NULL || (v = tag(fmt, st, "bpm")) != NULL) {
        char *end;
        double bpm = strtod(v, &end);
        if (end != v && bpm > 0) {
            printf("bpm_tag: %g\n", bpm);
        }
    }

    if (artdir != NULL) {
        for (i = 0; i < (int)fmt->nb_streams; i++) {
            const AVStream *s = fmt->streams[i];
            if ((s->disposition & AV_DISPOSITION_ATTACHED_PIC) && s->attached_pic.size > 0) {
                if (write_artwork(artdir, &s->attached_pic, s->codecpar->codec_id == AV_CODEC_ID_PNG) != 0) {
                    avformat_close_input(&fmt);
                    return TC_FAIL;
                }
                break;
            }
        }
    }
    avformat_close_input(&fmt);
    return TC_OK;
}
