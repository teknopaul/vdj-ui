# MIDI control of the sampler modules and the DR-6S (Korg nanoKONTROL Studio)

Source: `ai-prompts/audio-module-midi-control.prompt.md`.

A Korg nanoKONTROL Studio has 8 strips; each strip has **mute, solo, rec, select** buttons, a **knob** and a **fader**, and there are track back / forward buttons.
The controller drives the sampler (`.planning/plans/sampler_PLAN.md`, `drum-machine_PLAN.md`, both done) in **one of two modes at a time**:

**Mode "Modules"** - strip n (1..8) controls module n of the sampler panel (the same order as the F1..F12 keys):

| control      | action                                                                                         |
|--------------|------------------------------------------------------------------------------------------------|
| mute         | the module's on / off switch (toggle)                                                          |
| solo         | the green button: 1 bar / Fire (only modules that have one)                                    |
| rec / select | step the module's sample **forward / back** in its current folder (only modules with a sample) |
| knob         | offset: 0 = -40 ms, middle = no offset, 127 = +40 ms                                           |
| fader        | the module's volume 0..127                                                                     |

**Mode "DR-6S"** - strip n (1..6) controls voice n (BD SD SY CL HH OH) of a DR-6S:

| control              | action                                                                     |
|----------------------|----------------------------------------------------------------------------|
| mute                 | the voice on / off (a **new** per-voice mute, see D3)                      |
| solo                 | nothing                                                                    |
| knob                 | the voice's offset, 0 = -40 ms, middle = 0, 127 = +40 ms                   |
| fader                | the voice's volume 0..127                                                  |
| track back / forward | previous / next drum kit (in the kit folder, sorted, from the current kit) |

## Decisions (taken here, change only if a phase finds them impossible)

- **D0 One file format: `.adjmm`.** The controller mapping is **not** a new format. It is the adj map file (`name ch ctrl val`, `#` comments, `*` = any, `N`/`O` = note on / off) with **extra op names** for the sampler; the same file can also hold the deck
  transport ops (`start`, `stop`, `nudge_fwd` ...), and `adj_midilearn` (in the adj project, `references/adj/src/adj_midilearn.c`) writes the same lines. Findings about adj that the plan relies on:
  adj's C parser (`parse_line` in `references/adj/src/adj_midiin.c`) rejects an unknown op name with "syntax error ... invalid name" **but the caller ignores the result**, so a file with sampler lines still works in `adj` (it logs one error per sampler line);
  Phase 7 makes that silent. vdj's own reader (`AdjmmMap::readFromFile`) already skips unknown names silently. `adj_midilearn` has two quirks that Phase 7 fixes: it prints ALSA's **0-based** channel (the map's channels are 1-16, 0 = any), and it writes the learned *value*
  for every control, which is right for buttons (127) but wrong for a knob / fader (must be `*`).
  The op names added for the sampler (all lowercase, adj style): `strip1_mute`, `strip1_solo`, `strip1_rec`, `strip1_select`, `strip1_knob`, `strip1_fader` ... `strip8_*`, plus `track_back`, `track_fwd`, `mode_toggle`.
  Buttons are written with the pressed value (`127`) or `N <note>` for a controller that sends notes; knobs and faders with `*`. Settings that are not a mapping are written as **special comments** `#! key value` (adj ignores any `#` line): `#! source nanoKONTROL`,
  `#! pickup 1`, `#! rec_is_forward 1`. Where the file lives: `midi.conf` gets a new key `sampler_map=<path>` (default `<configDir>/sampler-control.adjmm`); the existing `map=` stays for the decks. One file may be given for both keys.
- **D1 One reader, many listeners.** The controller's events reach the program through the existing `MidiMapInput` ALSA port (`src/midi/midi_map_input.*`, started by `MainWindow::setupMidi`;
  the controller is wired to that port with `aconnect` or the USB/MIDI dialog). `MidiMapInput` gets a second kind of output: a raw `controlEvent(channel, controller, value, sourceName)` signal
  for every controller event (and `noteEvent` for note on / off), besides the existing mapped transport operations. The new `SamplerMidiControl` listens to it and ignores events from sources
  whose ALSA client name does not contain the configured filter (default `nanoKONTROL`), so a deck on the same port does not move faders.
- **D2 No hard coded CC numbers.** What the Studio sends is **not verified**: the numbers live in the `.adjmm` file (D0). An example file `doc/nanokontrol-studio-sampler.adjmm` is shipped with the nanoKONTROL2 style layout as a **guess**
  (faders CC 0-7, knobs 16-23, solo 32-39, mute 48-55, rec 64-71, track back 58, track forward 59, `select` left commented out) and the program writes it to `<configDir>/sampler-control.adjmm` only when that file does not exist.
  **MIDI learn** (a dialog in the program, Phase 6, and `adj_midilearn -s`, Phase 7) fills the file from what the human presses; the existing MIDI monitor (View menu) shows the raw numbers.
  Buttons are momentary: the pressed value (127) is acted on, the release (0) matches nothing and is ignored.
- **D3 Voice mute is new.** `DrumVoice` gets `bool muted` (saved with the module and in sampler presets, **not** in drum kits: applying a kit keeps every voice's mute). Muted voices play nothing.
  The UI shows it: the voice button is dimmed and its tooltip says "muted"; **Alt+click** (or middle click) on a voice button toggles the mute from the mouse too.
- **D4 Mode switch.** A combo in the sampler panel's top row: `Controller: Modules | DR-6S` (saved), plus an optional controller button (`mode_toggle` in the `.adjmm` file, unassigned by default) that toggles it.
  The controller does one mode at a time. With several DR-6S modules the **target** is the one the human last touched (clicked / edited), else the first.
- **D5 Soft takeover ("pickup")**: after a mode change, a preset load or any change made with the mouse, a fader / knob on the controller is **ignored until it reaches the current software value**
  (within 2 steps, or it crosses it), then it follows. This avoids volume jumps on stage. On by default, a checkbox switches it off. State per (mode, strip, control).
- **D6 Value mapping.** volume = value (0..127). offset ms = `round((value - 63.5) * 80 / 127)`: 0 -> -40, 127 -> +40, 63 and 64 -> 0 (whole ms; the offset settings are in ms already, clamp +/-80).
- **D7 Rec / select** (the prompt says "rec/select forward back"): **rec = forward, select = back** (literal pairing of the words). It is one `#! rec_is_forward` line in the `.adjmm` file to swap; flagged to the human.
- **D8 Out of scope:** LED feedback to the controller (lighting the mute buttons: the controller would need a MIDI output connection), multiple scenes, the `select` button doing anything in the
  DR-6S mode (the prompt does not give it a job; a natural use would be to choose the voice shown in the editor - ask the human).

## Ground rules (read before every phase)

- C++17 / Qt (Qt6, Qt5 fallback; Widgets, no new Qt modules). Style: 4 spaces, short comments, `QStringLiteral`, `namespace vdj`.
- Build: `cd /home/teknopaul/bzr_workspace/vdj-ui && make -j8` (in-source tree; `cmake .` after adding files, **never `cmake ..` from `build/`**). Tests: QtTest executables registered at the end of
  `tests/CMakeLists.txt` (copy the last block, `QT_QPA_PLATFORM=offscreen`); run `QT_QPA_PLATFORM=offscreen ./tests/vdj-ui-<name>-test`. Do not run the GUI. Do not commit.
- ALSA events arrive on the `MidiMapInput` reader thread: everything that touches widgets or the module settings must run on the GUI thread (use signals with the default connection, which queues across threads).

## Facts about the existing code (verified, do not re-derive)

- `src/midi/midi_map_input.{h,cpp}`: `MidiMapInput(QObject*)`, `start(mapPath)` loads an `.adjmm` map (returns false when the file cannot be read), a private `Worker` polls the ALSA port and calls `dispatch(ev)`
  for `SND_SEQ_EVENT_CONTROLLER` (channel = `ev->data.control.channel + 1`, `param`, `value`), `NOTEON`, `NOTEOFF`, ...; `report(text, op)` emits `midiEvent(description, op)` only while `monitoring()`.
  `sourceNames()` lists the ALSA clients subscribed to the port. The MIDI monitor dialog (`src/ui/midi_monitor_dialog.*`) shows `midiEvent`. `MainWindow::setupMidi()` (`src/ui/main_window.cpp` ~line 1600)
  creates `midiIn_`, connects its transport signals, reads the map path from the `midi` config section (`/etc/adj/xdj700.adjmm` here). `/etc/adj/korg-nanokontrol.adjmm` is for the *old* nanoKONTROL (transport only).
- Sampler: `SamplerPanel` (`src/ui/sampler_panel.*`) owns `widgets_` (`SamplerModuleWidget*`, the order = F1..F12 order), `scheduler()`, `drumKits()`, `toggleModuleAt(int)`, `addModule`, `applyState`, `state()`.
  `SamplerModuleWidget` has `onButton()`, `oneBarButton()`, `fireButton()`, `sampleChooser()` (`SampleChooser`: `upButton()/downButton()` click = previous / next sample, a private `step(dir)`), `offsetSpin()`, `volumeSlider()`,
  `setVolume(int)`, `module()`, `drumEditor()`. Edits go through `edit(lambda)` -> `module->setSettings` -> `panel->moduleEdited(id)`. `SamplerScheduler::fireOneBar(id)` / `fireOneShot(id)`.
  `DrumEditor` (`src/ui/drum_editor.*`): `selector()` (`DrumVoiceSelector`: `selected()`, `setSelected()`, `button(v)`), `volumeSlider()` (selected voice), `offsetSpin()`, `kitCombo()`, `loadKit(name)`, `saveKitAs`, `refreshKits()`.
  `DrumKits` (`src/audio/drum_kits.*`): `list()` sorted names, `load`, `applyDrumKit(ModuleSettings*, kit)` copies voices + swing.
  `DrumVoice` (`src/audio/drum_pattern.h`): `samplePath, volume, offsetMs, chokeSteps, steps[16]`, `operator==`; `drumVoiceToVariant/FromVariant`; the scheduler's `tickDrums` (`src/audio/sampler_scheduler.cpp`) skips a voice
  with an empty sample or volume 0. `ModuleSettings::offsetMs` is clamped to +/- 80 by `SamplerModule::setSettings`.
- Test patterns: `tests/test_sampler_ui.cpp`, `tests/test_drum_ui.cpp`, `tests/test_drum_kits.cpp` (a `SamplerPanel` on a temp dir with `setAudioEnabled(false)`, `AlsaClock` never opened), `tests/test_midi_matic.cpp`.

## Phase 1 - sampler ops in the .adjmm reader / writer and the mapping maths (pure, tested)

Files: `src/midi/midi_map_input.h/.cpp` (the `AdjmmMap` part only), new `src/midi/sampler_ctl_map.h/.cpp`, new `src/midi/pickup.h`, new `doc/nanokontrol-studio-sampler.adjmm`; new `tests/test_sampler_ctl_map.cpp`.

- `AdjmmMap` keeps `ops[20]` exactly as it is (the numbering matches adj). It gets `std::map<QString, AdjmmOp> named;` for the sampler op names (D0: `stripN_{mute,solo,rec,select,knob,fader}`, `track_back`, `track_fwd`, `mode_toggle`) filled by `parseLine` **before** the "skip unknown names" branch
  (an unknown name that is not a sampler name is still skipped silently), and `QHash<QString, QString> directives;` filled from lines `#! key value` (the existing comment skipping must not swallow them: handle `#!` before the `#` test). Everything else in the parser (channel `*`/0..16,
  controller `N`/`O`/number, value `*` / number) is reused for the named ops. A strip number outside 1..8 is skipped with a warning.
- `sampler_ctl_map.h`: `enum class Control { Mute, Solo, Rec, Select, Knob, Fader }`; `struct ControlAction { enum Kind { None, Strip, TrackBack, TrackForward, ModeToggle } kind; Control control; int strip; int value; }`;
  `class SamplerCtlMap { static SamplerCtlMap fromAdjmm(const AdjmmMap&); ControlAction decodeControl(int channel, int controller, int value) const; ControlAction decodeNote(int channel, int note, int velocity, bool on) const; QString source() const; bool pickup() const; bool recIsForward() const; }`.
  Matching reuses adj's rules (channel 0 = any, `op.value` exact or `*`, notes via `N`): a button op matches only when the value equals the op's value (so the release never matches a `127` op); for an op written with value `*` that is a **button** (user error) a press is `value >= 64`; a knob / fader op matches any value.
  `source()` = `#! source` (default empty = accept every source), `pickup()` = `#! pickup` (default true), `recIsForward()` = `#! rec_is_forward` (default true, D7).
- A **writer** that edits a file without destroying it: `bool writeSamplerOps(const QString& path, const std::map<QString, AdjmmOp>& ops, const QHash<QString,QString>& directives, QString* error)` reads the existing file line by line, **replaces the line of every named op it is given, keeps every other line (transport ops, comments)
  untouched, appends the ops that were not in the file**, and writes with `QSaveFile`; creates the file (with the header comment `# adj midi map` ...) when it does not exist. A learned op is formatted the way adj does (`name  ch  ctrl  val` with `*` for a knob / fader).
- Maths (inline, tested): `int volumeFromValue(int v)` clamp 0..127, `double offsetMsFromValue(int v)` = `round((v - 63.5) * 80 / 127)` (D6), `int valueFromOffsetMs(double ms)`.
- `pickup.h`: `class Pickup { bool filter(int hardwareValue, int softwareValue, int* out); void reset(); void setEnabled(bool); }` - D5: after `reset()` values are swallowed until `abs(hw - sw) <= 2` or the hardware value crossed `sw` between two events, then `*out = hw` until the next `reset()`; disabled passes everything.
- `doc/nanokontrol-studio-sampler.adjmm`: the example (D2) with `#! source nanoKONTROL`, `#! pickup 1`, `#! rec_is_forward 1`, a header explaining that the numbers are a guess and how to learn them.
- Tests: parse of every sampler name for strips 1 and 8, channel `*` / specific, `N` note ops, a mixed file (transport ops + sampler ops) keeps `ops[]` intact and fills `named`, unknown non-sampler names still skipped, a bad strip number skipped, `#!` directives read (and a plain `#` comment still ignored),
  decode of each control (press matches, release does not, analog any value, wrong channel / controller no match), the first match wins when two ops share a controller, defaults of `source / pickup / recIsForward`, the **writer**: replace one op in an existing file keeps the other lines byte for byte, append missing ops, create a new file, a file written
  then re-read gives the same ops, the written file is accepted by vdj's `AdjmmMap::readFromFile` and its transport ops still match as before; maths (0 -> -40, 63 -> 0, 64 -> 0, 127 -> +40, inverse within 1); pickup (swallows far values, catches on equality, catches on a crossing, follows, reset re-arms, disabled passes).

On completion run:
```sh
geany-progress done 1 -r src/midi/midi_map_input.cpp -r src/midi/sampler_ctl_map.cpp -r src/midi/pickup.h -r doc/nanokontrol-studio-sampler.adjmm -r tests/test_sampler_ctl_map.cpp -w "The CC numbers in the example file are a GUESS (nanoKONTROL2 layout), not verified on the Studio; select is commented out" -w "Sampler ops are extra names in the normal .adjmm format; adj itself logs them as unknown names until Phase 7"
```

## Phase 2 - raw controller events out of MidiMapInput, and SamplerMidiControl

Files: `src/midi/midi_map_input.h/.cpp` (small), new `src/midi/sampler_midi_control.h/.cpp`; new `tests/test_sampler_midi_control.cpp`.

- First check that `MidiMapInput::start()` can run **without** a transport map the human wants (the app starts it from the `midi` config's `map=`; if that is empty `setupMidi` may not start it): make sure a start with an
  empty / minimal `.adjmm` is possible (a new `startWithoutMap()` or `start("")` that uses an empty `AdjmmMap`), so the controller works even when no deck map is configured. Keep the existing behaviour otherwise.
- `MidiMapInput`: new signals `controlEvent(int channel, int controller, int value, const QString& source)` (every `SND_SEQ_EVENT_CONTROLLER`, before the map matching, independent of `monitoring()`) and
  `noteEvent(int channel, int note, int velocity, bool on, const QString& source)`. The `source` is the ALSA client name of `ev->source.client` (query with `snd_seq_get_any_client_info`, cache per client id: it must not be
  looked up for every event). Emitted from the reader thread; connect with the default (queued) connection. The transport mapping keeps working unchanged (the existing tests must still pass).
- `SamplerMidiControl : QObject`: holds a `SamplerCtlMap` (Phase 1), `setMap()`, `loadFromFile(path)` (reads the `.adjmm` of D0 with `AdjmmMap::readFromFile` + `SamplerCtlMap::fromAdjmm`), `enabled`, a public slot `void onControlEvent(int channel, int controller, int value, const QString& source)` (filters `source` by `map.source()`,
  case-insensitive substring; empty filter = all) and `onNoteEvent(...)` (notes decode through `decodeNote`), which decode and emit **typed signals**: `buttonPressed(Control control, int strip)` (only presses), `knobMoved(int strip, int value)`, `faderMoved(int strip, int value)`, `trackBack()`, `trackForward()`,
  `modeToggleRequested()`. Also a `learn` hook for Phase 6: while `learning`, every event is reported as `learned(int channel, int controller, bool isNote, int valueOrNote)` instead (and nothing is acted on).
- Tests (inject events by calling `onControlEvent` directly; a `MidiMapInput` instance is not started): each control produces its signal with the right strip; release (0) produces nothing; a wrong source name is ignored, a right one (different case) works;
  an empty filter accepts all; unassigned controls do nothing; learn mode reports and does not act; a changed map applies at once; a note op (`N`) presses a button; loading a real `.adjmm` file from disk (temp file) works. Plus a test that `MidiMapInput` declares the signals (`QSignalSpy` can be created on them; `QMetaMethod` check).

On completion run:
```sh
geany-progress done 2 -r src/midi/midi_map_input.cpp -r src/midi/sampler_midi_control.cpp -r tests/test_sampler_midi_control.cpp -w "Events come from the reader thread: all consumers must be connected with the default queued connection" -w "The ALSA reader itself was not run against a real controller"
```

## Phase 3 - Modules mode: the strips drive the sampler modules

Files: new `src/ui/sampler_controller.h/.cpp` (the glue); small public additions to `src/ui/sampler_module_widget.*`, `src/ui/sample_chooser.*`, `src/ui/sampler_panel.*`; new `tests/test_sampler_controller.cpp`.

- Public helpers on the widgets (so the glue does not click buttons): `SampleChooser::stepSample(int dir)` (what the arrows do, public), `SamplerModuleWidget::setOffsetMs(double)` (writes the module's offset, updates the spin box, no recursion),
  `SamplerModuleWidget::currentVolume()/currentOffsetMs()` (for the pickup), `SamplerModuleWidget::pressGreenButton()` (1 bar or Fire; false when the module has none), `bool SamplerModuleWidget::stepSample(int dir)` (false without a sample chooser),
  `SamplerModuleWidget::toggleOnOff()`.
- `SamplerController : QObject` (owned by the `SamplerPanel`): `enum Mode { Modules, DrumMachine }`, `setMode`, `mode`, `attach(SamplerMidiControl*)`, a `Pickup` per (mode, strip, control kind).
  Modules mode: `buttonPressed(Mute, n)` -> `panel->moduleWidgets()[n-1]->toggleOnOff()`; `Solo` -> `pressGreenButton()`; `Rec` -> `stepSample(+1)` (forward) and `Select` -> `stepSample(-1)` (back), swapped when `map.recIsForward()` is false (D7);
  `faderMoved(n, v)` -> `setVolume(pickup(v, currentVolume))`; `knobMoved(n, v)` -> `setOffsetMs(offsetMsFromValue(...))` for modules that have the generic offset spin (not the DR-6S, which has none: ignore);
  strips with no module are ignored. Pickup is reset when the mode changes, when `SamplerPanel::applyState` runs (preset), and when the module's value is changed by the mouse (`SamplerPanel::moduleEdited` -> `controller->softwareChanged(id)`).
- `SamplerPanel` creates the controller, exposes `controller()`, `setControlMode()`; the top row gets the mode combo (`Controller: Modules | DR-6S`, saved in the panel's state? **no**: in `ConfigManager`-style `sampler.conf` `control_mode`, via the data dir file is fine: keep it as a plain
  `QSettings`-free key in `sampler-current.json` under `"controlMode"` so it comes back at the next start - add the key to `SamplerState`).
- Tests (fake `SamplerMidiControl` events, a `SamplerPanel` with 3 modules: Bombomatic with a sample folder of 3 wavs, StabPlayer, OneShot): mute press toggles module 1 / 2 / 3 and back; solo fires the green button (the Bombomatic gets a one bar armed (check `module->oneBarState`... use `fireOneBar` observation: `oneBarState(beat)`
  needs a running sequencer: assert through a `SamplerScheduler` with a `FakeBeatSource`? simpler: assert `pressGreenButton()` returned true and for the OneShot that the sink got a trigger), rec / select step the sample forward / back and clamp at the ends, a module without a sample ignores rec / select;
  knob 0 / 63 / 64 / 127 -> -40 / 0 / 0 / +40 ms; fader values -> volume (with pickup off); with pickup on: a far fader is ignored until it reaches the software value, then follows; strip 5 with 3 modules does nothing; mode DR-6S ignores the Modules actions;
  swapping `recIsForward`; a preset load re-arms the pickup.

On completion run:
```sh
geany-progress done 3 -r src/ui/sampler_controller.cpp -r src/ui/sampler_module_widget.cpp -r src/ui/sample_chooser.cpp -r tests/test_sampler_controller.cpp -w "Rec = forward, select = back (literal reading of the prompt) - one `#! rec_is_forward` line swaps them" -w "Pickup is on by default: a fader that is far from the on-screen value does nothing until it reaches it"
```

## Phase 4 - per-voice mute of the DR-6S (model, scheduler, UI)

Files: `src/audio/drum_pattern.h/.cpp`, `src/audio/sampler_module.cpp` (variant), `src/audio/drum_kits.cpp`, `src/audio/sampler_scheduler.cpp`, `src/ui/drum_voice_selector.*`, `src/ui/drum_editor.*`; extend `tests/test_drum_model.cpp`, `tests/test_drum_machine.cpp`, `tests/test_drum_ui.cpp`, `tests/test_drum_kits.cpp`.

- `DrumVoice::muted = false`, included in `operator==`; `drumVoiceToVariant` writes `"muted"` only when true (old files stay identical), `drumVoiceFromVariant` reads it (default false). **Drum kits:** `drumKitFromSettings` / `DrumKits::save` do not store `muted`;
  `applyDrumKit` keeps each voice's current `muted` (so loading a kit does not unmute or mute anything); the kit's JSON must not contain `muted`.
- Scheduler `tickDrums`: skip a muted voice (like volume 0). Muting is immediate for the next hits; a voice already ringing finishes (note it).
- UI: `DrumVoiceSelector::setMuted(int voice, bool)` / `muted(int)`: a muted voice button is drawn dimmed (a `QGraphicsOpacityEffect` of 0.35 or a greyed icon) and its tooltip ends with " - muted"; **Alt+click or middle click** on a voice button toggles the mute and emits `muteToggled(int voice, bool muted)`
  (a plain click still selects). `DrumEditor` connects `muteToggled` -> module settings (`drums[v].muted`), `moduleEdited`; `reload()` refreshes the dimming; public `setVoiceMuted(int voice, bool)` and `toggleVoiceMute(int voice)` for the controller.
- Tests: variant round trip with muted true / false and old variant without the key; a muted voice plays nothing while the others do, unmuting plays it again; kit save / load keeps the voices' mute and the JSON has no `muted`; selector dims and the tooltip changes; alt + click toggles without changing the selection; a preset round trip keeps the mutes.

On completion run:
```sh
geany-progress done 4 -r src/audio/drum_pattern.cpp -r src/audio/sampler_scheduler.cpp -r src/ui/drum_voice_selector.cpp -r src/ui/drum_editor.cpp -r tests/test_drum_machine.cpp -w "Voice mute is saved with the module / sampler presets but deliberately not in drum kits" -w "Alt+click or middle click on a voice button toggles its mute; a voice that is already ringing finishes"
```

## Phase 5 - DR-6S mode: strips drive the voices, track buttons step the kits

Files: `src/ui/sampler_controller.cpp` (extend), `src/ui/drum_editor.*`, `src/ui/sampler_panel.*`; extend `tests/test_sampler_controller.cpp`.

- Target: `SamplerPanel::drumTarget()` = the DR-6S widget the human touched last (`moduleEdited(id)` and clicks inside a `SamplerModuleWidget` of a DR-6S set `lastTouchedDrums_` - use the existing `moduleEdited` plus an event filter on focus-in / mouse press of the widget), else the first DR-6S, else null (then DR-6S mode actions do nothing).
- `DrumEditor` public controller helpers: `setVoiceVolume(int voice, int v)`, `voiceVolume(int)`, `setVoiceOffsetMs(int voice, double)`, `voiceOffsetMs(int)`, `toggleVoiceMute(int)` (Phase 4), `stepKit(int dir)`: `DrumKits::list()` sorted (it already is), find the current kit (`kitCombo()->currentText()`, or none),
  go to the next / previous one (clamped at the ends; from "(kits)" forward = the first, back = the last), `loadKit(name)`, update the combo. These change the **stored** voice values even when that voice is not the one shown in the editor (write the module settings, then refresh the widgets if it is the shown voice).
- `SamplerController` DR-6S mode: strips 1..6 -> voices 0..5: `Mute` -> `toggleVoiceMute`, `Solo` / `Rec` / `Select` nothing (D8), `Knob` -> `setVoiceOffsetMs(offsetMsFromValue)`, `Fader` -> `setVoiceVolume` (both through pickup against the voice's current value);
  strips 7, 8 nothing; `trackBack` / `trackForward` -> `stepKit(-1 / +1)` (in Modules mode they do nothing); `modeToggleRequested` flips the mode (the panel combo follows). The pickup is re-armed when the target DR-6S changes, after a kit load and after a mode change.
- Tests: with a DR-6S (voices with samples) and another module: strip 3 mute mutes SY only (`settings().drums[2].muted`), again unmutes; knob / fader on strip 6 write OH's offset / volume (and not the shown voice's when another is selected), pickup applies; strip 7 / 8 ignored; solo / rec / select do nothing; with three kits saved
  ("a", "b", "c"): forward from "(kits)" loads "a", then "b", "c", stays at "c"; back goes down and stops at "a"; the mute flags survive a kit change (Phase 4); no DR-6S -> nothing happens and nothing crashes; the last touched of two DR-6S is the target; mode toggle button flips the mode and the combo.

On completion run:
```sh
geany-progress done 5 -r src/ui/sampler_controller.cpp -r src/ui/drum_editor.cpp -r tests/test_sampler_controller.cpp -w "With several DR-6S modules the controller drives the one touched last (else the first)" -w "Strips 7 and 8, and solo / rec / select, do nothing in the DR-6S mode"
```

## Phase 6 - wiring in the program, controller settings and MIDI learn (in the program)

Files: `src/ui/main_window.cpp/.h` (wire), new `src/ui/controller_dialog.h/.cpp`, `src/ui/sampler_panel.*` (a "Controller..." button); new `tests/test_controller_dialog.cpp`.

- `MainWindow::setupMidi`: create / own a `SamplerMidiControl`; the file is `sampler_map=` of the `midi` config section, default `<configDir>/sampler-control.adjmm`; **if that file does not exist write the example of Phase 1 there (never overwrite an existing file)**;
  `loadFromFile`; connect `midiIn_->controlEvent` / `noteEvent` -> `SamplerMidiControl::onControlEvent / onNoteEvent` and attach it to `samplerPanel_->controller()`. Make sure `midiIn_` runs even without a transport `map=` (Phase 2).
  Status bar message when the `#! source` filter matches no connected client ("sampler controller: no source named nanoKONTROL on the MIDI port - connect it with aconnect or the USB / MIDI dialog").
  `InstrumentManager` has a single `midi_input` device; **check** whether a second device can be wired to the same port (read `src/midi/instrument_manager.*`): if it is a small change add `midi_input_sampler`, otherwise document `aconnect` in the README.
- `ControllerDialog` (button "Controller..." next to the mode combo): the file path (and "Open another .adjmm..."), the `#! source` line edit, the **pickup** and **rec = forward** check boxes, and a table of every sampler op: rows = the six controls + track back / forward / mode toggle, columns = strip 1..8 (each cell shows `ch / CC` or `N note`, "-" for unassigned).
  **Learn:** a "Learn" button per row (or double click a cell) puts `SamplerMidiControl` in learn mode: the next event fills that cell as adj writes it (button -> the pressed value, **knob / fader -> `*`**, a note -> `N <note>`; the channel is written as the human channel 1-16, or 0 when the checkbox "any channel" is on, which is the default);
  "Learn strip 1 and fill the rest" fills strips 2..8 with CC + 1, + 2 ... (editable afterwards). A live line shows the last event received ("ch 1  CC 18 = 64"). **Save** uses the line preserving writer of Phase 1 (the transport lines and comments of the file stay as they are) and applies the map at once. "Reset to the example".
- README: "Controlling the sampler with MIDI": connecting the nanoKONTROL, the two modes and the two tables, pickup, the file format (the extra names, `#!` lines, same file as the deck map if you like), `adj_midilearn`, and that the example numbers are a guess.
- Tests: the dialog learn flow with injected events (learn a cell for a button and for a fader -> `*`, the fill-the-rest shortcut, editing by hand, any-channel on / off writes `0` / the channel), Save writes a file that `AdjmmMap::readFromFile` reads back and **keeps an existing `start` line and comment of the file**, Reset, the check boxes change the `#!` lines;
  the example file is created once and not overwritten; factor the wiring into `wireSamplerControl(MidiMapInput*, SamplerPanel*, SamplerMidiControl*)` so one test connects a `MidiMapInput` (not started) with `emit controlEvent(...)` and sees a module toggle.

On completion run:
```sh
geany-progress done 6 -r src/ui/main_window.cpp -r src/ui/controller_dialog.cpp -r README.md -r tests/test_controller_dialog.cpp -w "Not tried with the real nanoKONTROL Studio: the example CC numbers are a guess, use Controller... > Learn to capture the real ones" -w "No LED feedback to the controller"
```

## Phase 7 - the adj project side: `adj_midilearn -s` and a quiet parser (optional, outside this repository)

`references/adj` is a symlink to the sibling **adj** project: edits there are plain local edits, **no git commands**, and read `references/adj/CLAUDE.md` first if it exists (the earlier plans say so). Do this phase only after the human agrees: it changes another project.

- `references/adj/src/adj_midiin.c` `parse_line`: before the "invalid name" error, accept (and silently ignore) the sampler names of D0 (`strip<1-8>_{mute,solo,rec,select,knob,fader}`, `track_back`, `track_fwd`, `mode_toggle`) and any line starting with `#!`, so a combined file produces no log errors in `adj`.
- `references/adj/src/adj_midilearn.c`: (1) print the **human channel** (`ctrl.channel + 1`) or `0` for any (add `-a` "write any channel", default on, matching the existing maps which all use 0); (2) knobs / faders are written with `*` instead of the learned value
  (names ending `_knob`, `_fader`, and `slider`); (3) new option `-s`: learn the sampler: for strip 1 ask for mute, solo, rec, select, knob, fader, then "Learn the other 7 strips? (y: fill CC + 1 ...)", then track_back, track_fwd, mode_toggle; the output is the same `.adjmm` text (plus the `#! source` line from the `-i` port name);
  (4) buttons that send notes: when the first event is a note on write `N <note>`.
  Keep the current behaviour without `-s` (the transport questions) and keep the output format identical for those lines.
- Check with `make` in the adj project (if it builds there; do not install anything with sudo) and with a tiny shell test that feeds `adj_midiin`'s parser a sample file through the existing test mechanism if one exists, else compile-only. Update `references/adj/readme.md` "Midi Compatibility" with two lines about `-s`.
- Test of compatibility **from this repository**: `tests/test_sampler_ctl_map.cpp` (Phase 1) already reads a file with both kinds of lines; add a test that parses the adj project's own `references/adj/doc` / `/etc/adj/*.adjmm` style files if present (skip when absent).

On completion run:
```sh
geany-progress done 7 -r references/adj/src/adj_midilearn.c -r references/adj/src/adj_midiin.c -w "Edits another project (adj): local edits only, no git; compiled but not run against a controller" -w "adj_midilearn used to write 0-based channels and learned values for knobs: the old behaviour changes"
```

## Phase 8 - integration, timing, docs

- Run the whole suite (`ctest`) and the ASAN build (`cd build-asan && cmake . && make -j8`, then the new tests with `ASAN_OPTIONS=detect_leaks=0 QT_QPA_PLATFORM=offscreen`).
- Review thread safety: `controlEvent` is emitted from the reader thread and delivered queued; `SamplerController` only touches widgets in GUI-thread slots; the `source` name cache in the reader is only used by that thread.
- Latency check on paper: fader events can arrive at ~100 per second; each one writes the module settings (a mutex copy) and may call `moduleEdited` -> `scheduleSave()` (300 ms single shot): fine; make sure a fader sweep does not log or allocate per event beyond that.
- Update the README Sampler section: the green button position, the voice mute (Alt+click), the controller and the `.adjmm` file; list what is not done (LED feedback, scenes, select button in the DR-6S mode).
- Check the compatibility claim once more: the same file with transport ops + sampler ops loads in vdj (both reader keys can point at it) and `adj` still starts with it (if Phase 7 was not done it logs the unknown names but runs).
- Report to the human: the unverified default CC numbers, rec / select direction, the target DR-6S rule, pickup on by default, and the open questions below.

On completion run:
```sh
geany-progress done 8 -r README.md -r src/midi/midi_map_input.cpp -r src/ui/sampler_controller.cpp -w "Real hardware not tested" -w "ASAN clean on the new tests; no ThreadSanitizer run (the reader thread is new code)"
```

## Open questions for the human (defaults chosen above)

- Which CC numbers does your nanoKONTROL Studio send (faders, knobs, mute, solo, rec, select, track back / forward)? The plan guesses the nanoKONTROL2 layout; `adj_midilearn -s` (Phase 7) or the learn dialog (Phase 6) fixes it in a minute, or tell me the numbers from the MIDI monitor.
- May Phase 7 edit the adj project (`adj_midilearn`, its parser)? It is optional; without it the file still works in vdj and `adj` only logs the sampler names as unknown.
- Do you want the sampler lines in the same file as the deck transport lines (one `.adjmm` per controller), or a separate file? Both work (`map=` and `sampler_map=` may name the same file).
- "rec/select forward back": rec = forward and select = back (my literal reading); say if it is the other way round (one setting).
- In the DR-6S mode should the `select` buttons choose which voice is shown in the editor? (The prompt gives them no job; nothing is planned.)
- Pickup (a fader must first reach the on-screen value before it takes over): on by default; say if you prefer faders that jump.
- With more than one DR-6S: the one touched last is controlled. Alternative: the first one only.
- Would you like the controller's LEDs to follow the on / off state (needs a MIDI output connection to the controller)? Not planned.
