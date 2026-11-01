# Sync timing: what the decks really send

Status: measured on real decks with the sync telemetry log (View > Log Sync Telemetry). The numbers below come from the CSV files kept as test fixtures in `tests/fixtures/sync-telemetry/`. The plan that uses them is `.planning/plans/sync-improvements_PLAN.md`.

## 1. Beat packets are quantised to 3 ms

The beat packets of the deck we measured (player 4, an XDJ class deck) are emitted on a **3.000 ms tick**. A beat happens, and the packet leaves at the next tick boundary, 0 to 3 ms later. The interval between two consecutive beat packets is therefore always a multiple of 3 ms:

| Fixture / run                  | Intervals seen (ms)                     | Nominal period (ms) | Mean interval (ms) |
|--------------------------------|-----------------------------------------|---------------------|--------------------|
| sweep, pitch -10.00%, 97 beats | 540.0 (30x), 543.0 (63x)                | 542.0               | 542.009            |
| sweep, pitch -7.95%, 51 beats  | 528.0 (17x), 531.0 (33x)                | 529.9               | 529.966            |
| sweep, 114.82 BPM, 914 beats   | 519.0 (16x), 522.0 (681x), 525.0 (198x) | 522.557             | 522.568            |

Consequences:

- What looked like network jitter (a spread of about 1 ms) is the quantisation: a uniform error over 3 ms has a standard deviation of 3 / sqrt(12) = 0.87 ms. Real network jitter is small: about 5 intervals in 150 are 0.5 ms off a multiple of 3.
- **The error is not random.** It follows a fixed sawtooth set by (beat period mod 3 ms). Averaging N beats does *not* shrink it like 1 / sqrt(N). If the period is close to a multiple of 3 ms (540.0 for example) every packet is late by the same amount and averaging gains nothing.
- The packet time is the audio beat time plus a delay of 0 to 3 ms, 1.5 ms on average. This is a constant offset for a given deck and is absorbed by the difflock target (the DJ's ear sets it).
- Measured on one deck type. The pcap of deck 1 shows a similar spread (about 1 ms) but the 3 ms value is not confirmed for other models. Treat the quantum as a per-deck parameter that is learned from the data (the spacing of the interval levels), not a constant.

### Other packet fields do not help

The "next beat" and "8th beat" fields of the beat packet are the deck's **grid** times at 100% pitch, in whole milliseconds (for example 451/452 at 133 BPM, 487/488 at 123 BPM, 522/523 at 114.82 BPM). They are the nominal period (or a multiple of it), not the time left until the next beat, so they cannot be used to correct the 0 to 3 ms lateness: the scatter of `arrival + next_beat / pitch` against a straight line is 0.96 ms, the same as the arrival alone. They are useful for the beat grid itself (variable BPM tracks, shifted grids), not for the deck's clock. Nothing in a beat or status packet is a deck timestamp, so packet arrival is the only phase signal there is.

## 2. The deck's real tempo versus its reported tempo

The telemetry columns `reported_bpm` (what the deck says) and `measured_bpm` (a 32 beat least squares fit of the arrival times) differ slightly. This is real, small and steady on the long runs of player 4:

| Run                       | Beats | Real vs reported period       |
|---------------------------|-------|-------------------------------|
| 114.82 BPM track at +0.0% | 914   | +21 ppm (slow), bound +-6 ppm |
| 114.82 track at +10%      | 139   | +20 ppm, bound +-22 ppm       |
| -10% pitch                | 97    | +26 ppm, bound +-57 ppm       |

ppm = parts per million. 100 ppm is 0.01%, about 0.012 BPM at 123 BPM, 0.05 ms per beat at 123 BPM.

An earlier session (another track, 123.03 BPM, 287 intervals) measured +123 ppm slow, bound +-21 ppm, so a slower deck, or session, exists. That log was not kept, so it is not a fixture.

The sequencer is exact: ALSA's tempo is a whole number of microseconds per beat, an error of about 0.001 ppm. The `alsa_bpm` column is the tempo we *set*, not a measurement. "Locked" means the difflock follows the deck, not that both clocks tick at the same rate. The small auto-nudges in the logs (1 or 2 ms every 10 to 15 seconds) are the difflock correcting this drift.

### The per-pitch table was an artifact

A pitch sweep with 6 to 10 beats at each fader position gave errors from -440 to +210 ppm. These are **not** deck errors. A run of 8 beats with identical 540.0 ms intervals looks like a tempo error but is the quantisation pattern not having changed yet. The bound on a rate estimate from N beats is about 3 ms / N: 57 ppm at 97 beats, 6 ppm at 914, and between about 130 and 600 ppm for 6 to 10 beats (`BeatPhase` reports it). Only runs of 100 beats or more say anything. No pitch dependent error and no number-type bug has been found on player 4. To look for one properly we would need 300 beats (2.5 minutes) at each of 15 or more fader positions, with `pitch_raw` and `track_bpm_raw` logged (they now are).

The pitch field of the beat packet is truncated, not rounded (+10% is 1153433, exact would be 1153433.6). That error is 0.5 ppm and not a cause.

## 3. The ALSA queue and the 5 ms steps

`AlsaClock::positionBeats()` reads the queue's **tick** counter (96 ticks per beat), which moves in whole ticks: 5.08 ms at 123 BPM. Every `diff_ms` value in the old logs is a multiple of that (-20.32 = 4 ticks). Setting the clock is not quantised: `nudgeMillis` changes the tempo by whole microseconds per beat, so 1 and 2 ms nudges work.

The queue's **real time** is not quantised. A probe program (a tiny C program, not in the repo) on a queue with the high resolution timer (`/proc/asound/seq/timer` shows HR timer) showed `snd_seq_queue_status_get_real_time` follows `CLOCK_MONOTONIC` within 3 microseconds while the tick moved in 5 ms steps. A sub-tick position can therefore be built from the real time, integrating the tempo at each tempo change. This needs no libcdj change: the ALSA code lives in vdj-ui.

This is implemented as `SeqPhaseModel` (src/midi/seq_phase_model.*), used by `AlsaClock::positionBeatsExact()`. The model is anchored when the queue starts (tick 0 and real time 0 coincide) and fed at every tempo change; it checks itself against the tick counter on each read and resynchronises from it (counted in `modelResyncs()`, which stays 0 in the tests) if it is ever more than 1.5 ticks away. Because the model sees the real tempo history it shows something the tick counter cannot: `nudgeMillis(n)` does not move the sequencer by exactly n ms. The tempo is n ms longer per beat for one beat's worth of *wall time* (the restore deadline), so the shift is n * base / (base + n): 9.8 ms for a request of 10 ms at 123 BPM, 19.2 ms for 20. The difflock closes the loop on the measured offset, so this only makes the first correction about 2% short. It is covered by a test.

On a system where the queue timer is the 1 kHz system timer, the real time is interpolated per read as well, but the dispatch of the MIDI clock itself is then only 1 ms accurate. USB MIDI adds up to one 1 ms frame. The drum machines hear the clock to about +-1 ms at best, whatever we compute.

## 4. What we cannot calculate yet

- **The beat's true time to better than the quantisation, for one beat.** One packet only says the beat happened within the last 0 to 3 ms. Sub-millisecond phase needs many beats and a quantised model fit (see below), and even then not when the beat period is near a multiple of 3 ms: the unknown stays until the deck's own drift moves the pattern.
- **The deck's real tempo to useful accuracy in less than about 100 beats.** The bound is about 3 ms / N: 300 beats (2.5 min) for 10 ppm. A BPM change (fader move) resets the window, so while the DJ is moving the fader there is no usable estimate at all.
- **Whether a deck's tempo error depends on pitch.** Not enough data (see section 2).
- **A deck we have not measured.** The 3 ms is from one model.
- **Where the audio beat is inside a packet, i.e. the attack of the sound.** The grid is where rekordbox (or the DJ) put it; drum attacks are a human judgement. See `references/libcdj/doc/cdj-timig.md`.
- **Anything about audio versus packets inside a loop.** A glitch where a loop drifted out of audible sync was caused by a bad rekordbox beat grid for that track and is not understood further. Packets are not tagged with loop state in the telemetry yet (the status packet's play state, 4 = looping, is not logged).

## 5. We do not trim the tempo (yet), and why

The sequencer follows the deck's reported tempo and the difflock removes the phase error with small nudges on the last beat of the bar. We do **not** add a ppm trim to the ALSA tempo to follow the deck's real rate, because:

1. The deck's error on the data we trust is about +20 ppm, 0.01 ms per beat. The difflock already absorbs this with 1 to 2 ms nudges every 10 to 15 seconds, which cannot be heard.
2. We cannot measure it fast enough. A 10 ppm estimate takes 300 beats, a BPM change resets it, and a short-window estimate is mostly the quantisation pattern, which would make the trim wrong and cause audible drift of its own.
3. The measurement of the sequencer's phase has to be fixed first (section 3). The old 5 ms steps hide a drift of this size completely.
4. The error bar says nothing about whether the cause is the deck, the pitch step or the track. A trim built on a guess about the cause would be a hidden surprise for the DJ. Technics decks do not do it.

When trimming is added (last phase of the plan) it is behind a switch, off by default, only when we are not the master, applied to the ALSA tempo only (never to the BPM we show or broadcast), clamped to +-0.3%, and driven by a window of at least 64 beats at an unchanged reported BPM.

## 6. What the next steps use

1. A **sub-tick sequencer phase model**: queue real time plus the tempo history gives the sequencer's beat phase to microseconds, instead of 5 ms steps. It also removes a +-5 ms wobble in the DJ Tools' beat-to-time mapping.
2. A **quantised phase estimator** (`BeatPhase`, src/network/beat_phase.*, built and tested on the three fixture files plus synthetic data, not yet used by the difflock; its numbers are in the telemetry as `est_period_ms`, `est_phase_unc_ms`, `est_resonant`). Measured behaviour: with 3% odd packets and 0.15 ms of jitter the true phase of the last beat is inside the reported uncertainty in 96 to 100% of trials, and the median uncertainty is about 1.2 ms for 8 beats, 0.85 for 16, 0.6 for 32 and 0.45 for 64; on the real files the median is 0.4 to 0.7 ms at 32 beats. The jitter tolerance starts at 0.25 ms and grows to 0.5 and 1.0 only when no solution exists (real data needs 0.25 mostly, 0.5 for some long windows). The quantum is learned from the interval levels and snapped to the nearest 0.5 ms when within 0.12 ms (the deck's tick is a timer period; unlearnable below 1.5 ms). It is kept across window restarts.

   How it works: fit the beat phase and period that explain every arrival time under "rounded up to a tick of q ms", with q learned per deck from the interval levels (their spacing). The fit gives a phase with an uncertainty, so callers know when it is good (not a resonance) and when to keep trusting the deck's reported tempo. Test fixtures are the three CSV files in `tests/fixtures/sync-telemetry/`.
3. The rest of the plan (sync mode state machine, alignment, latency, calculated BPM, cadence, trim) uses these two.

## The telemetry log

View > Log Sync Telemetry (CSV) writes `sync-telemetry-<timestamp>.csv` in the application cache directory (`~/.cache/vdj-ui`). Columns, append-only:

| Column                   | Meaning                                                |
|--------------------------|--------------------------------------------------------|
| steady_ms                | arrival time on the steady clock, ms                   |
| kind                     | `beat` or `event`                                      |
| pid, bar_pos             | deck and beat in the bar (1 to 4)                      |
| reported_bpm             | track BPM times the pitch multiplier, from the packet  |
| measured_bpm             | 32 beat fit of the arrival times (0 until 8 beats)     |
| alsa_bpm                 | the tempo we set on the queue (not a measurement)      |
| diff_ms, avg_diff_ms     | sequencer offset of this beat, and the rolling average |
| target_ms, nudge_ms      | difflock target, and the nudge applied on this beat    |
| master_pid, sync_state   | who is master, `off`, `locked` or `follow-master`      |
| note                     | event text (nudge, lock, master change, VDJ BPM)       |
| track_bpm_raw, pitch_raw | raw packet fields: 0.01 BPM, and 0x100000 = 100%       |
| pitch_pct                | pitch_raw as a percentage                              |
| next_beat_ms, beat8_ms   | the grid times at 100% pitch                           |

Fixtures in `tests/fixtures/sync-telemetry/`:

| File                                   | Content                                                                                                |
|----------------------------------------|--------------------------------------------------------------------------------------------------------|
| `pitch-sweep-114-82bpm-old-schema.csv` | Player 4. Fader moved over 103 to 126 BPM, with a 490 beat run at 114.82. Old schema (ends at `note`). |
| `steady-126-30bpm-plus10pct.csv`       | Player 4 at +10%, 139 beats, locked. diff_ms mostly within one tick.                                   |
| `pitch-sweep-minus10-to-plus3.csv`     | Player 4. Fader from -10% to +3.5%, with 100 and 52 beat runs at -10% and -7.95%.                      |

## 7. What the Sync buttons do now

The Sync button is a mode (`SyncController`, src/midi/sync_controller.*), not an action:

| Button               | Press                            | Press again |
|----------------------|----------------------------------|-------------|
| a remote deck's Sync | follow that deck                 | stop        |
| the VDJ row's Sync   | follow the master, whoever it is | stop        |

The button shows where it is: `Sync` (off), `Waiting` (on for the master, none yet), `Syncing...` (aligning to the DJ's target, or holding while the offset to a new reference is measured), `Locked` (following), `Master` (on, but we are the master, so nothing is followed; it follows the next master).

- **A master change must not move what the DJ hears.** When the reference changes while Sync is on (another deck, a new master, a master that comes back after we were master) the sequencer's position and BPM are held, the offset to the new reference is measured over four beats, that offset becomes the target (the ear-set alignment was against the old reference), and following resumes. The BPM then glides to the new reference's at 0.1 BPM per beat (`SyncController::kGlideBpmPerBeat`, provisional).
- **The first engagement keeps the DJ's target** (the offset in the bar) and the BPM is copied at once; the difflock nudges (20 ms per bar at most) until the offset is within one nudge of the target.
- **Becoming the master suspends following**, the BPM is ours again (the spinner unlocks) and the mode stays on.
- A deck that leaves ends a deck selection. A master that leaves leaves the mode waiting.
- **Beats feed the difflock from `beatTimed`**, which carries the packet's arrival time. With the experimental View option "Sync: use Quantised Beat Phase" the difflock gets the `BeatPhase` estimate (shifted by half a tick so the target keeps its meaning) while the estimate is valid and not resonant; the telemetry has both numbers (`diff_ms` raw, `diff_used_ms` what the difflock got).

### A coupling that was removed

Showing a deck's BPM (`DeckControls::setBpm`) changed the spinner, and a spinner change was treated as the DJ setting the VDJ's tempo. So while Sync was off, **every deck's BPM packet set the VDJ's tempo** (the telemetry shows `vdj bpm` events before any Sync was pressed). Only a real edit of the VDJ's spinner sets the tempo now, as the Copy BPM and Sync buttons do. If something relied on the old behaviour, it is the one line `QSignalBlocker block(bpmSpin_)` in `DeckControls::setBpm`.

## 8. Beat alignment on the first Sync press

The first press (deck or VDJ row) holds the difflock's 20 ms bar nudges, measures the reference's offset over 4 beats, wraps
`avg - target` to the nearest beat (so it never moves more than half a beat) and slews the sequencer onto it in steps of at most
`SyncAligner::kStepMs` (40 ms), one per reference beat, using `AlsaClock::phaseShiftMs`. `nudgeMillis(n)` moves only
n*beat/(beat+n); `phaseShiftMs` solves the tempo for the exact shift (limit 0.75 beat). It then waits 5 beats for the
average to catch up and releases the difflock, and the button turns Locked. Later master changes do not align (they rebase).
The step cap is a guess to be set by ear. The quantize grid wait (`SyncAligner::Plan::waitMs`) is computed by the aligner but
not yet used: the first step is at the next reference beat.
