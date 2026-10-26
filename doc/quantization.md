# Quantization

Status: the local player is in `.planning/plans/cue-quantize_PLAN.md`, the ALSA sequencer in `.planning/plans/alsa-quantize_PLAN.md`; both share these rules.

## Rules

- **One global resolution**: DJ preference `dj/quantize` (`ConfigManager`), 1/4, 1/2 or 1 beat (default 1). Every quantized operation reads it
  (`QuantizeSettings::subdivisions()` = 4, 2, 1). Never keep a per-feature copy.
- **Grid point** = beat + k * beatLen / div. "Nearest" decides the case: in the first half of a step we are late, in the second half we wait.
- **Phase-preserving jump**: pressed `d` ms after a grid point -> land `target + d`. Pressed `d` ms before the next grid point -> wait `d`,
  then land exactly on `target`. Both are one rule (`planQuantizedJump`).
- **What is quantized to what**
  - Cue jumps -> the track's own beat grid (`BeatClock`, needs an analysed track).
  - Play from stopped/paused -> the *other deck's* beats (`DeckPhase`): the deck we are synced to (`DiffLock::player()`) or the master when
    follow-master is on. Never our own grid. No reference deck -> play immediately.
  - Back/forward jumps are **not** quantized and do not use the grid: pure BPM arithmetic, `60000 / originalBpm` source-ms per beat
    (`beatJumpMs`). The local BPM is trusted.
- **Latency is ignored** everywhere. Move by exact BPM-derived amounts; the DJ compensates by ear.

## Time bases (the usual bug source)

- Beat grid and `AudioPlayer::positionMs()` are **source time**. Waiting is **wall time** = source / tempo.
- Triggers are executed in the audio/worker thread (sample-accurate: split the render chunk), never with the 30 Hz GUI timer.
- Other decks' beats come from beat packets (`vdj::BeatInfo`: `bpm`, `barPos`, `nextBeatMs`), extrapolated between packets; stale (> ~2 beats) = unknown.

## Cancel / replace

A new quantized request replaces a pending one. Stop, pause, load, unload, plain seek and tempo change cancel it.

## The ALSA sequencer (VDJ deck row)

Code: `AlsaClock` (`barBeats`, `barPos`, `shiftBeats`, `restart`), `src/midi/seq_bar.*` (pure bar arithmetic), `DeckPhase` + `restartWaitMs` + `playWaitMs`
(`src/audio/deck_phase.*`, pure), `MainWindow::vdjRestart`, `makeQuantizeButton` (`src/ui/quantize_button.*`, the same red/white Q as the local player).

The sequencer is a steady clock; "where we are in the bar" is only a label. The reference is the deck we are synced to (`DiffLock::player()`,
which is the master with follow-master on); with no sync deck we are the reference (we are the master).

- **Restart** (⏮️): the hardware jump, like adj's quantized restart: MIDI STOP, the queue back to tick 0, MIDI START on the running clock thread
  (`AlsaClock::restart`). Q off: now. Q on: on the sync deck's next **downbeat** (`barPos == 1`), so afterwards our bar 1 is the track's bar 1 (give or
  take latency); with no sync deck, on our own next whole beat; a sync deck that is unknown or stale falls back to our own beat (status message).
  The global 1/2 and 1/4 resolution does not apply: a bar starts on a beat.
- **Beat jumps** (⏪ ⏩): only the bar label moves (`beatOffset_`, 0..3); the clock and the MIDI stream are untouched, so they are inherently quantized and Q
  makes no difference. They do not change what the drum machines play (the optional Song Position Pointer experiment was not built).
- **Play** (▶️): Q on and synced to a deck: waits for that deck's next grid point at the global resolution (`playWaitMs`), then starts. No sync deck: starts
  at once, silently (a stopped clock has no phase). A stale sync deck: starts at once and says so once. The button already shows pause while waiting;
  pressing it again cancels the wait. Pause/stop never wait.
- **Always quantized, whatever the Q button says**: the MIDI-map `q_restart` op and the joystick trigger buttons (10, 11), as in adj. MIDI-map start / stop /
  toggle are **not** quantized (a controller is timed by the DJ's hand).
- Waits use a precise `QTimer` (~1 ms jitter); the other decks' phase is extrapolated from their last beat packet (arrival time from `beatTimed`), a few ms.
- Joysticks (`JoystickSeqBridge`, event map in `joystick_map.*`) have never been tried on hardware; only the pure event -> action map is tested. The numpad
  bridge (`numpad_seq_bridge.*`) is dead code against a non-existent `AlsaSequencer` and is not built.
- The existing "<< Beat" button (send this deck's BPM and bar position on the link) is now "Send Beat" so it cannot be confused with ⏪.
