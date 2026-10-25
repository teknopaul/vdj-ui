# Live sampler ("bombomatic" family) for vdj-ui

Source prompt: `ai-prompts/audio-out.prompt.md`. Reference implementation of the simplest case:
`references/adj/src/mod/adj_mod_seq_bombomatic.c` (a note on every beat; ADJ drives MIDI, we also need to play audio).

A new **Sampler panel** lives where the Local Track Player panel lives (same central column in `MainWindow`, see `showPlayer`/`playerPanel_`
in `src/ui/main_window.cpp`) with its own **View -> Sampler** toggle (copy the *Local Track Player* action: checkable, saved in
`ConfigManager` section `ui`, key `sampler`, default on/off as you see fit - default **off**).
The panel holds a sample-folder tree, a list of *modules* (the sampler types) that can be added/removed, per-module name + on/off switch,
and saved **presets** (the DJ's favourite combination of modules and all their settings).

## Ground rules (read before every phase)

- C++17 / Qt (Qt6, falls back to Qt5; Widgets/Network only - **no QtSvg, no QtMultimedia**; do not add new Qt modules). Style: 4 spaces, short comments,
  `QStringLiteral`, `namespace vdj`. Look at `src/ui/player_panel.cpp` and `src/audio/audio_player.cpp` for the house style before writing.
- Build: `cd build && make -j8`. Tests are QtTest executables in `tests/` (pattern: `tests/test_beat_grid.cpp`, registered in `tests/CMakeLists.txt`).
  Do not run the GUI. Verify by unit tests plus a clean build. Do not commit.
- Audio output is PulseAudio/PipeWire via `pa_simple` (see `AudioPlayer::run()`); device list from `listAudioDevices()` (`src/audio/audio_output.h`).
  Samples are decoded with `decodeAudioFile()` (`src/audio/audio_decoder.h`) to 44.1 kHz 16 bit stereo (`DecodedAudio`); this also reads .wav.
  Tempo change with the key kept is `TimeStretcher` (`src/audio/time_stretcher.h`).
- MIDI output goes through the ALSA sequencer: `AlsaClock` (`src/midi/alsa_clock.h`) owns the "vdj-ui" client, the queue and the "MIDI clock" output
  port; `MidiRouter` lists/connects ports. `MainWindow::alsaClock()` returns the instance.
- Persistence is `ConfigManager::loadValue/saveValue(section,key,value)` (flat strings, `src/config/config_manager.h`); `configDir()` is the folder
  to keep preset files in.
- Every phase ends with a clean `make` and passing tests. Keep each new unit independent of the GUI where possible so it can be tested headless.

## Decisions already taken (change only if a phase finds them impossible)

1. **What "the beat" is.** The sampler follows the **VDJ sequencer clock** (`AlsaClock`: `bpm()`, `positionBeats()`, `beatOffsetMs()`), the same
   clock the decks sync to and ADJ's bombomatic follows. Samples are therefore in time with the decks that are synced to us. An abstraction
   `BeatSource` (below) hides it, so the local track player's beat grid (the `BeatClock` of `cue-quantize_PLAN.md`) or a deck can be added later.
   If the sequencer is not running, modules are silent (show "Sequencer stopped" in the panel).
2. **Audio engine.** One `SamplerEngine` with its **own `pa_simple` stream + worker thread** (to the device chosen in the Local Player's combo, or its own
   combo, default device) - PulseAudio mixes it with the track player, so `AudioPlayer` is not touched. The engine renders in small blocks
   (256 frames, ~6 ms), mixes active *voices* (sample + position + gain) into a block and writes it. A block is scheduled with a wall-clock
   (`steady_clock`) beat time so triggers are placed **sample-accurately inside the block** by the offset in frames.
   Output latency (stream buffer, `tlength` 40 ms) is a constant that is exposed as `kOutputLatencyMs` and compensated by firing early.
3. **Modules** share a base `SamplerModule` with: `name`, `enabled`, `sampleFolder`, `samplePath`, `offsetMs` (+/- to the beat, double),
   and a method `void schedule(const BeatWindow&, TriggerSink&)` that, for a window of beats `[b0,b1)`, emits triggers (sample, beat time, gain,
   playback ratio, loop flag). Types: `Bombomatic`, `StabPlayer`, `AcidLoop`, `OneShot`, `MidiMatic`. Each serialises to/from a `QVariantMap`.
4. **Up/down chooser** changes `samplePath` to the next/previous `.wav` in the same directory in ASCIIbetical order (`QString::compare`, case
   sensitive, wraps or clamps - clamp). The change is picked up at the **next trigger** (the engine reads the module's current sample when it
   fires, never mid-sample), so the DJ can flick through kicks live.
5. **Beat chooser** = four toggle images (beat 1..4 of the bar). `img/button-red.svg` = on, `button-yellow.svg` = off, `button-grey.svg` = disabled; `img/button-green.svg` = the **one-shot ("1 bar") button**
   (green = play).
   The project has no QtSvg and no resource file yet: **convert the four SVGs to PNG once** (`inkscape -w 24 -h 24 ... -o` or `convert`, both installed)
   into `img/button-red-24.png` etc. and ship them through a new `resources.qrc` (CMake `AUTORCC` is already `ON`). Keep the SVGs.
6. **Samples are .wav only** in the browser (name filter `*.wav`, case-insensitive). Decoding is cached per path (`SampleCache`, LRU by bytes,
   decode on a worker/lazy on first use, never on the audio thread).
7. Time-stretching for acid loops/one-shots is **done once when the BPM changes**, off the audio thread (render the stretched copy into a buffer
   and swap it in), not live per block. The BPM of a sample comes from (a) the RIFF `acid` chunk, else (b) the file name (`..._128bpm_...` /
   `128 BPM`), else (c) a per-module manual BPM field; with none known, one-shots are not stretched.
8. **One-shot button (Stab player and Acid loop; also fits Bombomatic if trivial).** A button on the module. While the module is enabled and the
   sequencer runs, a click arms **one bar**: the module plays exactly one bar (4 beats) of its normal pattern - for the stab player the stabs on the chosen beats
   of the next full bar, for an acid loop one pass of the 4-beat loop - then stops by itself and the button pops back. Quantised: the bar starts at the next bar
   line (beat `% 4 == 0`); a click during a bar plays the *next* bar, a second click while armed cancels. Implemented in the module as a small state
   machine `OneBarState { Idle, Armed(startBar), Playing(startBar) }` that gates `schedule()`; it is independent of `enabled` (a disabled module can still be
   fired by the one-shot button, the DJ's "stab on demand"; decide: **yes it fires even when the on/off switch is off**, the switch only controls the continuous mode).
9. **Reverse sync** (Stab player; Acid loop gets it too if it falls out naturally, otherwise leave it off for loops): the sample **ends** on the beat instead of
   starting on it - a reverse-cymbal / riser feel. For a trigger beat `B` the start time is `B - sampleDuration` (duration after any stretching, plus the
   negative/positive `offsetMs`). Consequences the phases must handle: the scheduler's lookahead must be at least the sample duration (so `lookahead = max(100 ms,
   longest reverse-synced sample duration + 100 ms)`, per module, not the global 100 ms); a trigger whose start is already in the past is **started part-way in**
   (`Trigger::startFrame` > 0) when more than half the sample remains, otherwise skipped for that beat; the engine must therefore support a start offset in the
   voice. "Reverse sync" does **not** reverse the audio.
10. Out of scope: recording the sampler, per-module effects/EQ, sample editing.

## Phase 1 - `BeatSource`, sample cache and WAV metadata (headless units + tests)

Files (new, under `src/audio/`): `sample_cache.h/.cpp`, `wav_info.h/.cpp`; `src/audio/beat_source.h`; register in `src/CMakeLists.txt` next to
`audio_player.cpp`; tests `tests/test_sampler_basics.cpp` (+ `tests/CMakeLists.txt`).

- `struct Sample { QString path; DecodedAudio audio; double bpm = 0; /* 0 = unknown */ QString name; }`; `class SampleCache` : `std::shared_ptr<const Sample> get(path)`
  (decode with `decodeAudioFile`, fill `bpm` from `wav_info`), thread-safe, LRU limit ~128 MB, `clear()`.
- `wav_info`: `double wavBpmFromFile(const QString& path)` : RIFF walk, `acid` chunk tempo (float at offset 20 of the chunk), else parse the file name with a regexp
  (`(\d{2,3}(?:\.\d+)?)\s*bpm`, case-insensitive); returns 0 when unknown. Also `QStringList siblingWavs(path)` (sorted ASCIIbetical) and
  `QString stepSample(const QString& path, int dir)` implementing decision 4 (clamping at the ends; empty when the folder has no wavs).
- `BeatSource` (interface): `bool running() const; double bpm() const; double beatsAtSteadyNs(qint64 ns) const; qint64 steadyNsAtBeat(double beat) const;`
  and `class AlsaClockBeatSource : BeatSource` wrapping `AlsaClock` (use `positionBeats()`, `effectiveBpm()`; derive the linear map beat<->steady_clock
  from one `positionBeats()` read plus `steady_clock::now()`, re-sampled on each call, tempo from `effectiveBpm()`). A `FakeBeatSource` for tests.
- Tests: step up/down (ends, empty dir, one file), bpm from file names, acid-chunk BPM from a tiny generated wav (write it in the test), cache hit/miss and
  eviction, `FakeBeatSource` map round trip. Put 3 tiny wav fixtures in `tests/fixtures/sampler/` (generate with a script in the test instead if you prefer; no big files).

On completion run:
```sh
geany-progress done 1 -r src/audio/sample_cache.cpp -r src/audio/wav_info.cpp -r src/audio/beat_source.h -w "The beat map is re-derived on every call from AlsaClock::positionBeats(); check it is cheap enough to call from the audio thread (no locks that the GUI holds)"
```

## Phase 2 - `SamplerEngine`: voices, mixing, scheduled triggers

Files: new `src/audio/sampler_engine.h/.cpp`; `tests/test_sampler_engine.cpp`.

- `struct Trigger { std::shared_ptr<const Sample> sample; qint64 steadyNs; float gain = 1; bool loop = false; double ratio = 1; int tag = 0; qint64 startFrame = 0; /* skip this many frames of the sample (reverse sync arriving late) */ }`.
- `class SamplerEngine : public QObject`: owns the worker thread and `pa_simple` stream (copy the open/reopen/error handling from `AudioPlayer::run()`
  incl. `deviceError` signal marshalled through a timer, and `setDevice()`); `void post(const Trigger&)` (lock-free-ish queue: mutex + small vector, audio
  thread drains it per block); `void stopAll()`, `void stopTag(int)`, `void setMasterGain(float)`.
- Rendering core as a **pure function/class `VoiceMixer`** (no pulse): `render(qint16* out, int frames, qint64 blockStartNs)` : for each pending trigger whose
  `steadyNs` lies in `[blockStartNs, blockStartNs + frames/44100 s)` start a voice at the exact frame offset; sum voices with saturation to int16; voices end at
  the sample end or loop. Triggers whose time already passed by more than 20 ms are dropped, within 20 ms they play immediately. `kOutputLatencyMs` is applied
  by the engine when computing `blockStartNs`, not inside the mixer.
- Tests (headless, drive `VoiceMixer` directly): trigger lands at the exact frame (impulse sample), two overlapping voices sum, clipping saturates, late trigger
  dropped, loop wraps, `stopTag`, `startFrame` > 0 starts part-way into the sample.

On completion run:
```sh
geany-progress done 2 -r src/audio/sampler_engine.cpp -r tests/test_sampler_engine.cpp -w "Pulse latency is a constant (kOutputLatencyMs) that must be tuned by ear"
```

## Phase 3 - modules: Bombomatic, StabPlayer and the scheduler

Files: new `src/audio/sampler_module.h/.cpp` (base + the types); `src/audio/sampler_scheduler.h/.cpp`; `tests/test_sampler_modules.cpp`.

- `SamplerModule` base as in decision 3 with `toVariant()/fromVariant()`. Bombomatic: one trigger on every integer beat. StabPlayer: `bool beats[4]`, trigger
  on the beats of the bar that are on (bar position = `floor(beat) mod 4`, beat counting from the sequencer's start - same convention as `AlsaClock`).
  `offsetMs` is added to the trigger time (negative = early).
- `SamplerScheduler`: a `QThread`-less timer (a worker thread loop, ~5 ms) that looks ahead **100 ms** of beat time, asks each enabled module for triggers in
  `[lastBeat, nowBeat + lookahead)` exactly once (remember the last scheduled beat per module; handle tempo changes and the sequencer restarting - beat
  position going backwards - by resetting), resolves `Sample` through the cache (if not cached yet: request async decode, skip this beat), and `post()`s to the engine.
  The module's `samplePath` is read here (so the up/down chooser applies at the next trigger).
- **One-shot button and reverse sync (decisions 8 and 9)** are part of this phase for `StabPlayer`: fields `bool reverseSync`, the `OneBarState` machine
  (`fireOneBar()` slot-like method, `cancelOneBar()`, `oneBarState()` for the UI), and per-module lookahead (`module.lookaheadMs(sampleDurationMs)`) used by
  the scheduler instead of the fixed 100 ms. Reverse sync schedules at `steadyNsAtBeat(B) - durationNs + offset`, with the late-start rule of decision 9.
  In reverse sync the trigger *beat set* is unchanged (stab on beat 3 means the sample ends on beat 3). The sample's duration comes from `Sample::audio.lengthMs()`
  (stretched copy's length for loops/one-shots later); a sample not yet decoded skips that beat.
- Tests with `FakeBeatSource` and a recording sink (no audio): bombomatic at 120 BPM over 8 s yields 16 triggers, correct times; offset -20 ms shifts them;
  stab on beats {1,3} yields every other beat with correct bar positions; sample change between two beats applies from the next beat; tempo change; sequencer stop/restart; disabled module;
  **one-shot**: click mid-bar -> exactly the stabs of the next bar and nothing after, click while armed cancels, fires with `enabled == false`, sequencer stopped while armed
  cancels; **reverse sync**: a 500 ms sample at 120 BPM stab on beat 1 starts 500 ms before every beat 1 (offset included), a 3 s sample needs the longer lookahead (nothing
  is scheduled late), a trigger already in the past starts part-way (`startFrame`) or is skipped according to the half rule, tempo change updates the start time.

On completion run:
```sh
geany-progress done 3 -r src/audio/sampler_module.cpp -r src/audio/sampler_scheduler.cpp -r tests/test_sampler_modules.cpp -w "Lookahead is 100 ms: an offset more negative than this cannot be honoured (clamp the UI to -80 ms); reverse sync uses a longer per-module lookahead"
```

## Phase 4 - acid loops, one shots, time-stretch

Files: `src/audio/sampler_module.cpp` (+ new types), new `src/audio/sample_stretch.h/.cpp`, extend `sampler_scheduler`, tests `tests/test_sampler_stretch.cpp`.

- `sample_stretch`: `std::shared_ptr<const Sample> stretchedTo(const Sample&, double ratio)` using `TimeStretcher` offline (render the whole thing, same chunk
  loop as `AudioPlayer`), cached by (path, ratio rounded to 0.1 %). Done on a thread-pool job from the scheduler; the unstretched sample plays until the
  stretched one is ready only for one-shots; **acid loops wait** for the stretched copy.
- AcidLoop: the sample is a loop of 4 beats; ratio = `sequencerBpm / sample.bpm`; if bpm unknown, assume the loop is exactly 4 beats long (ratio from length vs the
  beat duration). Starts **quantised** on the next bar start (beat `% 4 == 0`) when enabled; re-triggers every 4 beats (re-sync, never drift); switching off lets the
  current pass finish? **No** - stop at the next beat. A BPM change re-stretches (debounce 300 ms) and swaps in at the next bar.
- AcidLoop also gets the **one-shot button** (decision 8): one pass of the loop starting on the next bar line, not re-triggered, independent of `enabled`; reuse
  the `OneBarState` from phase 3 (move it to the base class if it is not already there). Reverse sync for loops is **not** supported: hide/disable it in the UI.
- OneShot: `quantize` (bool, start at the next beat), `stretch` (bool, only if the BPM is known), `offsetMs`, a "fire" action (button / MIDI later) that
  schedules the trigger (the module is not periodic).
- Tests: stretch ratio and output length for a synthetic loop at 100->120 BPM (+/-0.5 %), acid loop starts on the next bar and repeats each 4 beats with no drift over
  64 beats, acid loop one-shot button plays exactly 4 beats from the next bar and stops, one-shot quantised vs immediate, bpm unknown -> not stretched, cache hit.

On completion run:
```sh
geany-progress done 4 -r src/audio/sample_stretch.cpp -r src/audio/sampler_module.cpp -r tests/test_sampler_stretch.cpp -w "WSOLA artefacts on percussive loops at large ratios; listen to a loop at +-10 %"
```

## Phase 5 - MidiMatic

Files: new `src/midi/midi_out.h/.cpp`, module type in `sampler_module.cpp`, extend `AlsaClock` only if unavoidable; `tests/test_midi_matic.cpp`.

- Output through the existing ALSA client so ports show up with the rest of `vdj-ui`'s ports. **First read `alsa_clock.cpp` / `midi_router.cpp`** and decide:
  give the sampler its **own output port** ("Sampler MIDI") on the same `snd_seq_t` (preferred), with note events **scheduled on the AlsaClock queue by tick**
  (`snd_seq_ev_schedule_tick`, absolute) so ALSA places them precisely, a note-off one 1/4 beat later (like the ADJ example), channel and note configurable.
  If the clock queue cannot be shared, schedule in real time with `snd_seq_ev_schedule_real` from the scheduler's wall-clock trigger time.
- Module fields: `device` (an ALSA client:port from `MidiRouter::outputs()`, subscribe on change via `MidiRouter::connect`), `channel` 1-16, `note` 0-127,
  `velocity`, `beats[4]` (beat chooser), `offsetMs`. The device list refreshes with `MidiRouter::portsChanged`.
- Tests: pure function `buildNoteEvents(beat, module) -> list of (tick, on/off, channel, note, vel)`; check beat chooser, offset to ticks (PPQ 96: 1 tick = 5.2 ms at
  120 BPM, so keep a sub-tick note in the code that offsets finer than a tick are rounded), channel/note ranges clamp. No real ALSA in tests.

On completion run:
```sh
geany-progress done 5 -r src/midi/midi_out.cpp -r src/audio/sampler_module.cpp -r tests/test_midi_matic.cpp -w "Tick resolution is ~5 ms at 120 BPM, so MIDI offsets are quantised to ticks"
```

## Phase 6 - presets and sample-folder model

Files: new `src/audio/sampler_presets.h/.cpp`, `src/ui/sample_tree_model.h/.cpp`; `tests/test_sampler_presets.cpp`.

- `SamplerPresets`: a preset = name + list of modules (`QVariantMap` each, including enabled/name/sample/offset/beats/stab/`reverseSync`/etc.; the one-bar state is transient and **not** saved) + master gain + sample folder;
  stored as one JSON file per preset in `ConfigManager::configDir()/sampler-presets/<name>.json` (use `QJsonDocument`; Qt Core only). API: `list()`, `save(name, state)`,
  `load(name)`, `remove(name)`, `rename`. Sanitise the file name. Also persist the **current** working state (`current.json`) on every change so the next
  start resumes it. Unknown module types / missing keys are skipped with a warning, never fatal; missing sample files stay in the module as "missing".
- `SampleTreeModel`: `QFileSystemModel` subclass or filter (`*.wav` + directories, no empty-dir pruning needed), root = chosen folder (persisted in
  `ConfigManager` `sampler/folder`), a helper `QModelIndex indexForPath`.
- Tests: round trip of every module type, forward compatibility (extra keys ignored), bad JSON, name sanitising, current.json restored.

On completion run:
```sh
geany-progress done 6 -r src/audio/sampler_presets.cpp -r src/ui/sample_tree_model.cpp -r tests/test_sampler_presets.cpp -w "Preset files are plain JSON in the config dir; no migration code yet"
```

## Phase 7 - Sampler panel UI

Files: new `src/ui/sampler_panel.h/.cpp`, `src/ui/sampler_module_widget.h/.cpp`, `src/ui/beat_chooser.h/.cpp`, `src/ui/sample_chooser.h/.cpp`,
`resources.qrc` + the PNGs (decision 5); wire into `src/ui/main_window.{h,cpp}` and `src/CMakeLists.txt`; `tests/test_sampler_ui.cpp` (offscreen, as other widget tests).

- `BeatChooser` : four buttons using the red/yellow/grey PNGs (on/off/disabled); `quint8 mask()`, `setMask`, signal `changed`. Used by StabPlayer and MidiMatic.
- `SampleChooser` : shows the selected sample **file name**, a "..." button opening the chooser (a dialog with the `SampleTreeModel` tree, a **Preview** button that
  plays the highlighted sample once through the engine on the preview tag, and OK), and **up / down arrow buttons** calling `stepSample` (decision 4); the label updates at once,
  the audio at the next trigger.
- `SamplerModuleWidget` per module : name (editable line edit), on/off switch (checkable button), the type-specific controls (beat chooser, sample chooser, offset spin box
  in ms with +/- buttons, range -80..+80, a **"1 bar" one-shot button** on Stab player and Acid loop modules (a `QToolButton` showing `button-green` PNG: idle = green, Armed/Playing = the red PNG (lit/active) or
  green with a blink while armed - pick one and keep it readable; click again cancels; the state is polled from the module with a 50 ms timer), a **"Reverse sync" check box** on the Stab player (tooltip: "The sample ends on the beat instead of starting on it"), MIDI device/channel/note combos, quantize / stretch check boxes), remove button.
- `SamplerPanel`: top row (sample folder button + label, "Add module" menu with the 5 types, preset combo + Save / Delete, master volume, the sequencer-status label
  "Sequencer stopped" / BPM), a scroll area of module widgets, and the tree of the folder on the left side (splitter). Every change pushes to the scheduler
  (modules are mutated on the GUI thread; the scheduler reads copies under a mutex) and writes `current.json`.
- `MainWindow`: create the panel in the same layout slot as `playerPanel_`, a **View -> Sampler** checkable action with persistence (same pattern as the Local Track Player toggle),
  panel destroyed/stopAll when hidden? **No** - hiding the panel keeps the sampler running (it is a performance tool); hiding only hides. Switch-off of a module stops its voices.
- Tests: add/remove modules, toggles reach the module state, beat chooser mask, one-bar button lights and clears, reverse sync check box reaches the module, up/down changes the label, preset save/load restores the UI, panel hidden by the View toggle.

On completion run:
```sh
geany-progress done 7 -r src/ui/sampler_panel.cpp -r src/ui/sampler_module_widget.cpp -r src/ui/main_window.cpp -r resources.qrc -w "Not exercised against real audio hardware; check preview, up/down and timing by ear"
```

## Phase 8 - integration, timing check, docs

- Wire `SamplerEngine`'s device to the same combo as the Local Track Player (or its own combo if simpler; document which) and `refreshDevices()`.
- Add a small **timing self-test mode** (debug panel line or a hidden menu action under the debug mode): log the difference between the scheduled and the rendered trigger
  time (ns) per voice start, so the DJ/dev can verify jitter < 2 ms with `PacketLogger`/session log; and a `kOutputLatencyMs` setting in `ConfigManager` (`sampler/latency_ms`).
- README section "Sampler" (modules, presets, file locations, `.wav` only, the beat comes from the VDJ sequencer, latency setting).
- Run the full test-suite and a clean build with the `build-asan` configuration if it exists (`cd build-asan && make -j8 && ctest`) - the scheduler/engine are threaded.

On completion run:
```sh
geany-progress done 8 -r README.md -r src/audio/sampler_engine.cpp -w "Timing was only verified by the logged scheduled-vs-rendered difference, not with an external measurement"
```

## Open questions (defaults chosen above)

- Beat source is the VDJ sequencer clock. If you want the sampler to follow the **local track player's beat grid** or a **deck's** beats instead (or as a per-module choice),
  that is a new `BeatSource` implementation after phase 3; it does not change the other phases.
- Sampler audio goes to a separate PulseAudio stream (mixed by the server with the track preview). If you need the sampler on a different output than the track player
  (e.g. cue headphones vs master), add its own device combo in phase 7.
