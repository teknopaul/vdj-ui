#include "time_stretcher.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vdj {

TimeStretcher::TimeStretcher()
    : window_(2 * kHop), overlap_(size_t(kHop) * kChannels, 0.0f)
{
    for (int i = 0; i < 2 * kHop; ++i)
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * float(M_PI) * float(i) / float(2 * kHop)));
}

void TimeStretcher::setSource(const qint16* interleaved, qint64 frames)
{
    src_ = interleaved;
    frames_ = frames;
    seek(0);
}

void TimeStretcher::setTempo(double tempo)
{
    tempo = std::clamp(tempo, kMinTempo, kMaxTempo);
    if (tempo == tempo_)
        return;
    const double heard = position();             // restart from what is being heard, the buffered output is stale
    tempo_ = tempo;
    if (src_)
        seek(heard);
}

void TimeStretcher::seek(double frame)
{
    // keep what is already heard consistent: start fresh from the new position
    pos_ = std::clamp(frame, 0.0, double(frames_));
    lastStart_ = -1;
    std::fill(overlap_.begin(), overlap_.end(), 0.0f);
    fifoFill_ = 0;
    fifoRead_ = 0;
}

double TimeStretcher::position() const
{
    if (tempo_ == 1.0)
        return pos_;
    // the source position of the next window runs ahead of what is heard by the buffered output
    return std::max(0.0, pos_ - double(fifoFill_ - fifoRead_) * tempo_ - double(kHop) * tempo_);
}

float TimeStretcher::sample(qint64 frame, int ch) const
{
    if (frame < 0 || frame >= frames_)
        return 0.0f;
    return float(src_[frame * kChannels + ch]) * (1.0f / 32768.0f);
}

// Find the offset (within +- kSearch) at which the window starting near `nominal` best continues the previous one:
// the previous window was taken from lastStart_, so its natural continuation starts at lastStart_ + kHop.
int TimeStretcher::bestOffset(qint64 nominal) const
{
    if (lastStart_ < 0)
        return 0;
    const qint64 ref = lastStart_ + kHop;
    constexpr int kStep = 4;                     // correlate on every 4th frame of a mono mix: cheap and good enough
    double best = -1e30;
    int bestD = 0;
    for (int d = -kSearch; d <= kSearch; d += 2) {
        const qint64 start = nominal + d;
        if (start < 0)
            continue;
        double sum = 0.0, energy = 1e-9;
        for (int i = 0; i < kHop; i += kStep) {
            const float a = sample(ref + i, 0) + sample(ref + i, 1);
            const float b = sample(start + i, 0) + sample(start + i, 1);
            sum += double(a) * b;
            energy += double(b) * b;
        }
        const double score = sum / std::sqrt(energy);
        if (score > best) {
            best = score;
            bestD = d;
        }
    }
    return bestD;
}

void TimeStretcher::produceBlock()
{
    const qint64 nominal = qint64(pos_);
    const int d = bestOffset(nominal);
    const qint64 start = std::max<qint64>(0, nominal + d);

    // make room in the fifo
    if (fifoRead_ > 0 && fifoRead_ == fifoFill_) {
        fifoRead_ = fifoFill_ = 0;
    } else if (fifoRead_ > 65536) {
        std::memmove(fifo_.data(), fifo_.data() + fifoRead_ * kChannels, size_t(fifoFill_ - fifoRead_) * kChannels * sizeof(float));
        fifoFill_ -= fifoRead_;
        fifoRead_ = 0;
    }
    if (size_t((fifoFill_ + kHop) * kChannels) > fifo_.size())
        fifo_.resize(size_t((fifoFill_ + kHop) * kChannels) + 16384);

    // overlap-add: previous tail + the first half of the new window; keep the second half for next time
    float* out = fifo_.data() + fifoFill_ * kChannels;
    for (int i = 0; i < kHop; ++i) {
        for (int ch = 0; ch < kChannels; ++ch) {
            out[i * kChannels + ch] = overlap_[size_t(i) * kChannels + ch] + sample(start + i, ch) * window_[i];
            overlap_[size_t(i) * kChannels + ch] = sample(start + kHop + i, ch) * window_[kHop + i];
        }
    }
    fifoFill_ += kHop;
    lastStart_ = start;
    pos_ += kHop * tempo_;
}

void TimeStretcher::render(qint16* out, int frames)
{
    if (!src_ || frames_ == 0) {
        std::memset(out, 0, size_t(frames) * kChannels * sizeof(qint16));
        return;
    }
    if (tempo_ == 1.0) {
        // straight copy, silence past the end
        for (int i = 0; i < frames; ++i) {
            const qint64 f = qint64(pos_) + i;
            for (int ch = 0; ch < kChannels; ++ch)
                out[i * kChannels + ch] = f < frames_ ? src_[f * kChannels + ch] : qint16(0);
        }
        pos_ = std::min(pos_ + frames, double(frames_));
        fifoFill_ = fifoRead_ = 0;
        lastStart_ = -1;
        return;
    }
    int done = 0;
    while (done < frames) {
        if (fifoRead_ == fifoFill_) {
            if (pos_ >= double(frames_)) {
                std::memset(out + done * kChannels, 0, size_t(frames - done) * kChannels * sizeof(qint16));
                return;
            }
            produceBlock();
        }
        const int n = int(std::min<qint64>(frames - done, fifoFill_ - fifoRead_));
        const float* in = fifo_.data() + fifoRead_ * kChannels;
        for (int i = 0; i < n * kChannels; ++i)
            out[done * kChannels + i] = qint16(std::clamp(in[i] * 32768.0f, -32768.0f, 32767.0f));
        fifoRead_ += n;
        done += n;
    }
}

} // namespace vdj
