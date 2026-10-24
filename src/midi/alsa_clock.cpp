#include "alsa_clock.h"
#include <alsa/asoundlib.h>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace vdj {

AlsaClock::AlsaClock() = default;

AlsaClock::~AlsaClock()
{
    close();
}

bool AlsaClock::open(const QString& clientName, QString* error)
{
    if (seq_)
        return true;
    auto fail = [&](const QString& msg) {
        if (error) *error = msg;
        close();
        return false;
    };
    if (snd_seq_open(&seq_, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
        seq_ = nullptr;
        return fail(QStringLiteral("cannot open the ALSA sequencer"));
    }
    snd_seq_set_client_name(seq_, clientName.toUtf8().constData());
    client_ = snd_seq_client_id(seq_);
    port_ = snd_seq_create_simple_port(seq_, "MIDI clock",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port_ < 0)
        return fail(QStringLiteral("cannot create the ALSA clock port"));
    queue_ = snd_seq_alloc_named_queue(seq_, "vdj-ui clock");
    if (queue_ < 0)
        return fail(QStringLiteral("cannot create the ALSA queue"));
    quit_ = false;
    return setBpm(bpm_);
}

void AlsaClock::close()
{
    if (!seq_)
        return;
    stop();
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_ >= 0)
        snd_seq_free_queue(seq_, queue_);
    snd_seq_close(seq_);
    seq_ = nullptr;
    client_ = port_ = queue_ = -1;
}

bool AlsaClock::applyTempo(double bpmValue, double extraMicros)
{
    snd_seq_queue_tempo_t* tempo;
    snd_seq_queue_tempo_alloca(&tempo);
    const double micros = std::max(1000.0, 60000000.0 / bpmValue + extraMicros);   // micros per beat
    snd_seq_queue_tempo_set_tempo(tempo, static_cast<unsigned int>(std::lround(micros)));
    snd_seq_queue_tempo_set_ppq(tempo, kPpq);
    return snd_seq_set_queue_tempo(seq_, queue_, tempo) == 0;
}

bool AlsaClock::setBpm(double bpm)
{
    if (!(bpm >= kMinBpm && bpm <= kMaxBpm))
        return false;
    bpm_ = bpm;
    if (!seq_)
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    nudgeEnd_ = {};
    return applyTempo(bpm);
}

void AlsaClock::nudgeMillis(int ms)
{
    if (!seq_ || !running_ || ms == 0)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    const double bpm = bpm_;
    if (applyTempo(bpm, ms * 1000.0))
        nudgeEnd_ = std::chrono::steady_clock::now() + std::chrono::microseconds(std::lround(60000000.0 / bpm));
}

void AlsaClock::nudgeBpm(int multiplier)
{
    if (!seq_ || !running_ || multiplier == 0)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    const double bpm = bpm_;
    const double nudged = std::max(kMinBpm, bpm + 0.1 * multiplier);
    if (applyTempo(nudged))
        nudgeEnd_ = std::chrono::steady_clock::now() + std::chrono::microseconds(std::lround(60000000.0 / bpm));
}

bool AlsaClock::beatOffsetMs(double* ms) const
{
    const double beats = positionBeats();
    if (beats < 0.0)
        return false;
    double phase = beats - std::floor(beats);
    if (phase >= 0.5)
        phase -= 1.0;
    *ms = phase * 60000.0 / bpm_;
    return true;
}

double AlsaClock::queueBpm() const
{
    if (!seq_)
        return 0.0;
    std::lock_guard<std::mutex> lock(mutex_);
    snd_seq_queue_tempo_t* tempo;
    snd_seq_queue_tempo_alloca(&tempo);
    if (snd_seq_get_queue_tempo(seq_, queue_, tempo) < 0)
        return 0.0;
    const unsigned int micros = snd_seq_queue_tempo_get_tempo(tempo);
    return micros ? 60000000.0 / micros : 0.0;
}

double AlsaClock::positionBeats() const
{
    if (!seq_ || !running_)
        return -1.0;
    std::lock_guard<std::mutex> lock(mutex_);
    snd_seq_queue_status_t* st;
    snd_seq_queue_status_alloca(&st);
    if (snd_seq_get_queue_status(seq_, queue_, st) < 0)
        return -1.0;
    // tick time counts correctly across tempo changes, unlike the queue's real time
    return double(snd_seq_queue_status_get_tick_time(st)) / kPpq;
}

static void sendNow(snd_seq_t* seq, int port, snd_seq_event_type_t type)
{
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    ev.type = type;
    snd_seq_ev_set_source(&ev, port);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    snd_seq_event_output(seq, &ev);
    snd_seq_drain_output(seq);
}

void AlsaClock::start()
{
    if (!seq_ || running_)
        return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snd_seq_remove_events_t* rm;
        snd_seq_remove_events_alloca(&rm);
        snd_seq_remove_events_set_queue(rm, queue_);
        snd_seq_remove_events_set_condition(rm, SND_SEQ_REMOVE_OUTPUT | SND_SEQ_REMOVE_IGNORE_OFF);
        snd_seq_remove_events(seq_, rm);
        snd_seq_stop_queue(seq_, queue_, nullptr);   // a stopped queue restarts from tick 0
        snd_seq_drain_output(seq_);
        snd_seq_start_queue(seq_, queue_, nullptr);
        snd_seq_drain_output(seq_);
        sendNow(seq_, port_, SND_SEQ_EVENT_START);
    }
    quit_ = false;
    running_ = true;
    thread_ = std::thread([this] { loop(); });
}

void AlsaClock::stop()
{
    if (!running_)
        return;
    quit_ = true;
    if (thread_.joinable())
        thread_.join();
    running_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    stopLocked();
}

void AlsaClock::stopLocked()
{
    if (!seq_)
        return;
    snd_seq_remove_events_t* rm;
    snd_seq_remove_events_alloca(&rm);
    snd_seq_remove_events_set_queue(rm, queue_);
    snd_seq_remove_events_set_condition(rm, SND_SEQ_REMOVE_OUTPUT | SND_SEQ_REMOVE_IGNORE_OFF);
    snd_seq_remove_events(seq_, rm);
    sendNow(seq_, port_, SND_SEQ_EVENT_STOP);
    nudgeEnd_ = {};
    applyTempo(bpm_);
    snd_seq_stop_queue(seq_, queue_, nullptr);
    snd_seq_drain_output(seq_);
}

// Keep MIDI clock pulses queued two beats ahead of the queue's position.
void AlsaClock::loop()
{
    constexpr snd_seq_tick_time_t kStep = kPpq / kClocksPerBeat;   // ticks between clock pulses
    snd_seq_tick_time_t next = 0;
    while (!quit_) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!seq_)
                return;
            snd_seq_queue_status_t* st;
            snd_seq_queue_status_alloca(&st);
            snd_seq_tick_time_t now = 0;
            if (snd_seq_get_queue_status(seq_, queue_, st) == 0)
                now = snd_seq_queue_status_get_tick_time(st);
            const snd_seq_tick_time_t horizon = now + 2 * kPpq;
            bool queued = false;
            while (next < horizon) {
                snd_seq_event_t ev;
                snd_seq_ev_clear(&ev);
                ev.type = SND_SEQ_EVENT_CLOCK;
                snd_seq_ev_set_source(&ev, port_);
                snd_seq_ev_set_subs(&ev);
                snd_seq_ev_schedule_tick(&ev, queue_, 0, next);
                if (snd_seq_event_output(seq_, &ev) < 0)
                    break;
                next += kStep;
                queued = true;
            }
            if (queued)
                snd_seq_drain_output(seq_);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (nudgeEnd_ != std::chrono::steady_clock::time_point{} && std::chrono::steady_clock::now() >= nudgeEnd_) {
                nudgeEnd_ = {};
                applyTempo(bpm_);               // the nudged beat is over
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

} // namespace vdj
