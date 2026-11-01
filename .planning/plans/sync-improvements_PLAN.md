# Sync improvements

Source: `ai-prompts/sync-improvements.prompt.md` (and `ai-prompts/problems-with-nudge.prompt.md`).

## Goal

Make the Sync button a real, visible, sticky sync mode: the ALSA sequencer and DJ Tools follow a chosen deck or the master, survive master changes without any audible jump, align to the beat when engaged, compensate DJ Tools audio latency separately from the MIDI offset, and (after real-world testing) track a remote deck's drifting or changing BPM fast enough that nobody hears it.

## What exists already (do not rebuild)

- `DiffLock` (src/midi/diff_lock.{h,cpp}): per-deck offset measurement, 4-beat average, auto nudge (limit 20 ms) on beat 4, `targetMs` (the "difflock" offset), follow-master flag, `nudgeTarget`. Tested in tests/test_diff_lock.cpp.
- `AlsaClock::nudgeMillis` (one-beat tempo change, restored after), `shiftBeats` (bar label), `beatOffsetMs`.
- `MainWindow`: `lockDiffLockTo`, `diffLockOff`, `setDiffLockFollowMaster`, `nudgeSequencer`, `captureDiffLockTarget`, `refreshDiffLock`, `bpmLockedTo`, `followLockedBpm`, `applyVdjBpm`, `copyBpmFromMaster`, `syncToMaster` (only copies BPM and broadcasts), `beatJumpClicked` back/forward one beat.
- `BeatTempo` (src/network/beat_tempo.h): measured BPM from beat packet arrival, already shown per deck (`setMeasuredBpm`).
- `DeckPhase` / `restartWaitMs` / `playWaitMs` (src/audio/deck_phase.*): wait-until-next-beat/downbeat of a remote deck.
- DiffLockBar nudge buttons (-10 -1 +1 +10) and keyboard `diffLockTargetNudgeRequested` already give the DJ micro-adjustment of the offset. No new code for that goal item.
- DJ Tools scheduling converts beats to steady-clock ns in src/audio/djtoolz_scheduler.cpp (`steadyNsAtBeat(...) + offsetMs` at ~lines 200, 298, 414).

## Gaps (the new code this plan covers)

| Goal item                                                            | Today                                                                          | New                                                                |
|----------------------------------------------------------------------|--------------------------------------------------------------------------------|--------------------------------------------------------------------|
| Sync is a visible toggle; second press stops                         | remote Sync only locks; VDJ Sync only copies BPM; no off by button             | `SyncController` + button states                                   |
| VDJ-row Sync = sync to master, sticky                                | `syncToMaster()` is a one-shot copy                                            | sync mode targeting "Master"                                       |
| Becoming master pauses sync, mode stays on, resumes with next master | `DiffLock::masterChanged` would lock to ourselves                              | explicit suspended state                                           |
| Master change causes no BPM/position change                          | `lock()`/`masterChanged` switch player and `followLockedBpm` snaps BPM at once | rebase offset + hold BPM until re-measured, then glide             |
| Nudge as master never changes BPM                                    | nudge is ALSA-only (`nudgeMillis`), probably fine                              | test it, guard it                                                  |
| Sync button aligns to the beat                                       | none (lock only measures and slews 20 ms per bar)                              | `SyncAligner` large phase correction, quantized                    |
| DJ Tools audio latency, persisted                                    | none                                                                           | `djtoolz` latency value in config, applied in scheduler            |
| Sync to calculated BPM checkbox                                      | `BeatTempo` is display only                                                    | option feeding `followLockedBpm`                                   |
| Remote BPM changes and drift                                         | correction only on beat 4, limit 20 ms                                         | telemetry first, then cadence + drift trim (gated on user testing) |

Library note: `libcdj` is at references/libcdj. Phases 1 to 9 need no libcdj change. Phase 1 checks whether `cdj_beat_calculated_bpm` loses precision (rounding); only if it does is a libcdj change proposed, in `ai-output/`, not made.

## Design decisions

1. **`DiffLock` stays the measurement and nudge engine.** New pure class `SyncController` (src/midi/sync_controller.{h,cpp}, no Qt widgets) owns the *intent*: `Mode { Off, Deck(pid), Master }`, the resolved reference (`pid` or 0), and a `Phase { Idle, Aligning, Rebasing, Locked, Suspended }`. `MainWindow` asks it what to do and calls `DiffLock`/`AlsaClock`. This keeps all the new logic unit-testable without a GUI or ALSA.
2. **Sync mode is sticky**; only the Sync button toggles it off (and removing the synced deck, which falls back to Off with a status message). Becoming master gives `Suspended`; a new master gives `Rebasing`.
3. **No immediate change on master change** means: BPM is held at the current ALSA tempo and the offset target is rebaselined to the *currently measured* offset to the new reference (after `DiffLock::kAvgSize` beats), then following resumes. Any BPM difference to the new master is closed by a glide (bounded BPM slew per beat), not a step.
4. **Phase alignment on Sync press** is a one-off correction up to +/- half a beat, applied at the next beat (Q on: next grid point from `QuantizeSettings`), spread over several beats if it exceeds a cap so decks driven by the sequencer do not hear a stutter. The DJ's back/forward-one-beat buttons still handle bar alignment; the bar label is not touched.
5. **Two latencies.** `difflock` target = MIDI/clock offset (existing). DJ Tools latency = separate signed ms, applied only in the DJ Tools scheduler, persisted in `ConfigManager` (section `dj`, key `djtoolz_latency_ms`).
6. **Know the measurement limits first** (doc/sync-timing.md, measured on real decks). The deck's beat packets are quantised to a 3 ms tick, the ALSA tick counter to 5 ms, and a tempo estimate from fewer than about 100 beats is mostly quantisation pattern. Hence phases 2 and 3 come before any new sync behaviour, and nothing in this plan trims the tempo from a short window.
7. **Measure before compensating.** The user wants to test whether calculated BPM matters and how fast corrections must be. Phase 1 builds telemetry (done); phases 9 and 10 are gated on the user's findings.

## Phase 1: Sync telemetry and BeatTempo verification (DONE)

Goal: the user can test iteratively with real decks and we can see what actually happens.

Files: new src/midi/sync_telemetry.{h,cpp}, src/ui/main_window.cpp (hook only), src/session_log.* (reuse its log path), src/CMakeLists.txt, tests/test_beat_tempo.cpp (new or extend existing), tests CMake.

- `SyncTelemetry`: when enabled (Debug menu check item, key `debug/sync_telemetry`), appends one line per remote beat to a log next to the session log: steady ms, pid, barPos, reported BPM, `BeatTempo` BPM, ALSA queue BPM, diff ms, avg diff, target ms, nudge applied, master pid, sync phase. Plain CSV so the user can plot it. A `sync_event(...)` line for presses of Sync, master changes, nudges, BPM changes.
- Hook calls in `beatReceived`, `beatTimed`, `statusUpdated`, `nudgeSequencer`, `lockDiffLockTo`. No behaviour change.
- Verify `BeatTempo`: unit tests with synthetic arrival times (exact 120.00, 127.93, jitter +/- 2 ms, 0.1% drift, window restart) assert the reported BPM is within 0.01 BPM and does not wobble from integer rounding of ns/ms. Record any rounding found in `ai-output/sync-bpm-rounding.md`; fix in `beat_tempo.cpp` only if it is a clear bug.
- Check the reported-BPM path for precision loss (`cdj_beat_calculated_bpm` in references/libcdj, `deck->setBpm`) and note it in the same file.
- Build, run `ctest -R "beat-tempo|difflock"`.

```sh
geany-progress done 1 -r src/midi/sync_telemetry.cpp -r tests/test_beat_tempo.cpp -w "telemetry is off by default; the CSV format is what the user will analyse, change columns only by appending"
```

## Phase 2: Sequencer sub-tick phase model

Why: the sequencer offset is read from the ALSA queue's tick counter, which moves in 5.08 ms steps at 123 BPM (96 ticks per beat), so every `diff_ms`, the rebase after a master change and the DJ Tools beat-to-time mapping carry up to one tick of error. The queue's *real time* is exact (probe: within 3 us of CLOCK_MONOTONIC with the HR timer). See doc/sync-timing.md sections 3 and 4.

Files: new src/midi/seq_phase_model.{h,cpp} (pure), src/midi/alsa_clock.{h,cpp}, src/audio/beat_source.cpp (`AlsaClockBeatSource`), src/midi/sync_telemetry.{h,cpp} (append a column), src/CMakeLists.txt, tests/test_seq_phase_model.cpp (new), tests/CMakeLists.txt.

- `SeqPhaseModel`: anchors `{realNs, beats, usPerBeat}`. `anchor(realNs, beats)` (start, restart, stop), `setTempo(realNs, usPerBeat)` (integrates the old tempo up to `realNs`, then switches), `beatsAt(realNs)`, and `consistentWith(tickBeats, realNs, tolTicks)`. All pure, nanosecond integers in, doubles out.
- `AlsaClock`: every place the queue is touched (start, restart, stop, `applyTempo` for `setBpm`, the nudge start and its end restore in the clock thread) reads `snd_seq_queue_status_get_real_time` and the tick under `mutex_` right before the tempo call and feeds the model. New `double positionBeatsExact() const`: model value, and resynced from the tick when `consistentWith` fails (more than 1.5 ticks away, e.g. a missed anchor after `restart()`). `beatOffsetMs` and `AlsaClockBeatSource::beatsAtSteadyNs / steadyNsAtBeat` use the exact position; `positionBeats()` (tick) stays for the bar label (`barBeats`, `barPos`) so nothing visible changes. The queue real time and `steady_clock` share a clock (CLOCK_MONOTONIC), so a steady-ns to queue-ns offset is taken once at start and re-measured on every resync.
- Telemetry: `diff_ms` becomes the exact offset; append `diff_tick_ms` (the old tick based number) so the two can be compared on a real deck.
- Tests: tempo changes (several `setTempo` calls, a nudge of +20 ms for one beat and its restore) against an analytic integration to 1 ns; `consistentWith` fires on an injected 3 tick error and not on 1.4 ticks; an `AlsaClock` integration test (the same AlsaClock use as test_diff_lock.cpp): 300 reads over one second give a strictly monotonic exact position whose difference from the tick position is in \[0, 1.5) ticks; after `nudgeMillis(10)` the exact position moves by exactly the nudge, within 100 us, where the tick position can only show 5 ms steps; `restart()` resets the model.
- Build in `build-dev`, run `ctest -R "seq-phase|difflock|djtoolz"`.

```sh
geany-progress done 2 -r src/midi/seq_phase_model.cpp -r src/midi/alsa_clock.cpp -r src/audio/beat_source.cpp -w "every queue tempo change must go through the model: a missed anchor shows up as a resync, check the resync counter in the test" -w "needs a real-deck look: compare diff_ms and diff_tick_ms in the telemetry"
```

## Phase 3: Quantised beat phase estimator (pure, tested on real data)

Why: the deck's beat packets are quantised to a 3 ms tick (doc/sync-timing.md section 1), so averaging N packets does not give sqrt(N) accuracy and a rate estimate from fewer than about 100 beats is mostly the quantisation pattern. Fit a model that knows about the quantisation instead.

Files: new src/network/beat_phase.{h,cpp} (next to `beat_tempo.h`), src/CMakeLists.txt, tests/test_beat_phase.cpp (new, QtTest like test_beat_tempo.cpp), tests/CMakeLists.txt (a `VDJ_SYNC_FIXTURES` compile definition pointing at tests/fixtures/sync-telemetry), src/midi/sync_telemetry.* (append columns).

- `BeatPhase` (same feeding style as `BeatTempo::addBeat(barPos, steadyNs)`, same restart rules for gaps and skipped bar positions): `estimate()` returns `{ bool valid; double quantumMs; double periodMs; double periodPpmUncertainty; double lastBeatMs; double phaseUncertaintyMs; bool resonant; int beats; }`.
  1. Learn the quantum `q` from the data: cluster the intervals (0.25 ms bins), `q` = the spacing of the levels when at least 2 levels have 3 or more observations each and all levels are multiples of that spacing within 0.3 ms; limit 0.5 to 10 ms. Not learned (one level only, or no multiples) = quantum unknown: `valid` stays false for the phase but `periodMs` falls back to the plain least squares of `BeatTempo` with the honest bound.
  2. Model: the packet for the beat at true time `b_i = phi + i * P` arrives at the next tick of a grid of spacing `q` (the grid offset is a nuisance constant), plus up to 0.6 ms outliers (the occasional 0.5 ms network jitter in the data). Find the set of `(phi, P)` that explains the most observations (tolerance 0.6 ms, brute force on a bounded grid around the mean interval, +-3 ms / N, is enough); the estimate is the centre of that set, the uncertainty its half-width. `resonant` = the phase half-width is more than q / 4 (the period is close to a multiple of q and the beats do not move through the tick pattern).
  3. Convention: `lastBeatMs` is the estimated *true* beat time on the steady clock, i.e. the arrival minus the average delay (q / 2) is NOT used; the constant 0..q delay is absorbed by the difflock target. State this in the header comment.
- Telemetry: append `est_period_ms, est_phase_unc_ms, est_resonant`; `MainWindow` feeds a `BeatPhase` per remote deck next to `BeatTempo`.
- Tests (acceptance criteria are the point of this phase):
  - Synthetic with known truth: random `P` (400 to 700 ms) and `phi`, `q` = 3 ms, outliers 2%, N = 8, 16, 32, 64, 1000 trials each: the true phase is inside the reported uncertainty in at least 95% of the trials, and the median uncertainty at N = 32 is under 1 ms when `resonant` is false.
  - Resonant case: 16 beats of period exactly 540.0 ms (every packet equally late): `resonant` is true and the uncertainty is about q.
  - Real data: using `steady-126-30bpm-plus10pct.csv`, `pitch-sweep-minus10-to-plus3.csv` (rows `kind=beat`, grouped by `pitch_raw`, runs of 40 beats or more) and `pitch-sweep-114-82bpm-old-schema.csv` (run at reported 114.820): the learned `q` is 3.0 ms +- 0.05; the period is within the reported bound of the nominal period times 1.00002 (the data says +20 ppm); intervals are all multiples of `q`. Also: never accept the six-to-ten-beat sweep runs as a tempo (periodPpmUncertainty is more than 150 ppm for them).
  - A BPM change (the `pitch_raw` changes) restarts the estimator.
- Build in `build-dev`, run `ctest -R "beat-phase|beat-tempo"`.

```sh
geany-progress done 3 -r src/network/beat_phase.cpp -r src/network/beat_phase.h -r tests/test_beat_phase.cpp -w "the quantum is learned, not hard coded: only one deck model has been measured" -w "unit tests on real CSVs; judge the estimator on a live deck by watching est_* in the telemetry"
```

## Phase 4: `SyncController` state machine (pure, tested)

Files: new src/midi/sync_controller.{h,cpp}, small additions to src/midi/diff_lock.{h,cpp} (`rebase`, `suspend`), src/CMakeLists.txt, new tests/test_sync_controller.cpp (snip framework per the `snip` skill, or QtTest as test_diff_lock.cpp does: match the neighbour).

- API sketch:
  - `void press(int pid, bool isLocalRow)`: remote deck row -> `Mode::Deck(pid)` or, if already that deck, `Off`; VDJ row -> `Mode::Master` or `Off` if already on.
  - `void masterChanged(int pid, int localId)`, `void deckLeft(int pid)`.
  - `int reference() const` (0 when Off, Suspended, or Master with no master yet), `Phase phase() const`, `bool on() const`, `bool bpmFollowActive() const`.
  - `void onBeatMeasured(...)` advances `Rebasing` to `Locked` once `DiffLock::hasAverage`.
- `DiffLock` additions: `rebase(int player)` (clear that deck's window, switch `player_`, mark target to be recaptured from the first full average, keeping the DJ's *relative* micro-offset on top), `suspend()` (player_ = 0 but keep `targetMs_` and the follow flag). `followMaster_` is generalised to be driven by the controller; the existing public methods keep their behaviour so test_diff_lock.cpp still passes.
- Rules covered by tests: Sync on deck 1, press again -> Off; Sync on deck 1 then deck 2 -> retarget (Rebasing); VDJ Sync with no master -> waits (Master, reference 0); master goes to us -> Suspended, mode still on; master goes to deck 3 -> Rebasing on 3; deck left -> Off or re-resolve for Master mode; Rebasing makes no nudge and holds BPM; `bpmFollowActive()` false in Suspended, Rebasing and Off.
- Build, run `ctest -R "sync-controller|difflock"`.

```sh
geany-progress done 4 -r src/midi/sync_controller.h -r src/midi/diff_lock.h -r tests/test_sync_controller.cpp -w "DiffLock public behaviour must be unchanged: existing test_diff_lock cases still pass"
```

## Phase 5: Wire it into the UI (toggle, visible state, master changes)

Files: src/ui/main_window.{h,cpp}, src/ui/deck_controls.{h,cpp}, src/ui/diff_lock_bar.{h,cpp}, tests/test_diff_lock.cpp (`panelAndSyncButtons` update) or a new GUI test.

- Uses the phase 2 exact sequencer offset everywhere (it already comes through `beatProbe_`). The rebase after a master change averages over a window long enough for the phase 3 `BeatPhase` estimate: while `BeatPhase::estimate()` is valid and not `resonant` the offset fed to `DiffLock::onBeat` is the offset to its `lastBeatMs`, otherwise the raw packet offset. Behind a setting `dj/sync_quantised_phase`, off by default until the user has compared it on a real deck (the telemetry has both). During `Rebasing` the window grows from 4 beats up to 16 as samples arrive; the first corrections are coarse, they do not wait.

- `MainWindow` owns a `SyncController`. `DeckArea::syncClicked` -> `syncController_.press(...)`; then `applyControllerState()` (replaces the body of `lockDiffLockTo`, `diffLockOff`, `setDiffLockFollowMaster`; keep those as thin wrappers because shortcuts and MIDI map call them).
- `DeckControls::setSyncLocked(bool)` becomes `setSyncState(SyncDisplay)`: `Off` ("Sync"), `Locked` ("Locked", amber as today), `Rebasing`/`Waiting` ("Syncing...", dim amber), `Suspended` ("Sync (master)", outline). The VDJ row shows the state too, with a tooltip naming the reference deck. Tooltips on both rows describe press-again-to-stop.
- `bpmLockedTo()` returns `syncController_.reference()` only when `bpmFollowActive()`. The BPM spinner is locked only then. Becoming master unlocks it automatically (we are in charge of BPM).
- Master-change handling in `statusUpdated`/`masterChanged`: call `syncController_.masterChanged`, never call `followLockedBpm()` directly while `Rebasing`; hold the ALSA tempo. `DiffLockBar` status text gains the phase.
- Nudge rule: `nudgeSequencer` while master, Suspended or Off must only call `clock_->nudgeMillis` (never `setBpm`/`applyVdjBpm`). Add a test with `AlsaClock` verifying `bpm()` is identical after a nudge and that nothing is broadcast through `network_->setLocalBpm`.
- Tests: the GUI sequence press/press, press on 2 decks, simulate `masterChanged` to local and back, and assert button texts, `bpmLockedTo()`, that `clock_->bpm()` never changes during Suspended and Rebasing, and that the first nudge after resume is within `kLimitMs` of 0.
- Build, run `ctest -R "difflock|sync"` and open the app (`/run`) to eyeball the buttons.

```sh
geany-progress done 5 -r src/ui/main_window.cpp -r src/ui/deck_controls.cpp -r src/ui/diff_lock_bar.cpp -w "MainWindow is large: keep sync glue in a few small methods, do not add to the beatReceived lambda beyond the controller call" -w "needs a real-deck test by the user: Sync toggle, then change master on the decks"
```

## Phase 6: Beat alignment on Sync press

Files: new src/midi/sync_aligner.{h,cpp} (pure), src/midi/alsa_clock.{h,cpp} (`phaseShiftMs`), src/ui/main_window.cpp, tests/test_sync_aligner.cpp, tests/test_diff_lock.cpp (AlsaClock section).

- `SyncAligner::plan(diffMs, beatLenMs, targetMs, quantizeDiv, nowMs)` returns `{ int firstShiftMs, int perBeatMs, int beats, double waitMs }`: the signed shift needed to bring the measured offset to the target, wrapped to the nearest of +/- beatLen/2 so we always take the short way, split into at most N beats of at most `kAlignStepMs` (default 40) each so the audible slew is a smooth move, first step applied at the next beat or next quantize grid point (reusing `DeckPhase::msToNextGrid`).
- `AlsaClock::phaseShiftMs(int ms)`: queue a one-beat tempo change like `nudgeMillis` but not limited to 20 ms; refuses shifts beyond (beatLen * 0.75). Document that `setBpm` cancels it. Tests: a +/-120 ms shift at 120 BPM leaves `bpm()` unchanged and the queue position moved by that amount (the existing `alsaNudgeLastsOneBeat` pattern).
- Sync press flow (Aligning phase of the controller): (1) copy the reference BPM (`copyBpmFromMaster` path, honouring the BPM follow option from phase 8 later), (2) wait for `DiffLock::hasAverage` (4 beats) or use `DeckPhase` for an immediate estimate when the sequencer is running, (3) execute the plan, (4) go to `Locked`. If the sequencer is stopped: copy BPM only and say "applies once the VDJ is playing" (as `lockDiffLockTo` does now).
- Copy BPM button stays exactly as is: BPM only, no phase move. Add the sentence to its tooltip.
- Tests for `SyncAligner`: wrap-around (diff +300 ms at 120 BPM becomes -200), cap and split into beats, zero when already within 1 ms, quantize division 1/2/4 picks the right grid wait.
- Build, run `ctest -R "sync-aligner|difflock"`.

```sh
geany-progress done 6 -r src/midi/sync_aligner.cpp -r src/midi/alsa_clock.cpp -r src/ui/main_window.cpp -w "audible behaviour: the user must listen to a sync on a real deck before the step cap (kAlignStepMs) is trusted"
```

## Phase 7: DJ Tools latency compensation (separate from the MIDI offset)

Files: src/audio/djtoolz_scheduler.{h,cpp}, src/config/config_manager.* (only if a typed accessor is useful; otherwise `saveValue`/read like `dj/quantize`), src/ui/djtoolz_panel.* or diff_lock_bar (control placement), src/ui/main_window.cpp, tests (the existing djtoolz scheduler test with `FakeBeatSource`).

- `DjtoolzScheduler::setLatencyMs(double)`: every `steadyNs` computed from `steadyNsAtBeat(...)` gets `- latencyNs` (audio stack takes latency, so the sound is queued earlier; negative values delay). Three sites: drum voices (~line 200), one-shots (~298), loops (~414), plus the stop sink at ~362 for consistency.
- Persist: `dj/djtoolz_latency_ms` through `ConfigManager::saveValue` on change, loaded at start. Range -200..+500 ms.
- UI: a "Audio latency (ms)" spin box with the DJ Tools controls, tooltip explaining it only affects DJ Tools sound, not MIDI/clock (the difflock offset does that). It is *not* mixed into `DiffLock::targetMs`.
- Quantization note: doc/quantization.md says latency is ignored for the local-player quantization. Keep that for jumps; this value is only for the DJ Tools scheduler. Update the sentence.
- Tests: with latency +30 ms the first hit's `steadyNs` is exactly 30 ms earlier than with 0; negative later; the value round-trips through the config file; a very late hit (already past after subtracting) is dropped or played now following the existing "late hit" rule at ~line 40 of the header.
- Build, run `ctest -R djtoolz`.

```sh
geany-progress done 7 -r src/audio/djtoolz_scheduler.cpp -r src/ui/djtoolz_panel.cpp -w "sign convention: positive = audio stack is slow, schedule earlier; make sure the tooltip says so"
```

## Phase 8: "Sync to calculated BPM" option

Files: src/ui/main_window.{h,cpp}, src/ui/diff_lock_bar.{h,cpp} (checkbox), src/config, tests.

- Checkbox "Calculated BPM" on the VDJ row's sync bar, persisted as `dj/sync_calculated_bpm`, default off.
- New `double MainWindow::syncReferenceBpm(int pid) const`: if the option is on and `beatTempo_[pid].valid()` return `beatTempo_[pid].bpm()`, else the reported `deck->bpm()`. `followLockedBpm()` and the Sync-press BPM copy use it. Hysteresis so ALSA tempo is only reapplied when the value moved by more than 0.01 BPM (avoid retuning the queue every beat from fitting noise), and the telemetry columns (phase 1) show which source was in use.
- During `Rebasing` and `Suspended` the option does nothing (decision 3).
- Tests: with the option on, a fake `BeatTempo` window at 127.80 and reported 128.00 gives 127.80 on the ALSA clock; off gives 128.00; falls back to reported until `valid()`.
- Build, run `ctest -R "sync|beat-tempo"`. The user then tests whether this makes a significant difference.

```sh
geany-progress done 8 -r src/ui/main_window.cpp -r src/ui/diff_lock_bar.cpp -w "STOP after this phase: the user tests calculated vs reported BPM and the telemetry before phases 9 and 10 are started"
```

## Phase 9 (gated on user testing): correction cadence for a changing remote BPM

Do not start until the user has run phases 1 to 8 against real decks and supplied telemetry (or says what they heard). The aim is that a continuously changing remote BPM stays inaudibly in sync even though there is no "BPM is being changed" signal.

Known limit (doc/sync-timing.md): while the DJ moves the pitch fader the reported BPM changes every few beats, and every change restarts the `BeatTempo` and `BeatPhase` windows. There is then no usable measured tempo, only the reported one, so this phase trusts the reported BPM and corrects phase often with small steps. It does not try to estimate a tempo.

Files: src/midi/diff_lock.{h,cpp}, new src/midi/bpm_change_detector.{h,cpp}, tests.

- `BpmChangeDetector`: per reference deck, remembers the time and value of the last reported-BPM change (and the calculated-BPM slope). `recentlyChanged(nowMs)` is true for `kHoldBeats` (about 2 bars) after a change.
- `DiffLock`: a correction cadence parameter `correctEvery` (beats): 4 (today, beat 4 only) when steady, 1 when `recentlyChanged`, with a per-correction cap that scales (`kLimitMs` stays 20 for steady; use a smaller per-beat cap, e.g. 5 ms, in fast mode so many small steps are inaudible). Both values come from the telemetry findings and live in constants with a comment citing them.
- Tests: simulated deck whose BPM steps 124 -> 126 at t0: assert the offset error stays under a threshold (e.g. 5 ms) with cadence 1 and exceeds it with cadence 4 (documents why the code exists).
- Build, run `ctest -R difflock`.

```sh
geany-progress done 9 -r src/midi/diff_lock.cpp -r src/midi/bpm_change_detector.cpp -w "constants must come from the user's measurements, not guesses; record them in doc/sync.md"
```

## Phase 10 (gated on phase 9 results): drift trim, DJ nudge heuristic, docs

Why there is no trim before this phase: doc/sync-timing.md section 5. The measured deck error on long runs is about +20 ppm (0.01 ms per beat), which the difflock already absorbs with 1 to 2 ms nudges every 10 to 15 s; a tempo estimate good to 10 ppm needs about 300 beats and restarts on every BPM change; a short window estimate is the quantisation pattern and would make the trim wrong. This phase only starts if the telemetry from phases 2 to 9 shows nudging is audible or too frequent. Requirements when it is built: off by default behind a checkbox, never when we are master, a window of at least 64 beats (300 for a 10 ppm claim) at an unchanged reported BPM, based on `BeatPhase` (not the plain `BeatTempo` fit), applied to the ALSA tempo only, clamped to +-0.3%.

Files: new src/midi/drift_trim.{h,cpp}, src/midi/diff_lock.*, src/ui/main_window.cpp, src/ui/diff_lock_bar.*, new doc/sync.md, doc/vdj-ui.1.in, tests.

- `DriftTrim`: a least-squares slope of the measured offset against time per reference deck (after the offset has been rebased). A persistent slope beyond a dead band means the real BPMs differ: it proposes a tiny tempo trim (ppm, clamped to e.g. +/-0.3 percent) applied to the ALSA tempo *on top of* the followed BPM, never changing the displayed BPM. Case 2 from the prompt (the track's apparent beat moves) shows as a step, not a slope, so it is corrected by nudge only (no trim).
- Nudge heuristic (checkbox "Micro-BPM on nudge", default off; the Technics-expectation reason goes in its tooltip): when the DJ nudges in the direction of the detected real-vs-reported BPM difference, treat it as the DJ compensating and fold the equivalent ppm into `DriftTrim`. Nudges are otherwise trusted as timing and move the target as now (`nudgeTarget`). As master the option and the trim are disabled.
- Tests: synthetic drift of +0.05 percent converges with trim and not without; a step offset change produces no trim; nudging against the drift does not add trim; disabling the checkbox yields zero trim.
- `doc/sync.md`: modes, button states, offsets (difflock vs DJ Tools latency), the options, the telemetry log columns. Add a pointer in doc/vdj-ui.1.in and README only if sync is already documented there.
- Build, run the full `ctest`.

```sh
geany-progress done 10 -r src/midi/drift_trim.cpp -r doc/sync.md -w "trim applies to the ALSA tempo only; the displayed and broadcast BPM must stay the followed value" -w "heuristic needs real-deck validation"
```

## Cross-cutting notes for the executing agent

- Never use git. The user handles version control.
- Time bases: diffs are wall-clock ms measured at packet arrival (`beatProbe_`, steady clock). Do not mix with source-track ms.
- The sequencer position is quantised to ticks (96 per beat): do not chase sub-5 ms precision with `positionBeats()`; the existing `beatOffsetMs` is the right measurement.
- All sync intent flows through `SyncController`; keep `MainWindow` changes thin and small (it is already about 2200 lines). Log any general refactor ideas in `ai-output/refactor-recommendations.md`.
- Build with `build-dev/` only when the user says the export work no longer holds the build; until then write code and tests but do not build.
