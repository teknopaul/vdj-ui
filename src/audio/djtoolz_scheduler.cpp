#include "djtoolz_scheduler.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace vdj {

DjtoolzScheduler::DjtoolzScheduler(BeatSource* source, SampleCache* cache, Sink sink)
    : source_(source)
    , cache_(cache)
    , sink_(std::move(sink))
{
    loader_ = std::thread([this] { loaderLoop(); });
}

DjtoolzScheduler::~DjtoolzScheduler()
{
    stop();
    quit_ = true;
    loadWake_.notify_all();
    if (loader_.joinable())
        loader_.join();
}

void DjtoolzScheduler::addTool(std::shared_ptr<DjtoolzTool> m)
{
    std::lock_guard<std::mutex> lock(mutex_);
    tools_.push_back(std::move(m));
}

void DjtoolzScheduler::removeTool(int id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    tools_.erase(std::remove_if(tools_.begin(), tools_.end(), [id](const auto& m) { return m->id() == id; }), tools_.end());
    states_.erase(id);
}

std::vector<std::shared_ptr<DjtoolzTool>> DjtoolzScheduler::tools() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tools_;
}

std::shared_ptr<DjtoolzTool> DjtoolzScheduler::tool(int id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& m : tools_)
        if (m->id() == id)
            return m;
    return nullptr;
}

void DjtoolzScheduler::start()
{
    if (running_.exchange(true))
        return;
    thread_ = std::thread([this] { loopBody(); });
}

void DjtoolzScheduler::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

void DjtoolzScheduler::loopBody()
{
    while (running_) {
        tick(BeatSource::steadyNowNs());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

double DjtoolzScheduler::currentBeat() const
{
    return source_->running() ? source_->beatsAtSteadyNs(BeatSource::steadyNowNs()) : -1.0;
}

void DjtoolzScheduler::fireOneBar(int toolId)
{
    const double beat = currentBeat();
    if (beat < 0.0)
        return;
    if (auto m = tool(toolId))
        m->fireOneBar(beat);
}

void DjtoolzScheduler::requestLoad(const QString& path)
{
    {
        std::lock_guard<std::mutex> lock(loadMutex_);
        if (loadQueue_.count(path) || loading_.count(path) || failed_.count(path))
            return;
        loadQueue_.insert(path);
    }
    loadWake_.notify_all();
}

void DjtoolzScheduler::requestStretch(const QString& path, int permille, qint64 exactFrames)
{
    {
        std::lock_guard<std::mutex> lock(loadMutex_);
        auto it = stretchQueue_.find(path);
        if (it != stretchQueue_.end() && it->second.permille == permille)
            return;
        stretchQueue_[path] = StretchJob{ permille, exactFrames };
    }
    loadWake_.notify_all();
}

void DjtoolzScheduler::loaderLoop()
{
    std::unique_lock<std::mutex> lock(loadMutex_);
    while (!quit_) {
        if (loadQueue_.empty() && !stretchQueue_.empty()) {
            const auto it = stretchQueue_.begin();
            const QString path = it->first;
            const StretchJob job = it->second;
            stretchQueue_.erase(it);
            stretching_.insert(path);
            lock.unlock();
            if (auto base = cache_->peek(path))
                stretch_.get(*base, job.permille, job.exactFrames);
            lock.lock();
            stretching_.erase(path);
            continue;
        }
        if (loadQueue_.empty()) {
            loadWake_.wait_for(lock, std::chrono::milliseconds(200));
            continue;
        }
        const QString path = *loadQueue_.begin();
        loadQueue_.erase(loadQueue_.begin());
        loading_.insert(path);
        lock.unlock();
        const bool ok = cache_->get(path) != nullptr;
        lock.lock();
        loading_.erase(path);
        if (!ok)
            failed_.insert(path);                    // do not retry every tick
    }
}

bool DjtoolzScheduler::waitIdle(int timeoutMs)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        {
            std::lock_guard<std::mutex> lock(loadMutex_);
            if (loadQueue_.empty() && loading_.empty() && stretchQueue_.empty() && stretching_.empty())
                return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

// Where the next window of hits starts: where the last one ended, or now for a tool that has just become active; a sequencer
// that restarted (its beat count went back) cancels a one bar in progress and starts over from now.
double DjtoolzScheduler::windowStart(const State& st, double nowBeat, double lookaheadBeats, DjtoolzTool* m)
{
    if (!st.active)
        return nowBeat;
    if (nowBeat + lookaheadBeats + 1.0 < st.scheduledTo) {
        m->cancelOneBar();
        return nowBeat;
    }
    return std::max(st.scheduledTo, nowBeat);
}

// DR-6S: every hit belongs to a voice with its own sample, volume, offset and choke.
void DjtoolzScheduler::tickDrums(const std::shared_ptr<DjtoolzTool>& m, const ToolSettings& s, double nowBeat, double bpm)
{
    State st;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        st = states_[m->id()];
    }
    const double lookaheadBeats = kLookaheadMs * bpm / 60000.0;
    const double from = windowStart(st, nowBeat, lookaheadBeats, m.get());
    const double to = nowBeat + lookaheadBeats;
    std::vector<PlannedHit> hits;
    if (to > from)
        m->plan(from, to, hits);
    for (const PlannedHit& h : hits) {
        if (h.voice < 0 || h.voice >= kDrumVoices)
            continue;
        const DrumVoice& v = s.drums[h.voice];
        if (v.muted || v.samplePath.isEmpty() || v.volume <= 0 || s.volume <= 0)
            continue;
        auto sample = cache_->peek(v.samplePath);
        if (!sample) {
            requestLoad(v.samplePath);               // this hit is skipped, the next ones find it decoded
            continue;
        }
        Trigger t;
        t.sample = sample;
        t.steadyNs = source_->steadyNsAtBeat(h.beat) + qint64(v.offsetMs * 1e6);
        t.gain = s.gain * float(s.volume) / float(DjtoolzTool::kMaxVolume) * float(v.volume) / float(DjtoolzTool::kMaxVolume) * h.level;
        t.tag = m->id();
        if (v.chokeSteps > 0) {
            // the voice is cut that many quarter beats after it starts, with a fade of up to 100 ms before the cut
            t.cutAfterFrames = qint64(std::llround(double(v.chokeSteps) * (60.0 / bpm) / 4.0 * DecodedAudio::kSampleRate));
            t.fadeFrames = DecodedAudio::kSampleRate / 10;
        }
        sink_(t);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    State& ns = states_[m->id()];
    ns.active = true;
    ns.scheduledTo = std::max(to, from);
}

// MidiMatic: the same window logic as the samples, but the result is queued note events, no sample is involved.
void DjtoolzScheduler::tickMidi(const std::shared_ptr<DjtoolzTool>& m, const ToolSettings& s, bool oneBar,
                                double nowBeat, double bpm)
{
    Q_UNUSED(oneBar);
    State st;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        st = states_[m->id()];
    }
    const double lookaheadBeats = kLookaheadMs * bpm / 60000.0;
    const double from = windowStart(st, nowBeat, lookaheadBeats, m.get());
    const double to = nowBeat + lookaheadBeats;
    std::vector<PlannedHit> hits;
    if (to > from)
        m->plan(from, to, hits);
    if (midiSink_) {
        for (const PlannedHit& h : hits)
            for (const MidiNoteEvent& e : buildNoteEvents(h.beat, s, bpm, m->id()))
                midiSink_(e);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    State& ns = states_[m->id()];
    ns.active = true;
    ns.scheduledTo = std::max(to, from);
}

// The copy of an acid loop to play now: stretched so it is exactly 4 beats at `bpm`. Null while the first copy is being made.
std::shared_ptr<const Sample> DjtoolzScheduler::acidSample(const ToolSettings& s, const std::shared_ptr<const Sample>& base,
                                                           double bpm, State& st)
{
    const double barSec = 4.0 * 60.0 / bpm;
    const qint64 barFrames = qint64(std::llround(barSec * DecodedAudio::kSampleRate));
    const double srcBpm = base->bpm > 0 ? base->bpm : s.manualBpm;
    // the loop's BPM when known, else assume it is exactly 4 beats long
    const double tempo = srcBpm > 0 ? bpm / srcBpm : double(base->audio.frames()) / double(barFrames);
    const int permille = tempoPermille(tempo);
    if (auto hit = stretch_.peek(base->path, permille)) {
        st.lastStretched = hit;
        return hit;
    }
    requestStretch(base->path, permille, barFrames);
    return st.lastStretched;
}

bool DjtoolzScheduler::fireOneShot(int toolId, qint64 nowNs)
{
    auto m = tool(toolId);
    if (!m || m->type() != DjtoolzTool::OneShot)
        return false;
    if (nowNs == 0)
        nowNs = BeatSource::steadyNowNs();
    const ToolSettings s = m->settings();
    auto sample = s.samplePath.isEmpty() ? nullptr : cache_->peek(s.samplePath);
    if (!sample) {
        if (!s.samplePath.isEmpty())
            requestLoad(s.samplePath);
        return false;
    }
    const bool running = source_->running() && source_->bpm() > 0.0;
    std::shared_ptr<const Sample> played = sample;
    const double srcBpm = sample->bpm > 0 ? sample->bpm : s.manualBpm;
    if (s.stretch && srcBpm > 0 && running) {
        const int permille = tempoPermille(source_->bpm() / srcBpm);
        if (permille < 999 || permille > 1001) {
            if (auto hit = stretch_.peek(sample->path, permille))
                played = hit;
            else
                requestStretch(sample->path, permille, 0);   // as recorded this time
        }
    }
    qint64 ns = nowNs + immediateLeadNs_;
    if (s.quantize && running) {
        const double beat = source_->beatsAtSteadyNs(nowNs);
        if (beat >= 0.0) {
            ns = source_->steadyNsAtBeat(std::ceil(beat));
            if (ns < nowNs + immediateLeadNs_)       // too close to be heard on that beat: take the next one
                ns = source_->steadyNsAtBeat(std::ceil(beat) + 1.0);
        }
    }
    Trigger t;
    t.sample = played;
    t.steadyNs = ns + qint64(s.offsetMs * 1e6);
    t.gain = s.gain * float(s.volume) / float(DjtoolzTool::kMaxVolume);
    t.tag = m->id();
    if (s.volume > 0)
        sink_(t);
    return true;
}

void DjtoolzScheduler::tick(qint64 nowNs)
{
    const auto mods = tools();
    if (!source_->running()) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& m : mods) {
            if (m->isEffect())
                continue;
            m->cancelOneBar();
            auto it = states_.find(m->id());
            if (!m->usesSample() && !m->perVoiceSamples() && it != states_.end() && it->second.active && midiSink_) {
                // the sequencer stop dropped the queued note offs: release whatever may be sounding
                const ToolSettings s = m->settings();
                MidiNoteEvent e;
                e.kind = MidiNoteEvent::AllNotesOff;
                e.channel = std::clamp(s.midiChannel, 1, 16) - 1;
                e.device = s.midiDevice;
                e.tag = m->id();
                midiSink_(e);
            }
        }
        states_.clear();
        return;
    }
    const double nowBeat = source_->beatsAtSteadyNs(nowNs);
    const double bpm = source_->bpm();
    if (nowBeat < 0.0 || bpm <= 0.0)
        return;
    const double beatsPerMs = bpm / 60000.0;

    for (const auto& m : mods) {
        if (m->isEffect())
            continue;                                // effects work on the audio, not on the beat (FxRunner)
        m->finishOneBar(nowBeat);
        const ToolSettings s = m->settings();
        const bool oneBar = m->oneBarState(nowBeat) != OneBarState::Idle;
        State st;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            st = states_[m->id()];
        }
        if (m->perVoiceSamples()) {
            // decode the six samples ahead of time so the first bar after switching on is not missing hits
            for (const DrumVoice& v : s.drums)
                if (!v.samplePath.isEmpty() && !cache_->peek(v.samplePath))
                    requestLoad(v.samplePath);
        }
        if (!s.enabled && !oneBar) {
            if (m->stretchesToBpm() && !s.samplePath.isEmpty()) {
                // get the stretched copy ready while the loop is off, so switching it on or the one bar button is not late
                if (auto base = cache_->peek(s.samplePath))
                    acidSample(s, base, bpm, st);
                else
                    requestLoad(s.samplePath);
            }
            if (st.active && m->stretchesToBpm() && stopSink_)
                stopSink_(m->id(), source_->steadyNsAtBeat(std::ceil(nowBeat)));   // a loop that is switched off ends on the next beat
            std::lock_guard<std::mutex> lock(mutex_);
            states_[m->id()].active = false;         // starts from "now" when it becomes active again
            states_[m->id()].lastStretched = st.lastStretched;
            continue;
        }

        if (m->perVoiceSamples()) {
            tickDrums(m, s, nowBeat, bpm);
            continue;
        }
        if (!m->usesSample()) {
            tickMidi(m, s, oneBar, nowBeat, bpm);
            continue;
        }
        std::shared_ptr<const Sample> sample;
        if (!s.samplePath.isEmpty())
            sample = cache_->peek(s.samplePath);
        if (!sample) {
            if (!s.samplePath.isEmpty())
                requestLoad(s.samplePath);
            continue;                                // skip this beat, the next one will find it decoded
        }

        std::shared_ptr<const Sample> played = sample;
        if (m->stretchesToBpm()) {
            played = acidSample(s, sample, bpm, st);
            if (!played) {
                std::lock_guard<std::mutex> lock(mutex_);
                states_[m->id()].lastStretched = st.lastStretched;
                continue;                            // the stretched copy is being made: wait for the next bar
            }
        }
        const double durationMs = played->lengthMs();
        const bool reverse = s.reverseSync && m->supportsReverseSync();
        const double lookaheadBeats = (kLookaheadMs + (reverse ? durationMs : 0.0)) * beatsPerMs;

        double from = nowBeat;
        if (st.active) {
            if (nowBeat + lookaheadBeats + 1.0 < st.scheduledTo) {
                m->cancelOneBar();                   // the sequencer restarted: its beat count went back
                from = nowBeat;
            } else {
                from = std::max(st.scheduledTo, nowBeat);
            }
        }
        const double to = nowBeat + lookaheadBeats;
        std::vector<PlannedHit> hits;
        if (to > from)
            m->plan(from, to, hits);

        for (const PlannedHit& h : hits) {
            qint64 ns = source_->steadyNsAtBeat(h.beat) + qint64(s.offsetMs * 1e6);
            qint64 startFrame = 0;
            if (reverse) {
                ns -= qint64(durationMs * 1e6);
                const qint64 earliest = nowNs + immediateLeadNs_;
                if (ns < earliest) {
                    // the start has already passed: play the rest if more than half is left, else skip this hit
                    const double playedMs = double(earliest - ns) / 1e6;
                    if (durationMs - playedMs <= durationMs / 2.0)
                        continue;
                    startFrame = qint64(playedMs * DecodedAudio::kSampleRate / 1000.0);
                    ns = earliest;
                }
            }
            Trigger t;
            t.sample = played;
            t.steadyNs = ns;
            t.gain = s.gain * float(s.volume) / float(DjtoolzTool::kMaxVolume);
            t.tag = m->id();
            t.startFrame = startFrame;
            if (m->stretchesToBpm() && stopSink_)
                stopSink_(m->id(), ns);              // a loop restarts on the bar line: the previous pass ends there
            if (s.volume > 0)
                sink_(t);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        State& ns = states_[m->id()];
        ns.lastStretched = st.lastStretched;
        ns.active = true;
        ns.scheduledTo = std::max(to, from);
    }
}

} // namespace vdj
