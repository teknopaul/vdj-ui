#include "audio_decoder.h"
#include <QFile>
#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace vdj {

namespace {

struct Ffmpeg {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* codec = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    ~Ffmpeg()
    {
        av_frame_free(&frame);
        av_packet_free(&packet);
        swr_free(&swr);
        avcodec_free_context(&codec);
        avformat_close_input(&fmt);
    }
};

QString avError(int rc)
{
    char buf[128];
    av_strerror(rc, buf, sizeof(buf));
    return QString::fromLocal8Bit(buf);
}

// Resample whatever the decoder produced (or flush with in == nullptr) and append it to out.
bool convert(Ffmpeg& f, const AVFrame* in, QVector<qint16>& out)
{
    const int inFrames = in ? in->nb_samples : 0;
    const int maxOut = swr_get_out_samples(f.swr, inFrames);
    if (maxOut <= 0)
        return true;
    const qsizetype old = out.size();
    out.resize(old + qsizetype(maxOut) * DecodedAudio::kChannels);
    uint8_t* dst[1] = { reinterpret_cast<uint8_t*>(out.data() + old) };
    const int got = swr_convert(f.swr, dst, maxOut, in ? const_cast<const uint8_t**>(in->extended_data) : nullptr, inFrames);
    if (got < 0)
        return false;
    out.resize(old + qsizetype(got) * DecodedAudio::kChannels);
    return true;
}

} // namespace

DecodedAudio decodeAudioFile(const QString& path, QString* error)
{
    DecodedAudio result;
    auto fail = [&](const QString& msg) {
        if (error) *error = msg;
        result.samples.clear();
        return result;
    };

    Ffmpeg f;
    const QByteArray name = QFile::encodeName(path);
    int rc = avformat_open_input(&f.fmt, name.constData(), nullptr, nullptr);
    if (rc < 0)
        return fail(QStringLiteral("cannot open %1: %2").arg(path, avError(rc)));
    if ((rc = avformat_find_stream_info(f.fmt, nullptr)) < 0)
        return fail(QStringLiteral("%1: %2").arg(path, avError(rc)));

    const AVCodec* decoder = nullptr;
    const int stream = av_find_best_stream(f.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (stream < 0 || !decoder)
        return fail(QStringLiteral("%1: no audio stream").arg(path));

    f.codec = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(f.codec, f.fmt->streams[stream]->codecpar);
    if ((rc = avcodec_open2(f.codec, decoder, nullptr)) < 0)
        return fail(QStringLiteral("%1: %2").arg(path, avError(rc)));

    AVChannelLayout inLayout = f.codec->ch_layout;
    if (inLayout.nb_channels == 0)
        av_channel_layout_default(&inLayout, 2);
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, DecodedAudio::kChannels);
    rc = swr_alloc_set_opts2(&f.swr, &outLayout, AV_SAMPLE_FMT_S16, DecodedAudio::kSampleRate,
                             &inLayout, f.codec->sample_fmt, f.codec->sample_rate, 0, nullptr);
    if (rc < 0 || swr_init(f.swr) < 0)
        return fail(QStringLiteral("%1: cannot convert the audio format").arg(path));

    f.packet = av_packet_alloc();
    f.frame = av_frame_alloc();
    const double seconds = f.fmt->duration > 0 ? double(f.fmt->duration) / AV_TIME_BASE : 0.0;
    if (seconds > 0)
        result.samples.reserve(qsizetype((seconds + 1.0) * DecodedAudio::kSampleRate * DecodedAudio::kChannels));

    auto drain = [&]() {
        while (avcodec_receive_frame(f.codec, f.frame) == 0) {
            if (!convert(f, f.frame, result.samples))
                return false;
        }
        return true;
    };
    while (av_read_frame(f.fmt, f.packet) >= 0) {
        if (f.packet->stream_index == stream && avcodec_send_packet(f.codec, f.packet) >= 0 && !drain()) {
            av_packet_unref(f.packet);
            return fail(QStringLiteral("%1: decode error").arg(path));
        }
        av_packet_unref(f.packet);
    }
    avcodec_send_packet(f.codec, nullptr);
    drain();
    convert(f, nullptr, result.samples);

    if (result.isEmpty())
        return fail(QStringLiteral("%1: no audio decoded").arg(path));
    return result;
}

} // namespace vdj
