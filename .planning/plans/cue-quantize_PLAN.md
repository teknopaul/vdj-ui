# Cue-jump quantize for the local track player

Goal: a red-text, checkable **Quantize** button in `PlayerPanel`. Off: a cue button seeks exactly to the cue, as today.
On: the jump is aligned to the track's beat grid so the *phase within the beat* is preserved:

- pressed `d` ms **after** beat B (while playing) -> jump to `cue + d` (the cue is treated as a beat, we land `d` after it);
- pressed `d` ms **before** the next beat -> wait `d`, then jump to exactly `cue` (we arrive on the beat, on the cue).

Both cases are the same rule: let `phase` = the play position's offset from the *nearest beat* in the grid.
`phase >= 0` (after the beat) -> target `cue + phase`. `phase < 0` (before the next beat) -> wait `-phase`, then target `cue`.
"Nearest" decides which case: if the position is in the second half of a beat interval, the next beat is nearer and we wait.

## Ground rules (read before every phase)

- Code is C++17 / Qt in `src/`. Match the surrounding style (4 spaces, `// comment` style, few comments, `QStringLiteral`).
- Build: `cd build && make -j8`. Tests live in `tests/` (QtTest, `QTEST_MAIN`, one executable per file registered in
  `tests/CMakeLists.txt`; look at `tests/test_beat_grid.cpp` and `tests/test_audio.cpp` for the pattern). Run with `ctest` or the executable.
- Do **not** run the GUI; verify with unit tests plus a clean build. Never use git commands that change state (no commit) unless asked.
- Do not touch the HUD, the deck widgets or the beat-grid *display* code except where a phase says so.

## Facts about the existing code (verified, do not re-derive)

- `src/ui/player_panel.{h,cpp}`: `PlayerPanel` owns `AudioPlayer* player_`, `WaveformWidget* waveform_`, `TrackAnalysis analysis_`,
  `QList<QPushButton*> cueButtons_`. `rebuildCueButtons()` connects each button to `player_->seekMs(double(t))` (`t` = cue `timeMs`).
  `originalBpm_` / `targetBpm_` give the tempo ratio `targetBpm_/originalBpm_` (also `player_->tempo()`).
- `TrackAnalysis` (`src/ui/track_analysis.h`): `QVector<AnalysisBeat> beats` (`ms` is a double, **source-track time**, sorted; `number` 1..4),
  `beatCount == 0` / empty `beats` = not analysed. `cues` is `QList<AnalysisCue>` with `timeMs`.
- `WaveformWidget::nearestBeat(double ms)` (private, `waveform_widget.cpp`) already does a nearest-beat lookup for hover text. Reuse its idea, do not
  couple the player logic to a widget.
- `AudioPlayer` (`src/audio/audio_player.{h,cpp}`): `positionMs()` is **source time** (`TimeStretcher::position()` frames -> ms, already
  accounts for tempo), guarded by `mutex_`. A worker thread renders `kChunkFrames = 1024` frames (~23 ms) per loop iteration into PulseAudio
  (`tlength` 60 ms). A GUI `QTimer` emits `positionChanged` ~30 Hz. `seekMs()` does `stretcher_.seek(...)` under the mutex.
- Because of tempo changes, **wall time = source time / tempo**. A source-time delay of `x` ms is `x / tempo` ms of waiting.
- `TimeStretcher::render(out, frames)` produces output; WSOLA output is not sample-exact after a seek but is exact enough (< 1 chunk).

## Design decisions (already taken)

1. **Beat-grid state lives in a small pure class**, `BeatClock` (new `src/audio/beat_clock.{h,cpp}`, no Qt GUI deps, trivially unit-testable):
   holds a sorted `QVector<double>` of beat times (source ms) + bar numbers, answers
   `locate(posMs) -> {index of beat at or before pos, msSinceBeat, msToNextBeat, beatMs (interval), beatNumber}`, with binary search
   (`std::upper_bound`) so it is O(log n) and can be called every position tick. Positions before the first beat / after the last one are
   handled by extrapolating with the first/last interval (or return `valid=false` if the grid has < 2 beats).
2. **Tracking while playing** = `PlayerPanel` feeds every `positionChanged` into the clock and keeps `BeatPhase current_` (beat index, number,
   phase in ms and 0..1) in memory; exposed via a getter and a `beatPhaseChanged` signal only when the beat index changes (cheap, usable later
   by a beat indicator). No UI is required for it in this plan beyond what Phase 4 shows.
3. **Quantized jumps are executed in the audio thread**, not with a 30 Hz GUI timer (33 ms granularity would defeat the feature).
   Add `AudioPlayer::seekAtMs(double sourcePosMs, double targetMs)` : "when the heard position reaches `sourcePosMs`, seek to `targetMs`".
   The worker checks, before each `stretcher_.render` chunk, whether the pending trigger falls inside this chunk
   (`pos + chunkFrames*tempo >= trigger`); if so render only the frames up to the trigger, then `stretcher_.seek(target)` and render the rest.
   Chunk-sized (~23 ms) granularity is acceptable only if the split render is not done; the split makes it sample-accurate to the render position.
   The PulseAudio buffer (~60 ms) adds a constant output latency that applies equally to the button press as to the beat the user hears, so the
   user's press is compared against the *heard* position as reported by `positionMs()`; document this and add a `// latency` constant
   `kQuantizeLatencyMs` (default 0, tweakable) in `PlayerPanel` rather than guessing.
4. Quantize is a **per-panel checkable `QPushButton`**, red text (`setStyleSheet("QPushButton { color: red; }")`), tooltip explaining the rule,
   state saved by `MainWindow`? **No**: keep it simple, default off, not persisted (Phase 4 may persist via `ConfigManager` `ui/quantize` if trivial).
5. Quantize does nothing special (plain exact seek) when: button off, player not playing (paused/stopped: seek exactly, there is no running
   phase), the track has no beat grid (and say so once in `status`), or the cue is a loop/memory point (treat all cues the same; no special case).
6. A second cue press while one jump is pending **replaces** the pending jump. Stop, pause, load, unload, a plain seek (waveform click) and
   tempo change all **cancel** the pending jump (tempo change: recompute is not worth it; cancel).

## Phase 1 - `BeatClock` (pure grid lookup) + tests

Files: new `src/audio/beat_clock.h`, `src/audio/beat_clock.cpp`; register in `src/CMakeLists.txt` (find where `audio_player.cpp` is listed and add
beside it); new `tests/test_beat_clock.cpp` + `add_executable`/`add_test` lines in `tests/CMakeLists.txt` copying the `test_beat_grid` pattern.

- `class BeatClock { void setBeats(const QVector<double>& ms, const QVector<quint8>& numbers); void clear(); bool isValid() const; struct Position { bool valid; int index; double sinceBeatMs; double toNextBeatMs; double beatLenMs; int beatNumber; }; Position locate(double posMs) const; double nearestBeatMs(double posMs) const; }`.
- `valid` needs >= 2 beats. Before the first beat: index = -1 semantic is awkward - instead extrapolate: virtual beat at `first - len`; `beatNumber`
  counts back modulo 4. After the last beat: extrapolate with the last interval. Document the choice in the header comment.
- Tests (QtTest): exact on a beat (`sinceBeat == 0`), 40 ms after, 10 ms before the next, halfway (tie -> after), before first beat, after last
  beat, 2 beats only, empty, a grid with a tempo change (intervals 500 ms then 400 ms), binary search with 5000 beats.
- Phase done when `make` is clean and the new test passes.

On completion run:
```sh
geany-progress done 1 -r src/audio/beat_clock.h -r src/audio/beat_clock.cpp -r tests/test_beat_clock.cpp -w "Before the first beat and after the last beat the grid is extrapolated from the neighbouring interval"
```

## Phase 2 - track the position inside the beat grid while playing

Files: `src/ui/player_panel.{h,cpp}` only.

- Add member `BeatClock clock_;` and `struct BeatPhase { bool valid=false; int beatNumber=1; double sinceBeatMs=0; double fraction=0; } phase_;`.
- In `loadTrack()` after `analysis_` is set (and cleared in the failure branches), `clock_.setBeats(...)` from `analysis_.beats`
  (build the two vectors; empty when `analysis_.beats` is empty). Clear the clock in the early-return failure paths too.
- In the existing `positionChanged` lambda call `updatePhase(ms)`: `clock_.locate(ms)` -> fill `phase_`; if the beat index changed since last time,
  `emit beatChanged(beatNumber)`.
- Public API: `bool hasBeatGrid() const`, `BeatPhase beatPhase() const`.
- Because `positionChanged` is only ~30 Hz the stored phase can be stale by up to 33 ms. For the jump logic (Phase 4) always call
  `clock_.locate(player_->positionMs())` fresh at the moment of the click; `phase_` is for display/state only. Note this in a comment.
- Test: extend a QtTest (new `tests/test_player_quantize.cpp`, same pattern as other widget tests, `QT_QPA_PLATFORM=offscreen` if the others do it -
  check `tests/CMakeLists.txt`) that builds a `TrackAnalysis` with beats every 500 ms, injects it, and checks `beatPhase()` after emitting
  `positionChanged(1040)` on `player()` : beat 3 (index 2), `sinceBeatMs == 40`. If `loadTrack` needs a real audio file, add a tiny protected/test
  seam `setAnalysisForTest(const TrackAnalysis&)` (document it as test-only) rather than shipping a fixture.

On completion run:
```sh
geany-progress done 2 -r src/ui/player_panel.cpp -r tests/test_player_quantize.cpp -w "phase_ is up to ~33 ms stale (positionChanged is ~30 Hz); the jump code re-reads the position instead"
```

## Phase 3 - `AudioPlayer::seekAtMs` (sample-accurate scheduled seek)

Files: `src/audio/audio_player.{h,cpp}`; possibly `src/audio/time_stretcher.h` if a helper is needed; extend `tests/test_audio.cpp`.

- State under `mutex_`: `bool pendingJump_; double jumpTriggerFrame_, jumpTargetFrame_;`.
- `void seekAtMs(double triggerSourceMs, double targetMs)` (public slot): converts to frames, sets the pending fields, `wake_.notify_all()`.
  If the trigger is already <= the heard position, perform `seekMs(targetMs)` immediately.
- `void cancelJump()` slot; called by `stop()`, `pause()`, `unload()`, `seekMs()`, `setTempo()` and `load()` (clear `pendingJump_` there, under the lock).
- Worker (`run()`), inside the existing locked region just before `stretcher_.render(chunk.data(), kChunkFrames)`:
  if `pendingJump_`, compute frames until trigger: `n = (trigger - stretcher_.position()) / stretcher_.tempo()`. If `0 < n < kChunkFrames`:
  render `n` frames into `chunk`, `stretcher_.seek(target)`, render the remaining `kChunkFrames - n` frames after them, clear `pendingJump_`.
  If `n <= 0` do the seek first and render the whole chunk. `ended_` handling unchanged. Emit a new signal `jumped(double targetMs)`
  (set an atomic flag in the worker, emitted from `tick()` like `failed_`/`ended_` are, since signals must not be emitted from the worker thread).
- Edge: the position estimate from `TimeStretcher::position()` already subtracts buffered output, so `n` is approximate to well under a chunk; document that.
- Tests in `test_audio.cpp` (follow what is there; the file already tests the player/time stretcher without a sound server - check how and
  reuse it, if the player cannot run headless test `TimeStretcher` + the scheduling arithmetic by factoring the split into a free function
  `splitAtTrigger(...)` that is unit-tested): trigger inside a chunk, trigger beyond a chunk (nothing happens yet), trigger already passed
  (immediate), cancel by seek, cancel by stop, tempo 0.5 and 1.5 (wall time conversion).
- Must not regress `seekMs`, `play`, `pause` behaviour: run the whole existing audio test.

On completion run:
```sh
geany-progress done 3 -r src/audio/audio_player.cpp -r src/audio/audio_player.h -r tests/test_audio.cpp -w "The jump is split inside a render chunk; the PulseAudio buffer (60 ms) is constant output latency and not compensated"
```

## Phase 4 - the Quantize button and the cue-jump logic

Files: `src/ui/player_panel.{h,cpp}`; test in `tests/test_player_quantize.cpp`.

- Add `QPushButton* quantize_` (checkable, text "Quantize", `setStyleSheet(QStringLiteral("QPushButton { color: red; }"))`, `NoFocus`,
  tooltip: "Cue jumps keep the position inside the beat: pressed 40 ms after a beat lands 40 ms after the cue; pressed just before a beat waits
  and lands exactly on the cue. Needs a beat grid."). Place it in the cue row (`cueLayout_`), left of the stretch / before the cue buttons so it
  survives `rebuildCueButtons()` (which only deletes `cueButtons_`; make sure it inserts new buttons after the quantize button, keep the index logic correct).
  Public getter `quantizeButton()`; `updateButtons()` enables it only when `player_->isLoaded() && hasBeatGrid()`; show a `status` message
  "No beat grid: quantize unavailable" once when toggled on without a grid.
- Replace the cue lambda body with `jumpToCue(t)`:
  ```
  if (!quantize_->isChecked() || state != Playing || !clock_.isValid()) { player_->seekMs(t); return; }
  pos = player_->positionMs() - kQuantizeLatencyMs (0 by default)
  Position p = clock_.locate(pos);
  if (p.sinceBeatMs <= p.toNextBeatMs)            // nearer the previous beat: we are late by `since`
      player_->seekMs(t + p.sinceBeatMs);          // pressed 40 ms after the beat -> cue + 40
  else                                             // nearer the next beat: wait for it
      player_->seekAtMs(pos + p.toNextBeatMs, t);  // arrive exactly on the cue at the beat
  ```
  Clamp `t + sinceBeat` to the track length. When the target is a pending jump, show `status` "Cue X queued (NN ms)" and clear when `jumped` fires.
- Cancel/replace rules from the design decisions: nothing to do beyond what Phase 3 gives, except the second press replaces (calling `seekAtMs`
  again overwrites the pending fields).
- Optional (only if trivial): persist the toggle via `ConfigManager` - skip otherwise; do not add a new dependency to PlayerPanel for it.
- Tests (`test_player_quantize.cpp`, using the seam from Phase 2 and a real or stubbed player as in earlier phases): grid every 500 ms, cue at 10000 ms:
  press at position 1040 -> seek to 10040; at 1480 -> pending jump at trigger 1500, target 10000; quantize off -> 10000 immediately; paused -> 10000
  immediately; no grid -> 10000 immediately; press exactly on a beat -> 10000; tempo != 1 the trigger is still in source ms. If the player cannot
  be observed, factor the decision into a pure function `QuantizedJump planQuantizedJump(const BeatClock&, double posMs, double cueMs, double lengthMs)`
  returning `{bool wait; double triggerMs; double targetMs;}` in `beat_clock.h`, and unit test that one - preferred, it removes the need for a player.
- Final: full `make`, run all the tests (`ctest` in `build`), and add a short "Quantize" paragraph to `README.md` if it already documents the local player.

On completion run:
```sh
geany-progress done 4 -r src/ui/player_panel.cpp -r src/audio/beat_clock.h -r tests/test_player_quantize.cpp -w "Not tried against real audio output: check by ear that the landing is on the beat; tune kQuantizeLatencyMs if it is consistently early/late" -w "Quantize state is not persisted"
```

## Open questions for the human (defaults chosen, change if wrong)

- "Nearest beat" decides wait-vs-late (half a beat each way). If you would rather wait only for a short window (e.g. at most 1/8 beat, else jump late),
  change the `if` in `jumpToCue` / `planQuantizedJump`.
- Output latency of the sound server is not compensated (see `kQuantizeLatencyMs`).
