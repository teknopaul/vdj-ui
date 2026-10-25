#include "voice_mixer.h"

#include <algorithm>
#include <cmath>

namespace vdj {

void VoiceMixer::post(const Trigger& t)
{
    if (!t.sample || t.sample->audio.isEmpty())
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(t);
}

void VoiceMixer::stopAll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
    stops_.clear();
    voices_.clear();
}

void VoiceMixer::stopTagAt(int tag, qint64 steadyNs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    stops_.push_back(Stop{ tag, steadyNs });
}

void VoiceMixer::stopTag(int tag)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [tag](const Trigger& t) { return t.tag == tag; }), pending_.end());
    stops_.erase(std::remove_if(stops_.begin(), stops_.end(), [tag](const Stop& st) { return st.tag == tag; }), stops_.end());
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [tag](const Voice& v) { return v.tag == tag; }), voices_.end());
}

void VoiceMixer::setTimingEnabled(bool on)
{
    std::lock_guard<std::mutex> lock(mutex_);
    timingEnabled_ = on;
    if (!on)
        timing_.clear();
}

std::vector<VoiceMixer::TimingSample> VoiceMixer::drainTiming()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TimingSample> out;
    out.swap(timing_);
    return out;
}

void VoiceMixer::setMasterGain(float g)
{
    std::lock_guard<std::mutex> lock(mutex_);
    master_ = std::max(0.0f, g);
}

int VoiceMixer::activeVoices() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return int(voices_.size());
}

int VoiceMixer::pendingTriggers() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return int(pending_.size());
}

qint64 VoiceMixer::droppedLate() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
}

void VoiceMixer::mixVoice(Voice& v, int* acc, int fromFrame, int toFrame)
{
    const qint16* s = v.sample->audio.samples.constData();
    const double frames = double(v.sample->audio.frames());
    const float g = v.gain;
    for (int f = fromFrame; f < toFrame; ++f) {
        if (v.pos >= frames) {
            if (!v.loop)
                return;
            v.pos = std::fmod(v.pos, frames);
        }
        const qint64 i = qint64(v.pos);
        float l, r;
        if (v.ratio == 1.0) {
            l = s[2 * i];
            r = s[2 * i + 1];
        } else {
            const double frac = v.pos - double(i);
            qint64 j = i + 1;
            if (j >= qint64(frames))
                j = v.loop ? 0 : i;
            l = float(s[2 * i] * (1.0 - frac) + s[2 * j] * frac);
            r = float(s[2 * i + 1] * (1.0 - frac) + s[2 * j + 1] * frac);
        }
        acc[2 * f] += int(l * g);
        acc[2 * f + 1] += int(r * g);
        v.pos += v.ratio;
    }
}

void VoiceMixer::render(qint16* out, int frames, qint64 blockStartNs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> acc(size_t(frames) * 2, 0);
    const qint64 blockNs = qint64(double(frames) * 1e9 / kRate);

    // triggers that fall into this block become voices that start at their exact frame
    std::vector<std::pair<int, size_t>> starts;              // frame offset, voice index
    for (size_t i = 0; i < pending_.size();) {
        const Trigger& t = pending_[i];
        if (t.steadyNs >= blockStartNs + blockNs) {
            ++i;
            continue;
        }
        if (t.steadyNs >= blockStartNs - kLateToleranceNs) {
            Voice v;
            v.sample = t.sample;
            v.pos = double(std::max<qint64>(0, t.startFrame));
            v.ratio = t.ratio > 0.0 ? t.ratio : 1.0;
            v.gain = t.gain * master_;
            v.loop = t.loop;
            v.tag = t.tag;
            const int off = t.steadyNs <= blockStartNs ? 0 : std::min(frames - 1, int(double(t.steadyNs - blockStartNs) * kRate / 1e9));
            voices_.push_back(v);
            starts.emplace_back(off, voices_.size() - 1);
            if (timingEnabled_ && timing_.size() < 256)
                timing_.push_back(TimingSample{ t.tag, blockStartNs + qint64(double(off) * 1e9 / kRate) - t.steadyNs });
        } else {
            ++dropped_;
        }
        pending_.erase(pending_.begin() + long(i));
    }

    // stops that fall into this block cut the voices of their tag that started before them
    std::vector<int> endAt(voices_.size(), frames);
    std::vector<char> killed(voices_.size(), 0);
    std::vector<int> startAt(voices_.size(), 0);
    for (const auto& st : starts)
        startAt[st.second] = st.first;
    for (size_t i = 0; i < stops_.size();) {
        const Stop& stop = stops_[i];
        if (stop.ns >= blockStartNs + blockNs) {
            ++i;
            continue;
        }
        const int off = stop.ns <= blockStartNs ? 0 : std::min(frames, int(double(stop.ns - blockStartNs) * kRate / 1e9));
        for (size_t vi = 0; vi < voices_.size(); ++vi) {
            const bool isNew = std::any_of(starts.begin(), starts.end(), [vi](const auto& st) { return st.second == vi; });
            if (voices_[vi].tag != stop.tag || (isNew && startAt[vi] >= off))
                continue;
            endAt[vi] = std::min(endAt[vi], off);
            killed[vi] = 1;
        }
        stops_.erase(stops_.begin() + long(i));
    }

    for (size_t vi = 0; vi < voices_.size(); ++vi)
        if (startAt[vi] < endAt[vi])
            mixVoice(voices_[vi], acc.data(), startAt[vi], endAt[vi]);
    size_t keep = 0;
    for (size_t vi = 0; vi < voices_.size(); ++vi) {
        const Voice& v = voices_[vi];
        if (killed[vi] || (!v.loop && v.pos >= double(v.sample->audio.frames())))
            continue;
        if (keep != vi)
            voices_[keep] = voices_[vi];
        ++keep;
    }
    voices_.resize(keep);

    for (int i = 0; i < frames * 2; ++i)
        out[i] = qint16(std::clamp(acc[size_t(i)], -32768, 32767));
}

} // namespace vdj
