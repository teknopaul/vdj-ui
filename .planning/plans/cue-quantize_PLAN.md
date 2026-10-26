# Cue-jump quantize for the local track player

Goal: a red, checkable **Q** button (tooltip "Quantize") in `PlayerPanel`. Off: a cue button seeks exactly to the cue, as today.
On: the jump is aligned to the track's beat grid so the *phase within the grid step* is preserved. The grid step is the **global DJ
quantize resolution** (see below): 1/4 beat, 1/2 beat or 1 beat (default 1 beat). Some DJs mix on half beats.

- pressed `d` ms **after** beat B (while playing) -> jump to `cue + d` (the cue is treated as a beat, we land `d` after it);
- pressed `d` ms **before** the next beat -> wait `d`, then jump to exactly `cue` (we arrive on the beat, on the cue).

Both cases are the same rule: let `phase` = the play position's offset from the *nearest grid point*. Grid points are the beats
subdivided by the resolution (`beat + k * beatLen / div`, `div` = 1, 2 or 4).
`phase >= 0` (after the beat) -> target `cue + phase`. `phase < 0` (before the next beat) -> wait `-phase`, then target `cue`.
"Nearest" decides which case: if the position is in the second half of a grid step, the next grid point is nearer and we wait.
Everything below that says "beat" in the quantize context means "grid point at the global resolution".

What Q quantizes (and what it does not):

- **Cue jumps** -> to the *track's own* beat grid (phase preserved, as above). Needs the analysed grid.
- **Play** (from stopped/paused) -> to the *other deck*, see Phase 5. The reference is only ever the deck we are synced to (`DiffLock::player()`)
  or, with follow-master set, the master (`DiffLock` already locks to the master in that mode). Never to our own grid.
- **Back / forward jumps are never quantized and never use the beat grid**: they are pure BPM arithmetic (see Phase 4).

## Global quantize resolution (DJ preference)

- One app-wide setting, `enum class QuantizeRes { Quarter = 4, Half = 2, Beat = 1 }` (value = subdivisions per beat), persisted through
  `ConfigManager` under `dj/quantize` (default `Beat`). Exposed as a small `QuantizeSettings` accessor (new, `src/audio/quantize_settings.{h,cpp}`,
  holds the value, emits `changed(QuantizeRes)`), owned by `MainWindow` and handed to every `PlayerPanel` (pointer, same way other shared
  services are passed). **Every** quantized operation in the app reads this one setting: cue jumps and any later
  sampler/loop quantization. No component keeps its own resolution.
- UI: a combo box "Quantize: 1/4 | 1/2 | 1 beat" in the existing DJ/config area (look at how `main_window.cpp` exposes the other
  `*_config_dialog`s and put it beside them; if there is a general settings dialog use it, do not invent a new window).
- The setting only changes the grid step; the Q button stays per-panel on/off.

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
   Output latency (PulseAudio buffer ~60 ms) is deliberately **ignored**: jumps move by an exact amount derived from the BPM/beat grid, and the DJ
   compensates for latency by ear, as with play-from-stopped micro adjustments. No latency constant, no compensation anywhere.
4. Quantize is a **per-panel checkable `QPushButton`**: a normal red button labelled "Q" (red text via stylesheet), tooltip just "Quantize"
   (DJs know what it means; the rule is documented here and in the README, not in the tooltip). On/off is not persisted; the *resolution* is
   (global setting above).
5. Quantize does nothing special (plain exact seek) when: button off, player not playing (paused/stopped: seek exactly, there is no running
   phase), the track has no beat grid (and say so once in `status`), or the cue is a loop/memory point (treat all cues the same; no special case).
6. A second cue press while one jump is pending **replaces** the pending jump. Stop, pause, load, unload, a plain seek (waveform click) and
   tempo change all **cancel** the pending jump (tempo change: recompute is not worth it; cancel).

## Phase 1 - `BeatClock` (pure grid lookup) + tests

Files: new `src/audio/beat_clock.h`, `src/audio/beat_clock.cpp`; register in `src/CMakeLists.txt` (find where `audio_player.cpp` is listed and add
beside it); new `tests/test_beat_clock.cpp` + `add_executable`/`add_test` lines in `tests/CMakeLists.txt` copying the `test_beat_grid` pattern.

- `class BeatClock { void setBeats(const QVector<double>& ms, const QVector<quint8>& numbers); void clear(); bool isValid() const; struct Position { bool valid; int index; double sinceBeatMs; double toNextBeatMs; double beatLenMs; int beatNumber; }; Position locate(double posMs, int div = 1) const; double nearestBeatMs(double posMs, int div = 1) const; }`.
- `div` (1, 2, 4) subdivides each beat interval: `sinceBeatMs` / `toNextBeatMs` / `beatLenMs` then refer to the *grid step* (`beatLen/div`), so callers need no other change. `index` and `beatNumber` still refer to the beat.
- `valid` needs >= 2 beats. Before the first beat: index = -1 semantic is awkward - instead extrapolate: virtual beat at `first - len`; `beatNumber`
  counts back modulo 4. After the last beat: extrapolate with the last interval. Document the choice in the header comment.
- Tests (QtTest): exact on a beat (`sinceBeat == 0`), 40 ms after, 10 ms before the next, halfway (tie -> after), div=2 and div=4 (500 ms beats: 125 ms steps at div=4, so pos 1130 -> since 5, toNext 120, len 125), before first beat, after last
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
geany-progress done 3 -r src/audio/audio_player.cpp -r src/audio/audio_player.h -r tests/test_audio.cpp -w "The jump is split inside a render chunk; output latency is intentionally not compensated (DJ adjusts by ear)"
```

## Phase 4 - transport buttons, the Q button and the cue-jump logic

Files: `src/ui/player_panel.{h,cpp}`, new `src/audio/quantize_settings.{h,cpp}`, `src/ui/main_window.{h,cpp}` (setting + combo); test in `tests/test_player_quantize.cpp`.

**Transport buttons use UTF-8 glyphs instead of the current `style()->standardIcon` icons** (set with `QStringLiteral(u8"...")`/`QString::fromUtf8`,
check the font renders them; fall back to the plain text symbols ⏮ ⏩ ▶ ⏸ without the emoji variation selector if the emoji font is missing):

| Button | Glyph | Action | Quantized by Q? |
|---|---|---|---|
| back | ⏮️ | seek back one bar (4 beats) | no |
| play / pause | ▶️ / ⏸️ | toggle: shows ▶️ when stopped/paused, ⏸️ while playing | **play only**, to the other deck (Phase 5); pause never |
| forward | ⏩ | seek forward one bar (4 beats) | no |

- **Back / forward are pure BPM jumps.** The local BPM is trusted, so no beat grid is needed (they work on un-analysed tracks too):
  `beatSrcMs = 60000 / originalBpm_` (one musical beat in *source* time), `delta = 4 * beatSrcMs` (⏩ forward, ⏮️ back), `player_->seekMs(clamp(pos ± delta))`.
  When the DJ has nudged the tempo (`targetBpm_ != originalBpm_`) the *musical* amount stays one beat, but its wall-clock length is
  `60000 / targetBpm_`, so the two differ - that is intended and needs no special code because `seekMs` takes source time. Disabled only when
  `originalBpm_ <= 0`. Unit-test the arithmetic as a free function `double beatJumpMs(double originalBpm, int beats)`.
- Replace the separate play/pause buttons by the single play|pause toggle; keep stop as its own button ⏹️ (`stop_`, existing connects at
  `player_panel.cpp:95-97`).
- Q button: `QPushButton* quantize_`, checkable, text "Q", `setStyleSheet(QStringLiteral("QPushButton { color: red; }"))`, `NoFocus`,
  tooltip "Quantize". Placed at the start of the cue row (`cueLayout_`) so it survives `rebuildCueButtons()` (which only deletes `cueButtons_`;
  new buttons are inserted after it, keep the index logic correct). Getter `quantizeButton()`. It is enabled whenever a track is loaded (it
  also governs play, which needs no grid); cue quantization additionally needs `hasBeatGrid()` - when Q is on and a cue is pressed without a
  grid, jump exactly and show `status` "No beat grid: cue quantize unavailable" once.
- Cue jumps go through `jumpToCue(t)`:
  ```
  div = quantizeSettings_->subdivisions()          // 1, 2 or 4: the global preference, never a local copy
  if (!quantize_->isChecked() || state != Playing || !clock_.isValid()) { player_->seekMs(t); return; }
  pos = player_->positionMs()
  Position p = clock_.locate(pos, div);
  if (p.sinceBeatMs <= p.toNextBeatMs)            // nearer the previous grid point: we are late by `since`
      player_->seekMs(t + p.sinceBeatMs);         // pressed 40 ms after the grid point -> cue + 40
  else                                             // nearer the next grid point: wait for it
      player_->seekAtMs(pos + p.toNextBeatMs, t);
  ```
  Clamp `t + sinceBeat` to the track length. When the jump is pending, show `status` "Cue queued (NN ms)" and clear when `jumped` fires.
- Cancel/replace rules from the design decisions: nothing to do beyond what Phase 3 gives, except the second press replaces (calling `seekAtMs`
  again overwrites the pending fields).
- Tests (`test_player_quantize.cpp`): grid every 500 ms, cue at 10000 ms, resolution `Beat`:
  press at position 1040 -> seek to 10040; at 1480 -> pending jump at trigger 1500, target 10000; Q off -> 10000 immediately; paused -> 10000
  immediately; no grid -> 10000 immediately; press exactly on a beat -> 10000; tempo != 1 the trigger is still in source ms.
  Resolution `Half` (250 ms steps): press at 1040 -> 10040; at 1230 -> trigger 1250, target 10000; at 1100 -> 10100 (100 ms late, the 1250 point is 150 ms away).
  Resolution `Quarter` (125 ms steps): press at 1100 -> trigger 1125. Changing the global setting changes the result with no other change.
  `beatJumpMs(120, 1) == 500`, `beatJumpMs(120, 4) == 2000`, `beatJumpMs(0, 1) == 0`.
  Preferred structure: factor the cue decision into a pure function
  `QuantizedJump planQuantizedJump(const BeatClock&, int div, double posMs, double targetMs, double lengthMs)` returning
  `{bool wait; double triggerMs; double targetMs;}` in `beat_clock.h`, and unit test that one - it removes the need for a player.
- Persist the resolution (`dj/quantize`) via `ConfigManager`; test the load/save round trip like the other config tests, if they exist.
- Final (after Phase 5): full `make`, run all the tests (`ctest` in `build`), and add a short "Quantize" paragraph to `README.md` if it already
  documents the local player (Q button, the 1/4 - 1/2 - 1 beat preference, play syncs to the other deck, ⏮️ ⏩ are plain BPM jumps).

On completion run:
```sh
geany-progress done 4 -r src/ui/player_panel.cpp -r src/audio/beat_clock.h -r src/audio/quantize_settings.h -r tests/test_player_quantize.cpp -w "Not tried against real audio output: check by ear that the landing is on the beat" -w "Q on/off is not persisted; the resolution is (dj/quantize)"
```

## Phase 5 - quantized Play (start on the other deck's beat)

Files: `src/audio/audio_player.{h,cpp}`, `src/ui/player_panel.{h,cpp}`, `src/ui/main_window.cpp` (feeding the reference deck's beats), new
`src/audio/deck_phase.{h,cpp}` (pure), `tests/test_deck_phase.cpp`.

- When Q is on and play is pressed from stopped/paused: find the reference deck = `DiffLock::player()` if locked (this is the deck we are synced
  to, or the master when follow-master is on, since `DiffLock` then locks to the master). No reference deck -> plain play, `status` "Quantize: no
  sync deck", once.
- Pure helper `DeckPhase`: `onBeat(player, steadyNowMs, bpm, nextBeatMs)` records the last beat packet of each deck (`vdj::BeatInfo` in
  `src/cdj/cdj_wrapper.h` carries `bpm`, `barPos`, `nextBeatMs`; `MainWindow` already receives these for `DiffLock::onBeat` - feed the same
  packets). `double msToNextGrid(player, steadyNowMs, div) const` = time until the deck's next grid point at the global resolution, extrapolated from
  the last packet with the deck's beat length (`60000 / bpm`), `-1` if the deck is unknown or its last packet is older than ~2 beats.
  The deck's pitch-adjusted `bpm` is used as received; our own tempo is irrelevant for the wait (it is wall-clock).
- `AudioPlayer::playAfterMs(double wallDelayMs)`: starts playback after exactly that delay by the worker writing whole silence frames first (so
  the start is sample-accurate rather than 30 Hz GUI-timer accurate). Cancelled by `stop()`, `pause()`, `unload()`, `seekMs()`, `load()`. A second
  Play press while pending cancels it and starts immediately (the DJ can always override). `PlayerPanel` shows "Play queued (NN ms)".
- No latency compensation (see design decision 3); the DJ does micro adjustments.
- Tests: `DeckPhase` with synthetic packets - 500 ms beats, packet at t=0 with next beat in 300 ms, query at t=100 -> 200 ms; div=2 -> next grid point
  at 250 ms steps; stale deck -> -1; unknown deck -> -1; bpm change between packets. `playAfterMs` arithmetic in `test_audio.cpp` like Phase 3.

On completion run:
```sh
geany-progress done 5 -r src/audio/deck_phase.cpp -r src/audio/audio_player.cpp -r src/ui/player_panel.cpp -r tests/test_deck_phase.cpp -w "Play is quantized to the sync/master deck's beat packets, extrapolated between packets; not tried with real decks"
```

## Open questions for the human (defaults chosen, change if wrong)

- "Nearest grid point" decides wait-vs-late (half a grid step each way, step = the global resolution). If you would rather wait only for a short window
  (e.g. at most 1/8 beat, else jump late), change the `if` in `jumpTo` / `planQuantizedJump`.
- Stop button: the table assumes play|pause is one toggle and ⏹️ stays as a separate stop. Say if stop should go.
- ⏮️ is now "back one bar (4 beats)" because "start of the bar" needs the beat grid, which back/forward no longer use. If you want the true
  bar start back, say so and `BeatClock::barStartMs` returns (grid-based, un-analysed tracks would be disabled).
- Play quantize waits for the reference deck's next grid point at the global resolution; it does not align our own beat to the deck's *bar* (downbeat).
  Say if you want it to wait for the bar start (`barPos == 1`) instead.
