#ifndef TIME_STRETCHER_H
#define TIME_STRETCHER_H

#include <QtGlobal>
#include <vector>

namespace vdj {

// Pitch-preserving tempo change (WSOLA) over a stereo 16 bit source held in memory. At tempo 1.0 the source is
// copied through untouched. Not thread safe: callers serialise access.
class TimeStretcher {
public:
    static constexpr int kChannels = 2;
    static constexpr double kMinTempo = 0.5;
    static constexpr double kMaxTempo = 2.0;

    TimeStretcher();

    void setSource(const qint16* interleaved, qint64 frames);
    void setTempo(double tempo);                 // 1.0 = original speed; clamped to kMinTempo..kMaxTempo
    double tempo() const { return tempo_; }

    void seek(double frame);                     // jump to a source position, dropping buffered output
    double position() const;                     // source frame currently being heard (fractional)
    bool atEnd() const { return pos_ >= double(frames_) && fifoFill_ == 0; }

    // Fill out with exactly `frames` stereo frames (silence past the end of the source).
    void render(qint16* out, int frames);

private:
    static constexpr int kHop = 1024;            // output hop; the window is 2 * kHop
    static constexpr int kSearch = 441;          // +- frames searched for the best splice point (10 ms)

    float sample(qint64 frame, int ch) const;
    int bestOffset(qint64 nominal) const;
    void produceBlock();

    const qint16* src_ = nullptr;
    qint64 frames_ = 0;
    double tempo_ = 1.0;
    double pos_ = 0.0;                           // nominal source position of the next analysis window
    qint64 lastStart_ = -1;                      // where the previous window was taken from, -1 after a seek
    std::vector<float> window_;                  // Hann, 2 * kHop
    std::vector<float> overlap_;                 // kHop frames of the previous window's tail, windowed
    std::vector<float> fifo_;                    // interleaved output waiting to be played
    qint64 fifoFill_ = 0;                        // frames in fifo_
    qint64 fifoRead_ = 0;
};

} // namespace vdj

#endif // TIME_STRETCHER_H
