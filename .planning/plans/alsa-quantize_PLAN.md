# Quantization and beat/bar jumps for the VDJ (ALSA sequencer)

Goal: the VDJ deck row (the local deck driving the ALSA sequencer) gets the same **Q** button as the local track player, UTF-8 transport
glyphs, and beat/bar jump buttons. Read `doc/quantization.md` first: the rules there (one global resolution, phase-preserving jumps,
play quantized to the *other* deck, latency ignored, cancel/replace) apply here unchanged. Local-player work is in
`.planning/plans/cue-quantize_PLAN.md` (Phases 1-4 done: `BeatClock`, `QuantizeSettings`, Q button style, `planQuantizedJump`).

## What the buttons do (decided)

The sequencer has no track and no beat grid of its own: it is a steady clock (`AlsaClock`, 96 ticks per beat, `positionBeats()` = beats since `start()`).
"Where we are in the bar" is a **label** (`barPos` 1..4: beat grid, link). Two different kinds of jump follow from that:

- **Restart** (⏮️ "start of the bar"): the *hardware* jump. Like `adj`'s quantized restart: MIDI STOP, the queue goes back to tick 0, MIDI START, so the drum
  machines start their pattern again and our bar label is 1. This is what the MIDI-map `q_restart` op, and the joystick triggers are for (the numpad bridge is out of scope).
- **Beat jump** (⏪ ⏩): relabel only. The bar position moves one beat, clock and MIDI are untouched. Inherently quantized: no timing changes.

| Button | Glyph | Q off | Q on |
|---|---|---|---|
| restart at bar start | ⏮️ | restart **now** (tick 0 at the press; beat phase vs. the decks is not kept - the DJ corrects by ear, difflock re-nudges) | restart on the **sync deck's next downbeat** when we are synced to a deck (`DiffLock::player()`, the master with follow-master); when we are the reference (no sync deck, i.e. we are the master) restart on **our own next whole beat** |
| back one beat | ⏪ | bar position -1 beat | same (timing untouched) |
| forward one beat | ⏩ | bar position +1 beat | same |
| play / pause | ▶️ / ⏸️ | start / stop the sequencer now | **Play** waits for the sync deck's next grid point at the global resolution, then starts (own clock has no phase when stopped, so with no sync deck Play is immediate); pause/stop never waits |
| Q | red "Q" | - | tooltip "Quantize" |

- "Next downbeat of the sync deck" = its next beat with `barPos == 1`, from the deck's beat packets (`DeckPhase`, Phase 4). If the deck is unknown or its packets are stale
  (> ~2 beats) fall back to our own next whole beat and say so once in the status bar.
- The global 1/2 and 1/4 resolution does **not** apply to restart (a bar starts on a beat); it applies to quantized Play.
- The MIDI-map `q_restart` op, the joystick trigger buttons are **always quantized** (the name says so, same as `adj`): they run the Q-on path whatever the Q button shows.
- There is only one bar, so no "forward a bar". ⏩ is one beat. (On the local *track* player ⏩ is a bar; that is intended.)
- Beat-jump model: `AlsaClock` keeps an integer `beatOffset_` (0..3). `barBeats() = positionBeats() + beatOffset_`; the label is `floor(barBeats) mod 4 + 1`.
  `start()`, `stop()` and `restart()` reset the offset.
- A beat jump does not change what the drum machines play; Phase 8 is the optional experiment for that.
- Jump buttons are enabled only while the sequencer is running (`clock_->running()`); Q is always enabled on the VDJ row.

## Ground rules (read before every phase)

- C++17 / Qt in `src/`. Match the surrounding style (4 spaces, short `//` comments, `QStringLiteral`).
- Build **from `build/`**: `cd build && cmake . && make -j8`. **Never `cmake ..`** (the repo root has a stale `CMakeCache.txt`, `cmake ..` regenerates the
  tracked in-source Makefiles). Tests: QtTest, one executable per file, registered in `tests/CMakeLists.txt`; the Qt tests run with
  `QT_QPA_PLATFORM=offscreen`. See `tests/test_beat_clock.cpp` (pure) and `tests/test_player_quantize.cpp` (widget) for the pattern.
- Do not run the GUI. Verify with unit tests plus a clean build. Never change git state (no commit).
- Glyphs are written as `QString::fromUtf8("⏮️")` like `PlayerPanel` does: ⏮️ `⏮️`, ⏪ `⏪`, ⏩ `⏩`, ▶️ `▶️`, ⏸️ `⏸️`.
- Latency is ignored everywhere (see `doc/quantization.md`).

## Facts about the existing code (verified, do not re-derive)

- `src/midi/alsa_clock.{h,cpp}`: `AlsaClock` (`kPpq = 96`), `start()` (stops/starts the queue, MIDI START, runs a clock thread), `stop()`,
  `running()`, `positionBeats()` (tick time / 96, `-1` when not running), `beatOffsetMs(double*)` (offset of "now" from the nearest beat,
  positive = last beat that long ago), `nudgeMillis()`. The sequencer handle is guarded by `mutex_`.
- `src/ui/main_window.cpp`: `setVdjPlaying(bool)` -> `clock_->start()/stop()` + `updateSequencerPlayhead()`; `setVdjTransport(bool)` is the
  full path (deck row state, ProLink playing status, sequencer). `updateSequencerPlayhead()` feeds `clock_->positionBeats()` into every VDJ row's
  `beatGrid()->setSequencerBeats(beats)` (~60 Hz). `isVdjDeck(pid)` / `isVdjRow(deck)` / `localVdjId()` identify the VDJ row.
  `restartRequested` (the MIDI-map `q_restart` op, `src/midi/midi_map_input.cpp`) currently does `clock_->stop(); clock_->start();` **immediately** - despite the name it is
  not quantized yet; Phase 7 fixes that.
- `DeckArea::playPauseClicked(pid, play)` is wired at `main_window.cpp` (~line 363). `DeckControls` (`src/ui/deck_controls.{h,cpp}`) owns
  `playPause_` (checkable, text "Play"/"Pause"), `sync_`, `becomeMaster_`, `copyBpm_`, `broadcastBeat_` (text `"<< Beat"`), `bpmSpin_`;
  `setRemote(true)` hides everything except Sync / Copy BPM. `DeckArea` forwards the controls' signals.
- `difflock_` (`src/midi/diff_lock.h`): `player()` = the deck the sequencer is locked to (0 = none; with `followMaster()` it is the master),
  `onBeat(pid, barPos, sequencerOffsetMs)`. Beat packets arrive in `NetworkManager::beatReceived(pid, bpm, barPos, sequencerOffsetMs)`
  (`vdj::BeatInfo` in `src/cdj/cdj_wrapper.h` has `bpm`, `barPos`, `nextBeatMs`); `main_window.cpp` ~line 244 handles it.
- `adj` (`~/bzr_workspace/adj/src/libadj.c`, `adj_quantized_restart`): sets a flag; the clock thread, which is in sync with the sequencer to within a tick, sees it at a bar line, then
  `midi_stop; tick = 0; midi_start`. That is the behaviour being reproduced (the readme says "on the next beat").
- `src/midi/joystick_seq_bridge.{h,cpp}` and `numpad_seq_bridge.{h,cpp}` call an `AlsaSequencer` class (`toggle()`, `stop()`, `quantizedRestart()`, `nudge()`, `adjustTempo()`)
  that **does not exist** in `src/`, and are not in `src/CMakeLists.txt`: dead code from an older design. The default Logitech map (axes 0-3, buttons 8 toggle, 9 stop, 10/11
  quantized restart) is in `joystick_seq_bridge.cpp`; `JoystickConfigDialog` is built and stores the joystick device. No joystick hardware is available yet: nothing there has ever run.
- Quantize pieces that exist: `QuantizeSettings` (`src/audio/quantize_settings.h`, `subdivisions()` 1/2/4, owned by `MainWindow::quantize_`, Config menu),
  `BeatClock`, `planQuantizedJump`, and the Q button style in `PlayerPanel` (`background #8a1c1c`, white bold "Q", checked = `#e01818` with white border, 26x22).
  **Not yet existing:** `DeckPhase` (cue-quantize Phase 5) - this plan creates it in Phase 5.

## Phase 1 - bar-position model and a light restart in `AlsaClock` (pure helper + tests)

Files: new `src/midi/seq_bar.{h,cpp}` (pure, no ALSA), `src/midi/alsa_clock.{h,cpp}`, register `seq_bar.cpp` next to `alsa_clock.cpp` in `src/CMakeLists.txt`,
new `tests/test_seq_bar.cpp` (+ `add_executable`/`add_test`, copy the `vdj-ui-beatclock-test` lines).

- `seq_bar.h` (namespace `vdj`): `int barPos(double barBeats)` (1..4, negatives handled: `((floor(b) % 4) + 4) % 4 + 1`); `double beatsToNextBeat(double beats)` (`ceil(b) - b`,
  exactly on a beat -> 0); `int wrapOffset(int offset)` (0..3, negative inputs); `double beatsToWallMs(double beats, double bpm)`.
- `AlsaClock`: `std::atomic<int> beatOffset_{0}`. API: `double barBeats() const` (raw + offset, `-1` when not running), `int barPos() const` (0 when not running),
  `void shiftBeats(int beats)` (offset += beats, mod 4, immediately), `void restart()`.
- `restart()` = the hardware jump: while running, reset the queue to tick 0 and send MIDI STOP then START, **without joining/recreating the clock thread** (read `start()`, `stop()` and
  `loop()` in `alsa_clock.cpp` first: reuse their queue stop/start/`sendNow` steps under `mutex_`, and check what `loop()` caches about the next tick so it stays consistent after the reset;
  if it caches anything, signal it with an atomic flag the loop consumes, as adj does with `adj_q_restart`). `beatOffset_` is reset to 0. When not running it does nothing.
  `start()` / `stop()` also reset `beatOffset_`. `restart()` must keep `running()` true.
- Tests (pure, `seq_bar`): `barPos` wraps 1..4 incl. negatives; `beatsToNextBeat` at 3.0, 3.25, 3.999; `wrapOffset` for +1/-1/+5/-5. `AlsaClock` needs a sequencer: add a case to the existing
  `alsaClockTempo` test group only if that can open a client (follow how it skips): `restart()` keeps `running()`, `positionBeats()` drops back below ~0.1 beats, `shiftBeats(1)` moves `barPos()`,
  `restart()`/`stop()` reset the offset, `barPos()` is 0 when stopped.
- Phase done when `make` is clean and the new test passes.

On completion run:
```sh
geany-progress done 1 -r src/midi/seq_bar.h -r src/midi/alsa_clock.cpp -r tests/test_seq_bar.cpp -w "restart() reuses the running clock thread; check by ear/with a drum machine that START arrives right after STOP and the pattern restarts" -w "A beat jump changes only the bar label, never the clock timing"
```

## Phase 2 - show and send the new bar position

Files: `src/ui/main_window.cpp` (`updateSequencerPlayhead`, the place the VDJ's barPos goes on the link), possibly `src/ui/beat_grid_widget.{h,cpp}`.

- `updateSequencerPlayhead()` passes `clock_->barBeats()` (not `positionBeats()`) to `setSequencerBeats`. Check `BeatGridWidget` uses the value only for bar position/phase (`seqBeats_`);
  if anything else assumes "beats since start" (e.g. a bar counter) read the code before changing it.
- Find where the VDJ's own `barPos` is produced for `network_->broadcastBeat(bpm, barPos)` / the local row's `DeckControls::setBarPos`
  (`grep -n "setBarPos\|broadcastBeat\|barPos" src/ui/main_window.cpp src/network/*.cpp`). When the sequencer is running use `clock_->barPos()` there; when stopped keep today's value.
  Do not change the beat packets' timing.
- Check `difflock_.onBeat(pid, barPos, sequencerOffsetMs)`: it receives the *deck's* bar position and the sequencer phase offset; confirm whether it compares bar positions with the sequencer's.
  If it does, feed it `clock_->barPos()` so a relabel is seen as a bar-alignment change.
- `sampler_scheduler.cpp` (~line 398) cancels a one-bar when "the sequencer restarted: its beat count went back": check it still works with `restart()` (it reads the beat count; `restart()` goes back to 0
  like `start()` did) and with the offset (it must use the **raw** beats, not `barBeats()`, or a beat jump would look like a restart).
- Test: `BeatGridWidget` given `setSequencerBeats(5.3)` vs `(6.3)` yields different bar positions (use the getter the existing `test_beat_grid.cpp` uses, e.g. `playheadBarFraction`).

On completion run:
```sh
geany-progress done 2 -r src/ui/main_window.cpp -r src/ui/beat_grid_widget.cpp -r src/audio/sampler_scheduler.cpp -w "Only the label moves on a beat jump: beat packets and MIDI clock timing are unchanged; the sampler must keep reading raw beats"
```

## Phase 3 - shared Q button + glyph buttons in `DeckControls`

Files: new `src/ui/quantize_button.{h,cpp}` (`QPushButton* makeQuantizeButton(QWidget* parent)` carrying the red/white style, text "Q", tooltip "Quantize",
checkable, `NoFocus`, fixed 26x22 - move the style out of `PlayerPanel` so there is **one** definition and make `PlayerPanel` use it),
`src/ui/deck_controls.{h,cpp}`, `src/ui/deck_area.{h,cpp}` (forward signals), `src/CMakeLists.txt`, tests in `tests/test_controls.cpp`.

- `DeckControls`: `playPause_` text becomes ▶️ / ⏸️ (keep it checkable; the label is what a click does today - only swap the strings "Play"/"Pause" for the glyphs; update tests that compare the text).
  Add `restart_` ⏮️ (tooltip "Restart at the start of the bar; with Q on, on the synced deck's next bar"), `back_` ⏪ ("Back one beat"), `forward_` ⏩ ("Forward one beat"), `quantize_` (Q).
  Layout order: `[Q] [⏮️] [⏪] [▶️] [⏩] [Sync] ...`.
- Rename the existing `broadcastBeat_` label `"<< Beat"` to `"Send Beat"` (tooltip: sends this deck's BPM and bar position on the link): it must not be confused with ⏪. Update tests that look for that text.
- `setRemote(true)` hides Q and the three jump buttons (remote decks are display-only); the local VDJ shows them. Getters `quantizeButton()`, `restartButton()`, `backButton()`, `forwardButton()`,
  `quantizeOn()`. `setJumpsEnabled(bool)` (disabled while the sequencer is stopped).
- New signals: `restartClicked(int playerId, bool quantize)`, `beatJumpClicked(int playerId, int beats)` (beats = -1 / +1). `playPauseClicked(pid, play)` stays; `quantizeOn()` is read where Play is handled (Phase 6).
  `DeckArea` forwards the two new signals like it does `playPauseClicked`.
- Tests: Q button text/tooltip/checkable like `TestPlayerQuantize::buttons`; remote deck hides the four buttons, local shows them; clicking ⏮️/⏪/⏩ emits the signals with the right arguments (Q off and on);
  play button text is the glyph and toggles.

On completion run:
```sh
geany-progress done 3 -r src/ui/deck_controls.cpp -r src/ui/quantize_button.cpp -r src/ui/player_panel.cpp -r tests/test_controls.cpp -w "'<< Beat' (broadcast) was renamed 'Send Beat' to avoid clashing with the new back-one-beat button"
```

## Phase 4 - `DeckPhase`: where the other decks are in their beat and bar (pure)

Files: new `src/audio/deck_phase.{h,cpp}` + `tests/test_deck_phase.cpp` (register both). `cue-quantize_PLAN.md` Phase 5 needs the same helper for the local player; create it here so both use it.

- `DeckPhase`: `void onBeat(int player, double steadyNowMs, double bpm, int barPos, double nextBeatMs)` records the last beat packet per deck (the packet's `barPos` is the beat that just happened, 1..4;
  `nextBeatMs` as in `vdj::BeatInfo`). Queries, all taking `steadyNowMs`, all returning wall ms or `-1` for an unknown deck or stale data (> ~2 beats old):
  - `msToNextBeat(player, now)`;
  - `msToNextGrid(player, now, div)` (div = 1, 2, 4: beat subdivisions, extrapolated with the deck's beat length `60000 / bpm`);
  - `msToNextDownbeat(player, now)`: the next beat with `barPos == 1`. With `e` = beats elapsed since the packet (`(now - t0) / beatLen`) and `b` = the packet's barPos, the deck is at
    `pos = (b - 1) + e` beats into its bar; the next downbeat is `4 - fmod(pos, 4)` beats away (a downbeat exactly now returns 0... but treat `< 1 ms` as "now").
  The deck's pitch-adjusted `bpm` is used as received; our own tempo never matters (wall-clock waits).
- Check `NetworkManager::beatReceived(pid, bpm, barPos, sequencerOffsetMs)` for what is available. If `nextBeatMs` is not in the signal, derive the packet's time as "arrival now" (the beat just
  happened) and say so in a comment; if you add a field to the signal, update every `connect` (`grep -rn beatReceived src tests`).
- Feed it in the `beatReceived` handler (`main_window.cpp` ~244) for every non-VDJ deck, with a `std::chrono::steady_clock` ms value.
- Tests with synthetic packets (500 ms beats): beat 3 at t=0 -> query at t=100: next beat 400, next downbeat 900 (beat 4 at 500, beat 1 at 1000 -> `1000 - 100`); div=2 -> 150 at t=100; query exactly on a
  downbeat; barPos wraps 4 -> 1; stale -> -1; unknown deck -> -1; bpm change between packets uses the newest packet.

On completion run:
```sh
geany-progress done 4 -r src/audio/deck_phase.cpp -r src/ui/main_window.cpp -r tests/test_deck_phase.cpp -w "Extrapolated from the last beat packet; accuracy is the packet arrival jitter (a few ms)"
```

## Phase 5 - quantized restart (⏮️) and beat jumps (⏪ ⏩) in `MainWindow`

Files: `src/ui/main_window.{h,cpp}`.

- `void MainWindow::vdjRestart(bool quantize)` is the **single** entry point for every restart (button now, MIDI map / joystick in Phase 7):
  ```
  if (!clock_ || !clock_->running()) return;
  cancelPendingRestart();                       // a second request replaces the pending one
  if (!quantize) { clock_->restart(); updateSequencerPlayhead(); return; }
  ref  = difflock_.player();                    // 0 = we are the reference (we are the master / not synced)
  wait = ref ? deckPhase_.msToNextDownbeat(ref, nowMs, ...) : -1
  if (wait < 0)                                 // we are the master, or the sync deck's beats are not usable
      wait = beatsToWallMs(beatsToNextBeat(clock_->positionBeats()), clock_->effectiveBpm())
      (if ref != 0 say once in the status bar: "Quantize: sync deck not seen, restarting on our own beat")
  restartTimer_ (single shot, Qt::PreciseTimer) -> clock_->restart(); updateSequencerPlayhead()
  status "Restart queued (NN ms)"
  ```
  `restartTimer_` is a member `QTimer`; `cancelPendingRestart()` stops it. Stop, play, a new restart and shutdown cancel it (`setVdjPlaying`, `shutdown`).
  Tempo changes need no handling (the wait is recomputed at press time; if the BPM changes during a wait that is below the DJ's notice).
- `DeckArea::restartClicked(pid, quantize)` -> `vdjRestart(quantize)` for the VDJ row. `beatJumpClicked(pid, beats)` -> `clock_->shiftBeats(beats); updateSequencerPlayhead()`; Q is irrelevant to it.
- Enable/disable the jump buttons from `setVdjPlaying` and `updateSequencerPlayhead` (`DeckControls::setJumpsEnabled(clock_->running())`) and when the deck row is created.
- Test: `MainWindow` cannot be driven here. Keep the decision a free function in `deck_phase.h` or `seq_bar.h` - `double restartWaitMs(int refDeck, const DeckPhase&, double nowMs, double seqBeats, double seqBpm, bool* usedFallback)` -
  and unit test it: master case (no ref) waits for the next whole beat of the sequencer, ref deck waits for its downbeat, stale ref falls back and sets `usedFallback`. Add it to `test_deck_phase.cpp`.

On completion run:
```sh
geany-progress done 5 -r src/ui/main_window.cpp -r src/audio/deck_phase.cpp -w "Restart is scheduled with a precise QTimer (~1 ms jitter) and then restart() on the live clock thread; not tried against real decks or drum machines" -w "Without a sync deck (we are the master) a quantized restart waits for our own next whole beat"
```

## Phase 6 - quantized Play for the sequencer (start on the sync deck's beat)

Files: `src/ui/main_window.{h,cpp}`.

- Play path (`DeckArea::playPauseClicked` handler at ~line 363 and `setVdjTransport(true)`): when starting (`play == true`), Q is on and `difflock_.player() != 0`:
  `wait = deckPhase_.msToNextGrid(ref, nowMs, quantize_->subdivisions())`; if `wait >= 0` start a precise one-shot `QTimer` (`startTimer_`) that then runs the normal start (`setVdjPlaying(true)`);
  status "Play queued (NN ms)"; the deck row's play button must only show "playing" when it actually starts (check what `setPlaying`/`network_->setLocalPlaying` do and delay both).
  No sync deck (we are the master, nothing to align to) or Q off -> start now, silently. A sync deck whose beats are stale -> start now and say once "Quantize: sync deck not seen".
- Stop/pause is always immediate and cancels a pending start. A second Play press while pending starts immediately (the DJ can always override).
- The MIDI-map `startRequested` / `toggleRequested` paths stay immediate (a hardware controller is timed by the DJ's hand); only the on-screen Play uses Q. Note this in `doc/quantization.md`.
- Test: the wait is `DeckPhase::msToNextGrid` (already tested in Phase 4); keep the timer wiring in one small function `startVdjAfter(ms)` and say untestable in the warning.

On completion run:
```sh
geany-progress done 6 -r src/ui/main_window.cpp -w "Start is scheduled with a precise QTimer, not on the ALSA queue; not tried against real decks" -w "MIDI-map start/toggle stay immediate"
```

## Phase 7 - MIDI map and joystick use the quantized restart

Files: `src/ui/main_window.cpp` (the `restartRequested` handler ~line 1662), `src/midi/joystick_seq_bridge.{h,cpp}` (rewrite to signals), `src/CMakeLists.txt`, new `tests/test_joystick_map.cpp`, `src/ui/joystick_config_dialog.*` only if it needs a hook.

- `restartRequested` (MIDI-map `q_restart`) now calls `vdjRestart(true)` - always quantized, like adj. It no longer does the immediate `stop(); start();`.
- `JoystickSeqBridge` is dead code against a non-existent `AlsaSequencer`. Rework it to **emit signals** the way `MidiMapInput` does: `startRequested`, `stopRequested`, `toggleRequested`, `quantizedRestartRequested`,
  `nudgeRequested(int ms)`, `tempoAdjustRequested(double bpmDelta)`, and remove the `AlsaSequencer*` dependency. Keep its device handling (`ConfigManager`, the same device the `JoystickConfigDialog` configures)
  and the default Logitech map from `adj_js.c` (axis 0: tempo +-0.1, axis 1: tempo +-1.0, axis 2: nudge +-10, axis 3: nudge +-1; button 8 toggle, 9 stop, 10 and 11 quantized restart).
  Factor the mapping into a **pure function** `JoystickAction mapJoystickEvent(int type, int number, int value)` (no device needed) and unit test it in `tests/test_joystick_map.cpp`: every default
  axis/button, value 0 ignored, button release ignored, unknown number ignored. The thread/`/dev/input/js*` reading stays as is.
- Add it to `src/CMakeLists.txt`, create it in `MainWindow` beside `midiIn_`, and connect its signals to exactly the same handlers `midiIn_` uses (start/stop/toggle -> `setVdjTransport`, quantized restart ->
  `vdjRestart(true)`, nudge -> `nudgeSequencer`, tempo -> `nudgeLocalBpm`), so a joystick and a MIDI controller behave identically. A missing device is silent (as the header says).
- `NumpadSeqBridge` is **out of scope** (no numpad available): leave `numpad_seq_bridge.*` untouched and out of the build.
- **No joystick hardware is available**: nothing here can be tried. Only the pure mapping is tested; say so in the done warning.

On completion run:
```sh
geany-progress done 7 -r src/midi/joystick_seq_bridge.cpp -r src/ui/main_window.cpp -r tests/test_joystick_map.cpp -w "Joysticks have never been tested on real hardware: only the pure event->action mapping is unit tested" -w "MIDI-map q_restart is now really quantized (it was an immediate stop/start before)"
```

## Phase 8 - optional, experimental: tell the drum machines about beat jumps (decide after trying 1-7)

Do this phase only if, after trying ⏪ ⏩ with the real drum machines, the relabel alone is not enough (the machines keep playing their own bar). Hardware behaviour varies, so it is behind a setting, off by default.

- Config `sequencer/send_spp` (`ConfigManager`, "0"/"1", default "0") and a Config-menu check item "Send song position on beat jumps".
- On a beat jump while running, send MIDI Song Position Pointer (SPP, 14 bit, unit = 6 MIDI clocks = a 16th note; position = 4 per beat from the song start) with the new bar beat, on the clock port, using the
  `sendNow`-style helper in `alsa_clock.cpp` (`SND_SEQ_EVENT_SONGPOS`). Many machines only honour SPP while stopped; if so, report what the machines do instead of building a stop/SPP/continue dance blind.
- Test: only the pure position arithmetic (`sppForBarBeats(barBeats)`) in `test_seq_bar.cpp`.

On completion run:
```sh
geany-progress done 8 -r src/midi/alsa_clock.cpp -r src/ui/main_window.cpp -w "Experimental and off by default: drum machines differ in how they handle Song Position Pointer while the clock runs" -w "Not tried against hardware"
```

## Phase 9 - docs and full run

Files: `doc/quantization.md`, `README.md` (only if it documents the deck row buttons).

- Update `doc/quantization.md`: the sequencer section (restart vs beat jump, the reference deck rule: sync deck / master with follow-master, otherwise we are the reference; quantized restart waits for the
  sync deck's downbeat, or our own next beat when we are the master; Play waits for the global-resolution grid of the sync deck; MIDI-map `q_restart` and the joystick triggers are always quantized;
  MIDI-map start/toggle are not; beat jumps are inherently quantized), remove the future tense of "For the ALSA sequence (later)", list `AlsaClock::restart/barBeats/shiftBeats`, `DeckPhase`, `vdjRestart`.
  Mention the `<< Beat` -> `Send Beat` rename.
- Full `cd build && cmake . && make -j8`, then `ctest`; report failures verbatim.

On completion run:
```sh
geany-progress done 9 -r doc/quantization.md -w "Run the whole ctest and listen to it on real hardware (decks, drum machines, joystick) before relying on it"
```

## Decisions confirmed by the human

- **Restart lands on the sync deck's downbeat** (`barPos == 1`): after a quantized restart (give or take latency) the sequencer is in sync with the bar of the playing track. Keep `msToNextDownbeat`.
- **Restart with Q off** is immediate and does not keep the beat phase (plain MIDI STOP/START at tick 0); difflock re-nudges toward the deck afterwards.
- **Play with no synced deck starts immediately** (a stopped clock has no phase to quantize to). Do not even show a status message for it; only a *stale* sync deck gets the one-time message.
- **Numpad bridge**: skipped, no hardware.

## Open questions for the human (defaults chosen, change if wrong)

- The ⏩/⏮️ buttons on the *local track player* jump a bar; the VDJ row's ⏪/⏩ jump a beat. If you want them identical, tell me.
