#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <QString>
#include <QVector>

namespace vdj {

// A whole track decoded to 16 bit interleaved stereo at kSampleRate.
struct DecodedAudio {
    static constexpr int kSampleRate = 44100;
    static constexpr int kChannels = 2;

    QVector<qint16> samples;                 // interleaved L R L R ...
    qint64 frames() const { return samples.size() / kChannels; }
    double lengthMs() const { return double(frames()) * 1000.0 / kSampleRate; }
    bool isEmpty() const { return samples.isEmpty(); }
};

// Decode any file ffmpeg understands (mp3, m4a, flac, wav, aiff ...). Returns an empty result and sets *error on failure.
DecodedAudio decodeAudioFile(const QString& path, QString* error = nullptr);

} // namespace vdj

#endif // AUDIO_DECODER_H
