# DR-6S drum machine module for the sampler

Source: `ai-prompts/audio-out.prompt.md`, the "Drum machine" entry. It is one more module type of the existing sampler
(`.planning/plans/sampler_PLAN.md`, all 8 phases done), shown in the UI as **DR-6S**.

What the prompt asks for, as decisions (the numbers in brackets are used in the phases):

1. **Six voices** called BD, SD, SY, CL, HH, OH, each with its own sample, volume (0-127), offset (+/- ms) and 16 step pattern. [D1]
2. **Whole module mute** = the module's normal on/off switch (and its F-key), like every other module. [D2]
3. **Voice buttons** BD SD SY CL HH OH shown horizontally, with on / off SVGs (`img/button-XX-on.svg`, `-off.svg`). They are a **selector**: the voice
   that is "on" is the one being edited, only one at a time. They do not mute a voice. [D3]
4. The edit area (for the selected voice): load a .wav (the usual sample chooser: "..." dialog with preview, up / down arrows), per-voice volume with the
   **mouse wheel** (as before), per-voice offset. [D4]
5. **16 step chooser**: the bar cut in 16 equal parts, 16 round buttons on a line of their own, one pattern per voice. Each step has three states:
   off (yellow), on (red), half volume (pink, `img/button-pink.svg`). Ctrl+left click makes a step half volume. [D5]
6. **Swing** 0-127: the timing algorithm of the prompt (below). [D6] It is a **shared, reusable function** (`src/audio/swing.h`), used by the DR-6S **and by the existing Dembomatik**
   (which also lives on quarter beats): the same swing slider and the same maths, a `swing` setting on both. [D9]
7. **Choke** per voice: a spinner choosing 0, 1, 2, 4, 8 or 16 quarter beats (= 16th steps). The sample is cut off that long after it starts, 0 = no choke (plays
   to its end). The cut is not abrupt: the volume is faded out over 100 ms. [D8] Applies to every hit of the voice, so a ringing open hi-hat can be shortened or a long 808 tamed.
8. **Drum kits**: the DR-6S has its own save / load of "drum-kits" (all six samples, volumes, offsets, patterns, swing), separate from the sampler presets. [D7]

## Ground rules (read before every phase)

- C++17 / Qt (Qt6, Qt5 fallback; Widgets only, no QtSvg, no new Qt modules). Style: 4 spaces, short comments, `QStringLiteral`, `namespace vdj`.
- Build: `cd /home/teknopaul/bzr_workspace/vdj-ui && make -j8` (the tree is built in-source; `cmake .` after adding files). Tests: QtTest executables in `tests/`,
  registered at the end of `tests/CMakeLists.txt` (copy the last block: `add_executable`, `target_link_libraries(... ${QT_TEST} vdj-cdj)`, `add_test`,
  `QT_QPA_PLATFORM=offscreen`). New sources go in the `vdj-cdj` list of `src/CMakeLists.txt` (audio/ and ui/ groups). Run a test directly:
  `QT_QPA_PLATFORM=offscreen ./tests/vdj-ui-<name>-test`. Do not run the GUI. Do not commit.
- Never use `cmake ..` from `build/` (it regenerates the tracked Makefiles); use `cmake .` in the project root.
- Ignore the Inkscape backup files in `img/` (`*.svg.2026_*`); do not put them anywhere.

## Facts about the existing code (verified, do not re-derive)

- `src/audio/sampler_module.h/.cpp`: `ModuleSettings` is one flat struct (name, enabled, samplePath, gain, **volume 0..127**, offsetMs, beatMask, reverseSync,
  quantize, stretch, manualBpm, midi*). `SamplerModule` has `Type { Bombomatic, StabPlayer, AcidLoop, OneShot, MidiMatic, Dembomatik }`,
  `create(type)`, `fromVariant/toVariant` (QVariantMap, key `typeKey()`), `setSettings()` clamps, a mutex, one-bar state (`fireOneBar(nowBeat)`,
  `oneBarState`, `finishOneBar`, `supportsOneBar()`), and `plan(from, to, out)` which applies the one bar gating then calls the virtual
  `collectHits(lo, hi, settings, out)`. `PlannedHit` is `{ double beat; }` (fractional beats are fine: `Dembomatik` already plays 16ths).
  `usesSample()` false means the scheduler does not resolve a sample for the module (MidiMatic).
- `src/audio/sampler_scheduler.cpp`: `tick(nowNs)`; per module: `finishOneBar`, skip when `!enabled && !oneBar`, then `if (!m->usesSample()) { tickMidi(...); continue; }`
  else resolve `cache_->peek(samplePath)` (`requestLoad` when absent), build the window `[from,to)` (`State.scheduledTo`, `State.active`, restart detection),
  `m->plan(from,to,hits)`, then per hit `ns = source_->steadyNsAtBeat(h.beat) + offsetMs`, build `Trigger { sample, steadyNs, gain = s.gain * s.volume/127, tag = m->id() }`
  and `sink_(t)`. **`tickMidi()` is the template to copy for a module that has its own per-hit logic.** `immediateLeadNs_`, `kLookaheadMs = 100`.
- `VoiceMixer` (`voice_mixer.h`): `Trigger { sample, steadyNs, gain, loop, ratio, tag, startFrame }`; a trigger up to 20 ms late still plays; `stopTag`, `stopTagAt`.
- `src/ui/sampler_module_widget.{h,cpp}`: the generic module widget: header row (on button with F-key label, bold name edit, volume slider, type label, remove),
  `SampleChooser`, `BeatChooser`, offset spin etc. created by `if (m.usesSample())`, `if (m.hasBeatChooser())` ... ; `edit(lambda)` writes settings and calls
  `panel_->moduleEdited(id)`; `reloadFromModule()`; **wheel anywhere over the module = module volume** (event filter installed on all children in the constructor).
- `src/ui/beat_chooser.cpp` (4 round `QToolButton`s, 24x24, stylesheet without padding/background, icons from `samplerIcon()`),
  `src/ui/sample_chooser.cpp` (`SampleChooser`: name label, up / down / "..." buttons, `sampleChanged(path)`, `setPreview`, `setFolder`),
  `src/ui/sampler_icons.{h,cpp}` (`enum class SamplerIcon { Red, Yellow, Grey, Green }`, `Q_INIT_RESOURCE(resources)`), `resources.qrc` (aliases `img/button-red.png` ...
  from `img/button-red-24.png`; PNGs made with `inkscape -w 24 -h 24 img/x.svg -o img/x-24.png`).
- `src/ui/sampler_panel.{h,cpp}`: `addModule(type)`, the "Add module" menu is built from a list of types, `relayout()` puts module widgets in a `QGridLayout` (2 or 4
  columns), `applyState/state`, F1..F12 labels set in `relayout()`. `SamplerPresets` (`src/audio/sampler_presets.*`) has `static sanitise(name)` and the JSON
  read / write helpers (`QSaveFile`) - reuse the idea for kits.
- Test patterns to copy: `tests/test_sampler_modules.cpp` (scheduler with `FakeBeatSource` at 120 BPM = 500 ms per beat, `run(sch, fromMs, toMs)` in 5 ms steps,
  `MAKE_SCHED` macro, `writeWav`, `got_` recording sink), `tests/test_sampler_ui.cpp` (a `SamplerPanel` on a temp dir with `setAudioEnabled(false)`),
  `tests/test_sampler_presets.cpp`.
- `img/` has `button-{BD,SD,SY,CL,HH,OH}-{on,off}.svg`, `button-pink.svg`, `button-red/yellow/grey/green.svg`, `button-text.svg` (check what it is in phase 3).
  All 64 px SVGs from Inkscape.

## Swing (D6) - the algorithm to implement

From the prompt, **confirmed by the human with this table** (16 steps of the bar, "y" = the note is delayed):

```
step   01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16
delay   n  y  n  y  n  y  n  y  n  y  n  y  n  y  n  y
```
so every second step is delayed: the even numbered steps, i.e. 0-based indices 1, 3, 5, ... 15 (`index % 2 == 1`): the 2nd AND the 4th sixteenth of every beat, in every beat of the bar.
The amount 0-127: 0 = no delay, **127 = a quarter of a beat** = exactly one step, so at 127 each delayed note lands on the next step's position (the 2nd sixteenth on the half beat, the 4th on the next beat).
A beat has 4 steps (beat positions 0, 0.25, 0.5, 0.75):

```
delayBeats(step, swing) = (step % 2 == 1) ? 0.25 * swing / 127 : 0
```
`/home/teknopaul/bzr_workspace/lmms/src/core/HydrogenSwing.cpp` is only an example (ticks, 48 per beat, it also swings the 2nd and 4th sixteenth, but with a different amount: up to 1/8 beat); nothing from the LMMS code is reused
- the amount and the exact rule here are the human's. (An earlier version of this plan delayed only `step % 4 == 1`; the table above replaces that.)
The swing is a number in the kit / module settings; the delay is added to the hit's beat position before it is converted to time.

**Reuse (D9):** the function lives in its own header `src/audio/swing.h/.cpp`, independent of the drum machine: `double swingDelayBeats(int quarterBeatIndex, int swing)` (the index is the position in quarter beats from the
start of the bar / any multiple of 4 aligned to a beat; only `index % 2` matters) and `constexpr double kSwingMaxDelayBeats = 0.25`. Both `DrumMachine::collectHits` and `Dembomatik::collectHits` call it. The Dembomatik
pattern `X . . X . . X . X . . . . X . .` (steps 0, 3, 6, 8, 13) has two swung hits, the odd steps 3 and 13: at swing 127 step 3 moves from beat 0.75 to 1.0 and step 13 from 3.25 to 3.5; steps 0, 6 and 8 are unchanged. A hit that is delayed by swing
still counts as its grid step for the "one bar" gating.

## Choke (D8) - how it works

- `chokeSteps` of a voice is one of **0, 1, 2, 4, 8, 16** (the spinner steps through exactly these; any other stored value is clamped to the nearest allowed one).
  A step is a quarter of a beat, so the choke length in time is `chokeSteps * (60 / bpm) / 4` seconds at the BPM current when the hit is scheduled (at 120 BPM: 1 = 125 ms, 4 = 500 ms, 16 = 2 s).
- The sample plays from the hit's start; the **fade-out ends at the cut point**: the last `min(100 ms, choke length)` before the cut ramp the gain linearly to 0 and the voice ends there
  (so "cut off at this point" holds: it is silent at the cut). The fade is a linear gain ramp on the voice (per output frame, no clicks). Open question below if the fade should instead *start* at the cut.
- A sample shorter than its choke length is unaffected (it just ends). Choke 0 is today's behaviour.
- It is a property of the *hit* (the trigger carries `cutAfterFrames`, `fadeFrames`), not a tag based stop, so every hit of a voice is cut independently and later hits are not affected.

## Phase 1 - data model: voices, patterns, swing, serialisation (pure, tested)

Files: new `src/audio/swing.h/.cpp` (the shared swing, D9), new `src/audio/drum_pattern.h/.cpp`; edit `src/audio/sampler_module.h/.cpp` (settings + variant); new `tests/test_drum_model.cpp`.

- `drum_pattern.h`: `constexpr int kDrumVoices = 6, kDrumSteps = 16;` `enum DrumStep : quint8 { StepOff = 0, StepOn = 1, StepHalf = 2 };`
  `extern const char* const kDrumVoiceNames[6]` = BD, SD, SY, CL, HH, OH;
  `struct DrumVoice { QString samplePath; int volume = 127; double offsetMs = 0; int chokeSteps = 0; quint8 steps[16] = {0}; }`,
  `constexpr int kDrumChokeValues[6] = { 0, 1, 2, 4, 8, 16 }` and `int drumChokeClamp(int)` (nearest allowed value, negatives -> 0, > 16 -> 16), `int drumChokeNext(int current, int dir)` (the next / previous allowed value, clamped at the ends - used by the spinner) (comparison operator for tests);
  `QString drumPatternToString(const quint8 steps[16])` ("2100 ..." 16 chars of '0' '1' '2'), `bool drumPatternFromString(const QString&, quint8 out[16])` (wrong length / chars -> false, out
  untouched), `float drumStepLevel(quint8 step)` (0, 1.0, 0.5), (no swing function here: use `swingDelayBeats` from `swing.h`, the formula above, `swing` clamped 0..127,
  step taken mod 16; one `constexpr` list of the swung positions inside a beat so the open question is a one line change), `double drumStepBeat(int step)` = step / 4.0.
- `swing.h/.cpp`: `constexpr double kSwingMaxDelayBeats = 0.25; double swingDelayBeats(int quarterBeatIndex, int swing);` (negative indices work: use a proper modulo), `int swingClamp(int)`.
- `ModuleSettings`: add `DrumVoice drums[6]; int swing = 0;` (**`swing` is a general setting, used by the Dembomatik and the drum machine**) (include drum_pattern.h; default constructed). `setSettings()` clamps each voice volume to 0..127, `chokeSteps` with `drumChokeClamp`, offset to +/- `kMaxOffsetMs`,
  swing to 0..127, steps to 0..2.
- `toVariant/fromVariant`: key `"swing"` for the drum machine **and the Dembomatik** (an old Dembomatik preset without the key loads with swing 0), and `"voices"` = list of 6 maps `{ sample, volume, offsetMs, choke, pattern }` (pattern as the string). Missing keys keep defaults; a `voices` list with
  fewer / more than 6 entries uses the first 6; a bad pattern string leaves the voice's pattern empty. Other types keep writing exactly what they write
  today (add `voices` only for the drum machine and `swing` only for the drum machine and the Dembomatik, so old preset files of the other types stay byte-identical).
- Tests (`test_drum_model.cpp`): pattern string round trip and rejects (length 15 / 17, 'x'); `drumStepLevel`; swing (tests in `tests/test_swing.cpp`, or in the same file): swing 0 -> all 0; swing 127 -> the odd steps 1,3,5,...,15 delayed by exactly 0.25 and the even steps 0 (the table of the human: n y n y ...); swing 64 -> 0.25 * 64 / 127; swing -5 and 500 clamp; swing never moves a step past the next step... (step 1 + 0.25 beat = step 2's position: equal, not beyond; step 15 + 0.25 = the next bar's step 0);
  choke clamp (0, 1, 2, 3 -> nearest, 5, 100 -> 16, -1 -> 0) and `drumChokeNext` walking 0,1,2,4,8,16 both ways and stopping at the ends; settings clamping; variant round trip of all six voices (once the type exists in phase 2 add that part there).

On completion run:
```sh
geany-progress done 1 -r src/audio/swing.cpp -r src/audio/drum_pattern.cpp -r src/audio/sampler_module.cpp -r tests/test_drum_model.cpp -w "Swing delays every odd 0-based step (the 2nd and 4th sixteenth of each beat), 127 = a quarter of a beat = one step, as confirmed by the human"
```

## Phase 2 - the mixer's timed fade-out (choke), tested alone

Files: `src/audio/voice_mixer.h/.cpp`; extend `tests/test_sampler_engine.cpp`.

- `Trigger` gets `qint64 cutAfterFrames = 0;` (0 = no cut: play to the end / loop as before) and `qint64 fadeFrames = 4410;` (100 ms at 44.1 kHz; the part before the cut point that ramps down).
  Copy both into the `Voice` (plus `qint64 age = 0` = output frames since the voice started; a voice that starts at frame offset `off` of a block ages from there).
- `mixVoice`: per output frame, if `cutAfter > 0`: `age >= cutAfter` -> the voice ends (mark it finished, do not rely on `pos`);
  else if `age >= cutAfter - fade` -> `gain * (cutAfter - age) / fade` (fade = `min(fadeFrames, cutAfter)`). The sample position advances by `ratio` as now; `age` by 1.
  A voice that ended by a cut must be removed from `voices_` in the same pass as one that ended at the end of its sample (extend the `killed` / finished test; `activeVoices()` must drop).
- `stopTagAt` (hard cut) and `startFrame` (part-way start) must keep working with a cut set: add tests.
- Tests: no cut = unchanged output (the existing tests still pass); cut at 1000 frames, fade 441 -> frames 0..558 at full gain, then linear to 0, frame 999 almost 0, frame 1000 silent and the voice is gone; cut shorter than the fade (cut 100, fade 4410 -> the fade clamps to 100: ramp from frame 0); cut across a block boundary
  (cut at 300 with 256 frame blocks); cut + loop; cut + `startFrame`; two voices with different cuts; a sample shorter than the cut ends by itself; cut 0 and negative are "none".

On completion run:
```sh
geany-progress done 2 -r src/audio/voice_mixer.cpp -r tests/test_sampler_engine.cpp -w "The fade is linear in gain and ends at the cut point"
```

## Phase 3 - the DrumMachine module and its scheduling (headless, tested)

Files: `src/audio/sampler_module.h/.cpp` (new type), `src/audio/sampler_scheduler.h/.cpp`, new tests in `tests/test_drum_machine.cpp`.

- `SamplerModule::Type` gets `DrumMachine` **at the end**; `typeKey` "drums"; `typeName()` "DR-6S"; `create()` / `fromVariant` loop updated (the list of types in `fromVariant`
  and in the panel menu). Class `DrumMachine : SamplerModule`: `usesSample()` false in the sense of "the scheduler must not use `settings().samplePath`" - add a new virtual
  `bool hasVoices() const` (true) and keep `usesSample()` true? **Decision:** add `virtual bool perVoiceSamples() const { return false; }` (true for the drum machine) and make the scheduler
  test it before the generic sample branch; `usesSample()` stays false for it so the generic widget does not build the single sample chooser (phase 5 uses `perVoiceSamples()`).
  `supportsOneBar()` true. `periodic()` true. The module-level `offsetMs` is unused for the drum machine (document it).
- `PlannedHit` gains `int voice = -1; float level = 1.0f;` (other modules leave the defaults).
  `DrumMachine::collectHits(lo, hi, settings, out)`: for every step index k with `lo <= k/4 < hi` (same loop as `Dembomatik`: `k = ceil(lo*4 - 1e-9)` ... , `step = k % 16`),
  for each voice v whose `steps[step] != StepOff`: `out.push_back({ k/4.0 + swingDelayBeats(k, swing), v, drumStepLevel(...) })`.
  **Dembomatik (D9):** change the existing `Dembomatik::collectHits` (`src/audio/sampler_module.cpp`) to `out.push_back({ k/4.0 + swingDelayBeats(int(k), s.swing) })` with the same loop; `swing` 0 must leave every existing Dembomatik test and the
  behaviour byte for byte as it is.
  **Careful:** the swing delay moves a hit past the window it was planned in; the window logic only uses the unshifted grid beat to decide *when to plan it* (a hit is planned once, in the
  window containing its grid position) and the shifted `beat` for the time - that is correct and needs no change in the scheduler window code. The one bar gating in
  `SamplerModule::plan` clamps by the unshifted beat (so a swung hit of the last step of the bar plays slightly after the bar: fine).
- Scheduler: a new `tickDrums(m, s, oneBar, nowBeat, bpm, nowNs)` called like `tickMidi` (when `m->perVoiceSamples()`), sharing the window code (`State`, restart detection, lookahead
  `kLookaheadMs`). For every planned hit: voice `v = s.drums[h.voice]`; skip when `v.samplePath` empty or `v.volume == 0`; `sample = cache_->peek(v.samplePath)` (not decoded: `requestLoad`
  and skip the hit); `ns = source_->steadyNsAtBeat(h.beat) + v.offsetMs * 1e6`; `Trigger { sample, ns, gain = s.gain * (s.volume/127.0) * (v.volume/127.0) * h.level, tag = m->id() }`; **choke:** if `v.chokeSteps > 0`: `t.cutAfterFrames = llround(chokeSteps * (60.0 / bpm) / 4.0 * 44100)` and `t.fadeFrames = 4410` (D8, the BPM is the sequencer's at scheduling time); `sink_(t)`.
  Also pre-load all six sample paths while the module is idle (like the acid loop priming) so the first bar is not skipped: `requestLoad` for every non-empty voice path in `tick`.
  Reuse as much of `tickMidi` as possible - extract the window computation into a small helper used by both (do not duplicate 20 lines).
- Tests (`test_drum_machine.cpp`, copy the helpers of `test_sampler_modules.cpp`): kick on steps {0, 4, 8, 12} -> triggers at beats 0,1,2,3 (0, 500, 1000, 1500 ms at 120 BPM) with the BD sample;
  two voices with different samples on the same step -> two triggers with the right samples; half step -> gain exactly half; **choke**: choke 0 leaves `cutAfterFrames` 0, choke 4 at 120 BPM -> 22050 frames (500 ms), choke 1 at 120 BPM -> 5512 or 5513 frames, at 60 BPM the lengths double, each voice has its own choke, every hit of the pattern carries it; per-voice volume and module volume multiply; voice volume 0 and empty
  sample silent; per-voice offset (-10 ms) shifts only that voice; swing 127: a step-1 hit lands at 0.5 beat (+250 ms at 120 BPM), a step-3 hit at 1.0 beat (+250 ms), steps 0 and 2 are unchanged, and swing 0 changes nothing; swing is per
  beat (step 5 is also shifted); disabled module silent, the one bar button plays exactly one bar of the whole pattern (every voice) and stops; sample not decoded yet is skipped then plays;
  `toVariant/fromVariant` of a configured drum machine (6 voices, swing); the pattern repeats every 16 steps over 4 bars with no drift.
  **Dembomatik swing** (add to `tests/test_sampler_modules.cpp`): swing 0 = the current times (the existing dembow tests unchanged); swing 127: the hits of steps 3 and 13 move (0.75 -> 1.0 and 3.25 -> 3.5 beats, +250 ms at 120 BPM), steps 0, 6 and 8 are unchanged;
  swing 64 -> +0.25 * 64 / 127 beats; the swing survives `toVariant/fromVariant`; the one bar button plays the same five hits with the two swung ones delayed; an old variant without `swing` gives 0.

On completion run:
```sh
geany-progress done 3 -r src/audio/sampler_module.cpp -r src/audio/sampler_scheduler.cpp -r tests/test_drum_machine.cpp -w "tickDrums shares the window code with tickMidi; a swung hit can be planned slightly after its grid position's window - tested"
```

## Phase 4 - the voice and step button images

Files: `img/*.png` (generated), `resources.qrc`, `src/ui/sampler_icons.h/.cpp`, `tests/test_sampler_ui.cpp` (small additions).

- First read `img/button-text.svg` and the BD/SD/... SVGs (`head -30`, sizes) to see what they are (64 px? text on a round button?). Convert with `inkscape -w H -h H img/button-BD-on.svg -o img/button-BD-on-NN.png`
  (`NN` = pixel size). The voice buttons are wider than the round step buttons if the SVG is: keep the SVG's aspect ratio (`-w` only if it is not square - check). A sensible size is 32 px
  high. Convert: the 12 voice SVGs, `button-pink.svg` at 24 px (like red / yellow). If `inkscape` is missing use `convert -background none -resize`.
- `resources.qrc`: aliases `img/drum-BD-on.png` ... `img/drum-OH-off.png`, `img/button-pink.png`.
- `sampler_icons`: `SamplerIcon` gets `Pink`; add `QIcon drumVoiceIcon(int voice, bool on)` (voice 0..5 -> BD SD SY CL HH OH). Keep `Q_INIT_RESOURCE` behaviour.
- Test: all 13 new icons are non-null, on != off for each voice, pink != red != yellow (compare `pixmap().toImage()` like the existing beat chooser test).

On completion run:
```sh
geany-progress done 4 -r resources.qrc -r src/ui/sampler_icons.cpp -w "PNGs are generated from the SVGs with inkscape; regenerate them if the SVGs change"
```

## Phase 5 - the voice selector, the 16 step chooser and the swing slider widgets

Files: new `src/ui/drum_voice_selector.h/.cpp`, `src/ui/step_chooser.h/.cpp`, `src/ui/swing_slider.h/.cpp`; tests in `tests/test_drum_ui.cpp`.

- `DrumVoiceSelector`: six checkable `QToolButton`s with `drumVoiceIcon(v, on)`, horizontal, exclusive (D3): `selected()` 0..5, `setSelected(v)` (no signal), signal `selectedChanged(int)`; clicking the on button keeps it on (one is always on); tooltips BD "Bass drum", SD "Snare", SY "Synth", CL "Clap", HH "Closed hi-hat", OH "Open hi-hat". Same stylesheet as `BeatChooser` (no padding / background, 2 px apart).
- `StepChooser`: 16 `QToolButton`s on one line, 24x24, icons yellow (off) / red (on) / pink (half), with a slightly bigger gap after every 4th button so the beats can be read. API: `QVector<quint8> steps()`, `setSteps(const quint8[16])` (no signal),
  signal `changed(int step, quint8 state)`. Click rules (D5): plain click: off -> on, on -> off, half -> off... **decide and document**: plain click on half makes it on; Ctrl+left click: off -> half, on -> half, half -> off.
  Implement with `mousePressEvent`/`clicked` + `QGuiApplication::keyboardModifiers()` or a small `QToolButton` subclass that reads `event->modifiers()` (do not rely on `toggled`, three states are not checkable).
  Disabled -> grey icons like `BeatChooser`.
- `SwingSlider` (D9, shared by the DR-6S and the Dembomatik): a `QSlider` 0..127 (dragging only; the wheel is not used, it belongs to the volume) with a small "swing 64" label, tooltip "Swing: delays every second sixteenth note,
  127 = a quarter of a beat"; `value()/setValue()` (no signal), signal `valueChanged(int)`; an event filter free widget so the module widget's wheel routing can recognise it (`isSwingSlider(QObject*)` or an object name `swing`).
- Tests (`test_drum_ui.cpp`, offscreen, `QTest::mouseClick` with `Qt::ControlModifier`): selector exclusivity and signals; click cycle of a step (plain and ctrl), `setSteps` does not emit, icons per state, the gap after every 4th button (compare button x positions), `changed(step, state)` arguments.

On completion run:
```sh
geany-progress done 5 -r src/ui/step_chooser.cpp -r src/ui/drum_voice_selector.cpp -r tests/test_drum_ui.cpp -w "Click cycle for the half state is my choice: plain click on pink -> red; ctrl+click toggles pink/off"
```

## Phase 6 - the DR-6S module widget and panel integration

Files: new `src/ui/drum_editor.h/.cpp`; edit `src/ui/sampler_module_widget.h/.cpp`, `src/ui/sampler_panel.cpp`; extend `tests/test_drum_ui.cpp`.

- `DrumEditor : QWidget` (needs the module's `shared_ptr<SamplerModule>` and the panel for the preview / folder / `moduleEdited`): rows
  1. `DrumVoiceSelector` (D3) + the shared `SwingSlider` (D9).
  2. the edit area for the **selected voice**: `SampleChooser` (preview, up / down, "..."), an offset `QSpinBox` (+/- 80 ms), and a volume read-out (`QSlider` 0..127 like the module header's) - **the mouse wheel over the edit area (selector excluded? decide: over the whole editor except the swing slider) changes the selected voice's volume** (D4), 1 per notch, shift 10.
  3. `StepChooser` for the selected voice's pattern on a line of its own (D5).
  The edit area also has the **choke spinner** (D8) for the selected voice: a `QSpinBox` subclass `ChokeSpinBox` (new, in `drum_editor.h/.cpp`) that shows 0, 1, 2, 4, 8, 16 only: `stepBy(n)` uses `drumChokeNext`, `textFromValue` shows the number
  (`0` as "off"), suffix " /4 beat", tooltip "Cut the sample off after this many quarter beats (fades out over 100 ms). 0 = play to the end."; a typed value snaps to the nearest allowed one (`valueFromText` / `fixup`); keyboard up / down and the arrows step through the list.
  Every edit writes the matching `DrumVoice` through the module's `settings()/setSettings()` (copy, change, set) and calls `panel->moduleEdited`. Changing the selected voice reloads the edit area and the step chooser from the settings;
  `reload()` (called by `SamplerModuleWidget::reloadFromModule`) refreshes everything. A voice whose sample has been chosen shows a small marker in its button? (optional, skip if it costs more than a few lines: the sample name shows when selected).
- `SamplerModuleWidget`: when `module->perVoiceSamples()`: do not build the generic sample chooser, beat chooser, offset spin row (the drum machine has no module level offset); build the `DrumEditor` below the header row; keep the on button, name, module volume slider (this is the kit's master volume), remove button, F-key label and the **1 bar** button (green, supportsOneBar).
  Wheel routing: in `SamplerModuleWidget::eventFilter/wheelEvent`, if the watched widget is the `DrumEditor` or one of its descendants and not the swing slider, forward to `DrumEditor::wheelEvent` (voice volume), else module volume as today.
- **Dembomatik swing (D9):** in the generic `SamplerModuleWidget` add a `SwingSlider` to the third row for the Dembomatik type (`module->supportsSwing()`: new virtual, true for Dembomatik and DrumMachine), writing `settings.swing`
  through `edit(...)`, reloaded in `reloadFromModule()`. The wheel routing leaves the swing slider alone (it is dragged). Test: the slider is there for the Dembomatik and not for the other generic modules, moving it reaches the module settings, a preset
  round trip keeps it.
- `SamplerPanel`: add `SamplerModule::DrumMachine` to the "Add module" menu (after Dembomatik); `relayout()`: a module widget that wants the whole row (`SamplerModuleWidget::wantsFullRow()` true for the drum machine, the step chooser needs ~450 px) is placed with `columnSpan = columns` starting at column 0 (flow: finish the current row, place it on a new row, continue on the next row). Update the `gridCell`/`state` helpers and the grid test expectations only where a full row module is present.
- Tests: adding a DR-6S creates the editor, no generic sample chooser; selecting a voice shows its sample / pattern / offset; editing the step chooser changes only that voice's pattern in the module; per-voice sample via up / down (three wavs) and via `setSample`; per-voice volume wheel changes that voice and not the module volume, wheel on the header changes the module volume; swing slider reaches the settings; **choke spinner**: shows off,1,2,4,8,16, steps up and down (arrows, key up / down) and stops at the ends, a typed 3 snaps to the nearest allowed value, is per voice (select another voice -> its own value), reaches the `DrumVoice` in the module; module on / off by F-key and by the button; 1 bar button present; grid: a DR-6S takes a whole row at 2 and 4 across and the modules after it continue below; a preset round trip keeps all six voices, patterns and swing (use `SamplerPanel::savePresetAs / loadPresetByName`).

On completion run:
```sh
geany-progress done 6 -r src/ui/drum_editor.cpp -r src/ui/sampler_module_widget.cpp -r src/ui/sampler_panel.cpp -r tests/test_drum_ui.cpp -w "The DR-6S takes a whole row in the module grid; the module-level offset is not shown (per voice offsets instead)" -w "Wheel over the edit area = voice volume, elsewhere over the module = module (master) volume"
```

## Phase 7 - drum kits (the DR-6S's own presets)

Files: new `src/audio/drum_kits.h/.cpp`; edit `src/ui/sampler_module_widget.cpp` / `drum_editor.cpp` (kit row), `src/ui/sampler_panel.h/.cpp` (kit directory); new `tests/test_drum_kits.cpp`.

- `DrumKits(baseDir)`: files `<baseDir>/drum-kits/<name>.json` (baseDir = the panel's dataDir, same as `SamplerPresets`), JSON `{ "version": 1, "kit": { ...the module's toVariant() map without name / enabled / module volume? } }`.
  **What a kit holds (D8 and D7 of the kit):** the six voices (sample path, volume, offset, **choke**, pattern) and the swing. It does **not** hold the module's name, its on/off state or its master volume (loading a kit must not unmute / rename the module).
  API: `list()`, `exists`, `save(name, DrumKit)`, `load(name, DrumKit*, QString* error)`, `remove`, `rename`; `struct DrumKit { DrumVoice voices[6]; int swing; }` with `drumKitFromSettings(const ModuleSettings&)`, `applyDrumKit(ModuleSettings*, const DrumKit&)`.
  Reuse `SamplerPresets::sanitise` (make it usable from both, it is already `static`); writes with `QSaveFile`; missing keys default; unknown keys ignored; a bad file -> `load` false with a message.
- UI (in the DR-6S widget, a compact row under the header): a kit combo ("(kits)" + names), **Save** (asks a name, prefilled with the current kit name), **Delete**; selecting a kit applies it to this module, updates the editor and calls `moduleEdited`.
  The kit sample paths are stored as given (absolute); missing files show "(missing)" through the existing `SampleChooser`.
- Tests: save / load / list / remove / rename; a kit does not change name / enabled / module volume when applied; round trip of all voices; forward compatibility (extra keys); bad JSON; sanitising; UI: save a kit, change the pattern, load the kit, pattern restored; the kit list is shared between two DR-6S modules; the sampler presets (`savePresetAs`) are independent from the kits.

On completion run:
```sh
geany-progress done 7 -r src/audio/drum_kits.cpp -r src/ui/drum_editor.cpp -r tests/test_drum_kits.cpp -w "Kits store absolute sample paths; moving the sample folder breaks them (shown as missing)"
```

## Phase 8 - integration, timing check, docs

- README "Sampler" section: a DR-6S paragraph (voices, step chooser click rules, swing, kits, F-keys, wheel behaviour, whole row), choke, and a swing sentence in the Dembomatik paragraph.
- Run `ctest` for the whole suite and the ASAN build (`cd build-asan && cmake . && make -j8`, then the new drum tests and the sampler tests with `ASAN_OPTIONS=detect_leaks=0 QT_QPA_PLATFORM=offscreen`).
- Debug-mode timing lines already exist (`SAMPLER module N: rendered ... ms from its scheduled time`): make sure the drum triggers use the module id as `tag` so they show up; add the voice name to the tag? (not needed).
- Performance check on paper: six voices x 16 steps - `plan()` is O(steps in the window), the mixer handles up to a few dozen voices; note in the final report whether `VoiceMixer` has any voice limit (it has none; hi-hats can overlap).
- Open points to report to the human: where the choke fade sits (ends at the cut here), a moving step cursor on the step chooser is not implemented.
  The choke here is a per-voice maximum length; a "closed hi-hat cuts the open hi-hat" link between voices is a different feature and is **not** planned.

On completion run:
```sh
geany-progress done 8 -r README.md -r tests/test_drum_machine.cpp -w "Not heard on real audio: check the swing and the half volume steps by ear" -w "No cross-voice choke (HH cutting OH), no step cursor"
```

## Open questions for the human (defaults chosen above)

- Step click rules: plain click toggles on/off (pink -> red); Ctrl+click toggles half volume. Change if you prefer otherwise.
- Choke: the 100 ms fade ends at the cut point (default). If you meant the fade to start at the cut point and the sound to be gone 100 ms after it, that is one constant in the mixer.
- Choke length follows the BPM when the hit is scheduled (it is not re-computed for a hit that is already queued if the tempo changes).
- Module-level volume (the wheel over the header) acts as the kit's master volume, the per-voice volumes sit under it.
