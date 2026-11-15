# Studio mix: prepare a whole session from a playlist and render it to one MP3

Source prompt: `ai-prompts/studio-mix.prompt.md`. Builds on the DJ edits work (`doc/trakcrate/track-edits.md`, plan `track-edits_PLAN.md`): the cut / loop regions and
the click avoidance algos of `src/trakcrate/edit_*` are reused for the tracks of a mix.

Goal: a new window for studio mixes. The user picks a playlist, gets an automatic 8 bar cross-fade between every pair of tracks, then edits how each pair is mixed
(mix period, mix type, volume / bass / high curves, cuts and loops, micro grid nudges), previews it with the space bar, saves it as a `.mix` XML file in the
trakcrate database (`mixes/`), and renders it to a 256 kbps VBR `.mp3` with a name, author and optional cover image.

## Findings from reading the code (do not re-derive)

- **Edits to reuse:** `trakcrate::EditRegion / planEdit / renderEdit / outputGrid / validateRegions` (`edit_plan.h`, `edit_render.h`), `EditRenderCache`, `ensureSource`
  (`edit_job.h`). A track of a mix is the original with its regions applied: render it once to PCM (`renderEdit`) and mix from that. `outputGrid()` gives the grid of the
  edited track. The Qt widget `TrakcrateEditPanel` (`src/ui/trakcrate_edit_panel.*`) edits a region list for one track and pushes its changes to a `QUndoStack` itself.
- **Decoding:** `trakcrate::decodeAudio` -> `DecodedPcm` (44100 Hz interleaved stereo float, `audio_decode.h`). A track of 4 minutes is about 85 MB as float; 20 tracks would be
  1.7 GB. Never keep a whole mix decoded: keep the few tracks near the play position (LRU) and, when rendering, work track by track.
- **Writing MP3:** `writeMp3` (`edit_output.cpp`) takes a whole `DecodedPcm` and encodes constant bitrate. The libmp3lame encoder is present; ffmpeg's `libmp3lame` encoder
  supports `abr`/`q` options for variable bitrate and the mp3 muxer writes an attached picture (APIC) from a second stream. A streaming writer is needed for 2 hour mixes.
- **Library:** `trakcrate::Library` (`library.h`): `root()`, `masterDir()`, `playlistsDir()`, `listPlaylists()`, `playlistTracks(name)` (track folders in play order). Mixes
  go in `<root>/mixes/` (a new folder, subfolders make the tree). `Library::create` makes the skeleton; add `mixesDir()` and create the folder lazily (old libraries have none).
- **Analysis:** `trakcrate::loadWaveform(trackDir)` (`analysis.h`, `waveform.bin`, 150 columns/s `pwv5` colour + `pwv3`), `beatGridFrom(track.grid)`; `trakcrateTrackAnalysis(dir)`
  (`src/ui/trakcrate_analysis.cpp`) builds the Qt `TrackAnalysis` (color waveform + beats) the `WaveformWidget` draws. "Analysed" = `waveform.bin` exists and the grid has bpm > 0.
- **LADSPA:** `vdj::LadspaHost::plugins()/find(key)`, `LadspaInstance::create(info)`, `setControl`, `process(float* stereo, frames)` (`src/audio/ladspa_host.h`); `/usr/lib/ladspa`
  has caps (a `Compress` plugin), lsp-plugins, swh-like Ma* ... The core (`src/trakcrate`) is Qt free and cannot hold `LadspaInstance` (it uses `QString`): the engine takes plain
  `std::function<void(float*, int)>` processors and the Qt layer builds them.
- **Stretching today:** `vdj::TimeStretcher` (`src/audio/time_stretcher.*`) is a real time WSOLA over int16 (Qt types, 0.5..2.0): the lowest quality option and not usable in the Qt free core.
  Installed on this machine: the `rubberband` command (Rubber Band 3.3, R2 and R3 engines; `librubberband.so.2` without headers), the `sox` command (`tempo`), `ffmpeg` with the `atempo` and `rubberband` filters
  (libavfilter runtime only, no headers), `libsoundtouch.so.1` and `libSoundTouchDll.so.1` (runtime only), `libsamplerate` WITH headers (resampling only). No `-dev` package for rubberband / soundtouch / libavfilter is installed.
- **Audio out:** `vdj::AudioPlayer` (`src/audio/audio_player.*`) plays a decoded file through PulseAudio simple API from a worker thread. There is no streaming sink that pulls
  frames from a callback; Phase 9 adds one (copy the output half of `AudioPlayer::run`).
- **Windows / menus:** `TrakcrateWindow` (`src/ui/trakcrate_window.cpp`, `setupMenus()`, File menu) and `MainWindow` (`tcPanel_`, view menu ~`main_window.cpp:683`) both show the
  library; both get a "Studio mix..." action. The panel gives the open library (`panel->hasLibrary()`, `panel->library()`).
- **No XML library is linked.** The `.mix` writer / reader is a small hand written subset (elements, attributes, text, escaping) in the core, no Qt, no new dependency.
- **Edit panel behaviours to avoid in mixes:** its Preview / Render / Export loop buttons make no sense there (Phase 8 hides them).
- Keyboard: the main window has an application wide `QShortcut` on Space (sequencer play / pause). `TrakcrateTrackEditor::eventFilter` shows how a widget takes Space
  (accept `ShortcutOverride` and handle the `KeyPress` in an application event filter, only while its window has the focus). Copy that pattern.

## Design (decided here, so the phases do not re-argue it)

**Vocabulary.** A *mix* has an ordered list of *tracks*. Track `i` plays after track `i-1`; neighbouring tracks overlap in a *mix period*.

**Positions are in beats of the track they belong to** (so they stay with the audio when something is moved): beat index `b` of a track's *edited* timeline = `outputGrid(grid)`
beat `b` of the STRETCHED track after its regions were applied (cuts and loops change the length; with no regions it is the stretched grid). Fractional beats allowed. Mix time (ms) is
derived: `trackStartMs[i] + beat * 60000 / masterBpm` (a "do not stretch" track uses its own bpm).

**Master tempo and time stretching (the core of the feature).** A mix has ONE master tempo `masterBpm` (default: the bpm of the first track; the user can change it, decimals allowed). Every track is
time stretched, pitch preserved, so that its beat grid runs at exactly `masterBpm`: `ratio = masterBpm / trackBpm` (> 1 speeds up). From then on all tracks have the same beat length, so beats of one track line up with beats
of the next, mix periods have the same length in beats and in milliseconds on both tracks, and every track's grid is drawn at the master spacing. A track whose ratio is outside 0.5..2 cannot be in the mix; beyond 8% the
UI warns that artefacts are likely. A track may be set to "do not stretch" (it keeps its own tempo, useful for a track that already matches or an ambient intro): its grid is then drawn at its own spacing and mixing it is the DJ's problem.
The stretch is applied to the WHOLE ORIGINAL track first, then cuts and loops are applied to the stretched audio using the grid scaled to the master tempo (`bpm' = masterBpm`, `firstBeat' = firstBeat / ratio`). Region edits therefore never
invalidate the (expensive) stretch.
**The stretch algorithm is pluggable** (Phase 3). A `StretchEngine` has an id, a display name, a description, an availability test, a list of options (name, kind enum / bool / number, default, help) from which the UI builds its
controls, and an OFFLINE `stretch(input PCM, ratio, options, progress, cancel) -> PCM` (studio mixes are rendered offline, so quality beats speed). Engines register in a registry; the mix has a default engine + options and each track
may override them (stored in the `.mix` file). Built-in engines, in order of expected quality: `rubberband-r3` (finest), `rubberband-r2`, `soundtouch`, `sox-tempo`, `ffmpeg-atempo`, `wsola` (our own, always available), and
`varispeed` (libsamplerate resampling: pitch follows tempo like a turntable; best for changes of a few percent, the key moves about 1 semitone per 6%). Engines that need an outside tool (`rubberband`, `sox`, `ffmpeg`) run it as a
subprocess with an argument list (no shell) and report themselves unavailable when it is missing; engines using a library are compiled in only when the development package is found. Anyone can add an engine without
recompiling: a `*.stretch` text file in `<root>/stretchers/` or `~/.config/vdj-ui/stretchers/` defines an *external command engine* (id, name, command template with `{in}`, `{out}`, `{ratio}`, `{bpm_from}`, `{bpm_to}`, optional options).
The result of a stretch is cached on disk (key: track audio hash + engine id + options + ratio), so it is done once per combination, previews are instant, and only one track at a time needs to be in memory.

**Pair (mix period).** For the pair (`i`, `i+1`): `outStartBeat` (beat of track `i`), `inStartBeat` (beat of track `i+1`), `lenBeats` (default 32 = 8 bars), `type` (one of the mix
types). Track `i+1` starts in mix time so that its `inStartBeat` is at the mix time of track `i`'s `outStartBeat`:
`trackStartMs[i+1] = trackStartMs[i] + outStartMs - inStartMs`. Both starts are snapped to bars by default (Shift: beats). The first track starts at 0. Default for a new pair: `outStartBeat` = the last
bar line `lenBeats` before the end of track `i` (clamped >= 0), `inStartBeat` = 0 (the first beat of track `i+1`), type `cross-fade`. The outgoing track may play past the end of the period when its
curves say so (its audio is simply mixed for as long as it has audio and a gain above 0).

**Curves.** Per track three automation curves, each a list of points `{beat, value}` (value 0..100 %), sorted by beat: `volume`, `bass`, `high`. Between points the gain changes linearly
(gain = value/100). Before the first point the first value holds, after the last the last holds. An empty curve means 100 %. A change is "vertical" by putting two points at
the same beat (or closer than 5 ms): the renderer always uses a ramp of at least 5 ms, so there is never a click. Plus a track `trim` (master volume of the track, 0..200 %, default 100).
Curves are stored without any relation to the mix period, so editing a period afterwards does not move them; applying a mix type to a pair *replaces* the curve points of both tracks inside
the window the type writes to (see "Mix types") and leaves the points outside it.

**EQ.** One global pair of crossovers for the whole mix: `bassHz` (default 120) and `highHz` (default 2500) and a `slopeDb` of 12, 24 (default) or 48 dB/octave (Linkwitz-Riley order 2, 4 and 8,
as cascaded biquads). Each track is split into low / mid / high; low is multiplied by the track's `bass` curve, high by the `high` curve, mid by 1; the bands are summed, then `volume` * `trim`.
All three gains are applied per sample (interpolated), so automation never steps.

**Master chain.** An ordered list of *filters* over the summed mix: first the built-in hard knee compressor (default on; threshold -3 dBFS, ratio 10:1, attack 1 ms, release 80 ms, 2 ms look-ahead,
so peaks never exceed 0 dBFS), then any number of LADSPA plugins (key + control values). The user can remove the built-in one, add LADSPA ones, reorder, and bypass each.

**Cuts, loops, grid nudge.** Each mix track has its own `regions` (`EditRegion`, same meaning and validation as an edit) and a `gridNudgeMs` (micro adjustment, default 0). The nudge is applied to the
grid used by that track in this mix only (`firstBeatUs += nudge * 1000`); the track's `beatgrid.txt` is never touched. Because regions and curves are in beats, a nudge moves the cut points and the
curve points with the grid, which is what the DJ wants.

**Mix types** (written into the curves of the pair; `o0 = outStartBeat`, `i0 = inStartBeat`, `L = lenBeats`, `oE = o0 + L`, `iE = i0 + L`, a bar is 4 beats; "vertical" = two points at the same beat).
Values are percent. `out` = outgoing track, `in` = incoming track. A point beyond the track (negative beat, or after its end) is clamped.

| type | out volume | in volume | out bass | in bass |
|------|-----------|-----------|----------|---------|
| `cross-fade` | (o0,100) (oE,0) | (i0,0) (iE,100) | - | - |
| `cross-fade-drop` | as cross-fade | as cross-fade | - | (i0,0) (iE,0) (iE,100)v |
| `bass-drop` | (o0,100) (oE,100) (oE,0)v | (i0,0) (iE,100) | (o0,100) (oE,0) | (i0,0) (iE,0) (iE,100)v |
| `rough-cut` | (o0,100) (oE,100) (oE,0)v | (i0,0) (iE,0) (iE,100)v | - | - |
| `drop-cut` | (o0,100) (oE-4,100) (oE,0) | (i0,0) (iE,0) (iE,100)v | - | - |
| `stop-cut` | (o0,100) (oE-2,100) (oE-1,0) | (i0,0) (iE,0) (iE,100)v | - | - |
| `tail-cut` | (o0,100) (oE,100) (oE+8,0) | (i0,0) (iE,0) (iE,100)v | - | - |
| `bass-switch` | (o0,100) (oE,100) (oE,0)v | (i0-8,0) (i0,100) | (o0,100) (m,100) (m,0)v | (i0-8,0) (m,0) (m,100)v |

`bass-switch`: `m = i0 + 4 * round(L / 8)` for the incoming and `o0 + 4 * round(L / 8)` for the outgoing (halfway, on a bar line). Curves a type does not write are cleared inside the window
`[o0 .. oE+8]` of the outgoing / `[i0-8 .. iE]` of the incoming and reset to 100 % there. Assumptions to confirm with the user when they test: `stop-cut` fades over one beat that ends one beat before
`oE`; `bass-switch` keeps the outgoing at full volume (only its bass is cut) until `oE`; `rough-cut` / `drop-cut` / `stop-cut` / `tail-cut` / `bass-drop` keep the incoming silent until `iE` (the prompt says it
is "set to 100% at the end of the mix-period"), so the mix period of those types is the outgoing track playing alone with the incoming waiting, unless the user draws the incoming volume up.

**Files.** `<root>/mixes/<path>.mix`, `<root>` the trakcrate root; subfolders of `mixes/` are the tree. XML:
```xml
<mix version="1">
  <name>Friday warmup</name><author>Alice</author><image>covers/friday.jpg</image>   <!-- image path: absolute, or relative to the root -->
  <playlist>House</playlist>
  <tempo masterBpm="124"/>
  <stretch engine="rubberband-r3"><option name="formant" value="preserved"/></stretch>      <!-- the mix default; a track may carry its own <stretch> or stretch="off" -->
  <eq bassHz="120" highHz="2500" slopeDb="24"/>
  <filters><filter kind="builtin-compressor" on="1" threshold="-3" ratio="10" attack="1" release="80"/><filter kind="ladspa" plugin="caps.so|Compress" on="1"><control name="..." value="..."/></filter></filters>
  <tracks>
    <track file="track-master-copy/House/Foo/track.mp3" trim="100" gridNudgeMs="0">
      <regions><region kind="cut" start="32" len="16" algo="cross" param="1" .../></regions>
      <curve name="volume"><p b="0" v="100"/>...</curve><curve name="bass">...</curve><curve name="high">...</curve>
      <next outStart="224" inStart="0" len="32" type="cross-fade"/>        <!-- the pair with the following track; absent on the last track -->
    </track>
  </tracks>
</mix>
```
`file` is the audio file of the track, relative to the root (the track folder is its parent). On opening, a file that no longer exists is reported and the mix opens with that track marked missing (not
silently dropped); the mix cannot be rendered until fixed or the track removed. The names of the XML attributes may be chosen while writing the code, but the format is documented in `doc/trakcrate/studio-mix.md`.

**Output.** One `.mp3`, 44.1 kHz stereo, ABR/VBR at an average of 256 kbps (libmp3lame `abr` with bit_rate 256000; if the encoder refuses, `q:a 0`). Tags: title = mix name, artist = author, album =
playlist name, comment "Studio mix", picture = the optional image (jpeg or png, as given). Written to `<name>.mp3.part` and renamed; never overwritten (`_2`).

**Scope limits (v1).** One constant master tempo for the whole mix (no tempo ramps) and a constant tempo per track (the grid is constant); float inside the mix, no dithering; the preview plays from cached
renders and may glitch while a distant track is being stretched or rendered (it prepares the tracks near the play position); no key matching or key shifting.

## Ground rules (read before every phase)

- Version control: the human does git. **Never run git commands**, not even `git rm`. Touch only files a phase names; deleting a file you created yourself is fine.
- Core logic in `src/trakcrate/` (no Qt) and `src/audio/`, so it is unit-testable; GUI code stays thin. New files go in `src/CMakeLists.txt`'s existing lists (minimal edits).
- Build: `cd build && cmake . && make -j8`; tests `ctest -j4` (use `cmake .` inside `build/`, never `cmake ..`). Tests use synthetic audio generated in the test; no big fixtures.
- Source tracks are only ever read. `beatgrid.txt`, `cuemarks.txt` and every other file of a track are never written by the mix code.
- Match the surrounding comment density (comments explain why, one line where possible). New UI classes follow the style of `trakcrate_edit_panel.*`.
- Human steps are marked `USER STEP`: stop, tell the human exactly what to do, and do not run `geany-progress done` until they confirm.

---

---

## Phase 1 - Mix model, timeline maths, mix types (no audio, no files)

Context to load: this plan, `src/trakcrate/edit_plan.h`, `edit_job.h`, `track.h` (BeatGrid), `analysis.h` (`loadWaveform`), `tests/test_trakcrate_edit_plan.cpp` (test style).

Sonnet writes `src/trakcrate/mix_model.{h,cpp}` (namespace `trakcrate`):

1. Types: `CurvePoint {double beat; double value;}`, `Curve {std::vector<CurvePoint> points;}`, `MixType` enum (the 8 types, with `mixTypeName/mixTypeFromName`), `MixPair {double outStartBeat, inStartBeat, lenBeats; MixType type;}`,
   `MixTrack {fs::path file; double trim; bool stretchOff; StretchChoice stretch /* engine id + options; empty = the mix default */; double gridNudgeMs; std::vector<EditRegion> regions; Curve volume, bass, high; MixPair next; bool missing;}`, `MixFilter {kind: BuiltinCompressor|Ladspa; bool on; plugin key; params map<string,double>}`,
   `Mix {name, author, image, playlist; double masterBpm; StretchChoice stretch; double bassHz, highHz; int slopeDb; filters; tracks}` with the defaults from the Design section (`defaultFilters()` = the built-in compressor with threshold -3, ratio 10, attack 1, release 80).
2. Curve helpers: `double curveGain(const Curve&, double beat)` (value/100 linear interpolation, holds before / after, empty = 1, a ramp is at least `minRampBeats` long: the function takes the track bpm and uses 5 ms),
   `void setPoint/movePoint/removePoint(Curve*, ...)` keeping the list sorted and merging nothing (two points at one beat allowed), `bool curveIsDefault(const Curve&)`.
3. Per-track derived data, computed from `{Track dir facts}` passed in as plain values so the code stays testable: `struct TrackFacts {BeatGrid grid; long long frames;}` and
   `EffectiveTrack effectiveTrack(const Mix&, const MixTrack&, const TrackFacts&)` = `ratio = masterBpm / grid.bpm` (1 when `stretchOff`), the nudged grid scaled to the master tempo (`bpm' = masterBpm`, `firstBeatUs' = firstBeatUs / ratio`), stretched length `frames / ratio`, `planEdit(...)` of the regions on that stretched grid (when valid), edited length in frames, `outputGrid()`, effective bpm. Also `double defaultMasterBpm(const std::vector<TrackFacts>&)` (the first track's bpm), `double stretchRatio(...)` and `std::string stretchWarning(ratio)` ("" within 8%, a message up to 2x, an error beyond 0.5..2).
4. Timeline: `std::vector<double> trackStartMs(const Mix&, const std::vector<EffectiveTrack>&)` per the Design (pair anchoring), `double mixLengthMs(...)` = the latest track end (including the tail of the last track), `beatToMixMs(...)`, `mixMsToBeat(...)`.
5. `Mix defaultMix(const std::string& name, const std::vector<MixTrackInput>& tracks)` for a playlist: pairs per the Design (8 bars, cross-fade, outgoing period start = the last bar line `lenBeats` before the end,
   clamped); applies the cross-fade type curves (item 6) to every pair. `struct MixTrackInput {fs::path file; TrackFacts facts;}`.
6. `void applyMixType(Mix*, int pairIndex, MixType, const std::vector<EffectiveTrack>&)`: the table in the Design; replaces the curve points of both tracks inside the windows given there, clamps beats to
   `[0, editedBeats]`, leaves points outside the windows, and sets `pair.type`.
7. `std::string analysedProblem(const fs::path& trackDir)`: "" when `waveform.bin` exists and the grid has bpm > 0, else a message ("not analysed: no beat grid"). Also `bool isAnalysed(trackDir)`.
8. Validation: `std::string validateMix(const Mix&, const std::vector<TrackFacts>&)` (regions valid per `validateRegions`, pairs inside their tracks, lengths > 0, `masterBpm` in 40..300, every ratio within 0.5..2, no missing track, curve values in range).

Tests `tests/test_trakcrate_mix_model.cpp` (new target `vdj-ui-trakcrate-mix-model-test`, add to `tests/CMakeLists.txt` like the edit-plan test): `curveGain` interpolation and holds, a vertical change has a 5 ms ramp, default
mix for 3 tracks of known length gives the expected starts / lengths (track 2 starts at track 1's `outStart`), a cut region shortens the edited length and moves the default `outStart`, every mix type produces exactly the points of the table
(write the table as data in the test and compare), a type applied twice is idempotent, points outside the window survive, `isAnalysed` on a temp track folder, `validateMix` failures, grid nudge shifts the effective grid only, `defaultMasterBpm`, the ratios of tracks at 120 / 126 / 90 bpm for master 124, the stretched grid (`firstBeat'`, length) with a cut region applied on the stretched timeline, `stretchOff` keeps the track's own spacing, ratio warnings and errors.

Verify: build, `ctest -R mix-model`.

On completion:
```sh
geany-progress done 1 -r src/trakcrate/mix_model.h -r src/trakcrate/mix_model.cpp -r tests/test_trakcrate_mix_model.cpp -w "mix type table in the plan has assumptions (stop-cut, bass-switch, silent incoming until the period ends): confirm by ear in phase 9"
```

---

## Phase 2 - The `.mix` XML file and the `mixes/` tree

Context to load: this plan (Files), `src/trakcrate/mix_model.h`, `library.h/.cpp` (`Library::create`, `isSafeName`), `fsutil.h` (`atomicWrite`, `sanitizeName`), `tests/test_trakcrate_edit_output.cpp` (temp library helpers).

Sonnet writes:

1. `src/trakcrate/xml_lite.{h,cpp}`: a tiny XML subset: `struct XmlNode {std::string name; std::map<std::string,std::string> attrs; std::string text; std::vector<XmlNode> children;}`, `std::string xmlWrite(const XmlNode&)`
   (indented, escapes `& < > " '`), `bool xmlParse(const std::string&, XmlNode*, std::string* err)` (prolog and comments skipped, attributes in single or double quotes, entity decoding incl. numeric, errors with a line number; no DTD, no namespaces). Tests: round trip incl. awkward text (`&`, `<`, quotes, unicode, newlines), malformed input gives an error and never crashes (fuzz a few mutations).
2. `src/trakcrate/mix_file.{h,cpp}`: `std::string mixToXml(const Mix&, const fs::path& root)` and `bool mixFromXml(const std::string&, const fs::path& root, Mix*, std::vector<std::string>* warnings, std::string* err)`: the format in the Design; track files stored relative to `root`
   (`lexically_relative`; a file outside the root is stored absolute), `region` attributes for every `EditRegion` field, curves, pair, filters with their params, unknown elements ignored, `version` checked (newer: error), missing files -> `missing=true` + a warning, a `rate` is stored.
   `bool saveMix(const Library&, const std::string& relName, const Mix&, std::string* err)` -> `<root>/mixes/<relName>.mix` atomically (subfolders created; `relName` validated: each component `isSafeName`, no `..`), `bool loadMix(const fs::path& file, const fs::path& root, Mix*, ...)`.
3. `Library`: `fs::path mixesDir() const { return root_ / "mixes"; }` and `Library::create` makes it; opening an old library without it must still work (create on first save).
4. Tree listing: `struct MixTreeEntry {std::string name; fs::path path; bool folder; std::vector<MixTreeEntry> children;}` and `MixTreeEntry listMixes(const Library&)` (folders first, natural sort via `naturalLess`, only `.mix` files, hidden files skipped).
5. `std::string uniqueMixName(const Library&, const std::string& base)` (`base`, `base 2` ...) for "Save as".

Tests `tests/test_trakcrate_mix_file.cpp` (new target): full round trip of a Mix with every field set (compare field by field), file outside the root, XML escapes in names, a missing track warns and keeps the entry, unknown future element ignored, newer version refused, bad names refused (`../x`), tree of nested folders, save is atomic (no `.part` left), library without `mixes/` works.

Verify: build, `ctest -R "xml-lite|mix-file"`.

On completion:
```sh
geany-progress done 2 -r src/trakcrate/mix_file.cpp -r src/trakcrate/xml_lite.cpp -r tests/test_trakcrate_mix_file.cpp -w "the XML attribute names are fixed by this phase: document them in phase 9"
```

---

## Phase 3 - Pluggable time stretch engines and the stretch cache

Context to load: this plan (Design: Master tempo and time stretching), `src/trakcrate/audio_decode.h`, `src/trakcrate/fsutil.h` (`makeTempDir`, `atomicWrite`), `src/trakcrate/edit_output.cpp` (`writeWav`), `src/audio/time_stretcher.cpp` (the WSOLA to port, not to link), `src/CMakeLists.txt` (pkg-config pattern at ~line 250), `tests/test_trakcrate_edit_render.cpp`.

Sonnet writes, in `src/trakcrate/` (no Qt):

1. `stretch_engine.{h,cpp}`:
   - `struct StretchOption {std::string name, label, help; enum Kind {Enum, Bool, Number} kind; std::vector<std::string> choices; double lo, hi; std::string def;}`; `using StretchOptions = std::map<std::string, std::string>`;
   - `class StretchEngine { virtual std::string id() const; name(); description(); std::vector<StretchOption> options() const; bool available(std::string* why) const; virtual bool stretch(const DecodedPcm& in, double ratio, const StretchOptions&, DecodedPcm* out, const StretchProgress& progress, const std::atomic<bool>* cancel, std::string* err) = 0; bool preservesPitch() const; int version() const; }` (`ratio` = target bpm / source bpm: the output is `frames / ratio` long);
   - `class StretchRegistry` (singleton, thread safe): `add(std::unique_ptr<StretchEngine>)`, `find(id)`, `list()` (order of registration, available ones flagged), `registerBuiltins()` called once; `StretchChoice {std::string engine; StretchOptions options;}` and `StretchChoice resolveChoice(...)` (a track choice, else the mix default, else `defaultEngineId()` = the best available of the order in the Design); `std::string choiceKey(const StretchChoice&)` canonical text for cache keys.
2. Engines (`stretch_engines.cpp`, one class each, all offline, all accept ratio 0.5..2, all return exactly `round(frames / ratio)` frames by padding / trimming at the end):
   - `wsola`: float port of the WSOLA of `vdj::TimeStretcher` (Hann window, best splice search), options: window ms (default 46), search ms (10), overlap ms; always available.
   - `varispeed`: libsamplerate (`pkg-config samplerate`, link it to `vdj-trakcrate`; `src_simple` with SRC_SINC_BEST_QUALITY); `preservesPitch() == false`. Option: quality (best / medium / fastest).
   - `rubberband-r3` and `rubberband-r2`: run the `rubberband` command on temp 32 bit float WAVs: `rubberband --tempo <ratio> [--fine for R3] [--formant] [--crisp N] [--threads] in.wav out.wav` (check `rubberband --help` for the exact flags of version 3.3 and write them into the engine; r2 is the faster engine the tool offers); options: formant (shifted / preserved), transients (crisp 0..6 choices), smoothing, window (standard / short / long), channels (apart / together). Available when `rubberband` is on PATH.
   - `sox-tempo`: `sox in.wav out.wav tempo -m <ratio>` (music) with options for the speech / music / linear modes and segment / search / overlap numbers; available when `sox` is on PATH.
   - `ffmpeg-atempo`: `ffmpeg -i in.wav -af atempo=<ratio> out.wav`; `ffmpeg-rubberband`: `-af rubberband=tempo=<ratio>:transients=...:pitchq=...` when the filter exists (`ffmpeg -filters` lists it).
   - `soundtouch`: compiled only when `pkg-config soundtouch` is found at CMake time (`HAVE_SOUNDTOUCH`), using its C++ API (`setTempo`, `setSetting(SETTING_SEQUENCE_MS / SEEKWINDOW_MS / OVERLAP_MS)`); NOT available on this machine (no headers): write it behind the guard, and say in the progress file that it is untested. Same pattern for an in-process `rubberband-lib` engine (`pkg-config rubberband`, `HAVE_RUBBERBAND`; the R3 `RubberBandStretcher::OptionEngineFiner`), preferred over the CLI ones when present.
   - `external`: engines from `*.stretch` files (below).
   Subprocess helper `runCommand(std::vector<std::string> argv, ..., cancel, std::string* err)`: `posix_spawnp`, no shell, stdout / stderr captured to a string, killed on cancel, nonzero exit = error with the tool's last stderr line; the temp WAVs are float32 stereo 44.1 kHz written by a small writer here (`writeWavFloat`, read back with `decodeAudio`).
3. External command engines: `loadExternalEngines(dirs)` reads `*.stretch` (`key: value` lines: `id`, `name`, `description`, `command` = a template split on spaces with double-quote support into argv, placeholders `{in}` `{out}` (32 bit float WAV paths), `{ratio}`, `{bpm_from}`, `{bpm_to}`, `pitch: preserved|follows`, optional `option.<name>: <enum values or number range>` and `{opt:<name>}` placeholders). Search `<root>/stretchers/` and `~/.config/vdj-ui/stretchers/`. A bad file is reported, not fatal. Ship an example file in `doc/trakcrate/` (not installed anywhere).
4. `stretch_cache.{h,cpp}`: `class StretchCache { StretchCache(fs::path dir, uintmax_t maxBytes); std::shared_ptr<DecodedPcm> get(const fs::path& trackDir, const DecodedPcm* source /*may be null: decoded on demand*/, const StretchChoice&, double ratio, progress, cancel, std::string* err); bool has(...); void clear(); uintmax_t size(); }`. Key = SHA-256 of `audio_sha256` (from the track's `meta.txt`, else the file hash) + engine id + `choiceKey` + the ratio to 6 decimals + the engine's `version()`. Files are raw float32 stereo with a small header in `defaultStretchCacheDir()` (`$XDG_CACHE_HOME/vdj-ui/stretch` else `~/.cache/vdj-ui/stretch`), written to a temp name and renamed, least recently used files removed above `maxBytes` (default 4 GB, a setting later). Ratio 1 (within 1e-9) is a plain copy, never an engine call. A concurrent `get` of the same key waits for the first. A cancelled stretch leaves nothing.
5. `stretch_probe.{h,cpp}`: `struct StretchQuality {double durationError; double pitchRatio; double onsetErrorMs; double rmsRatio;}` and `StretchQuality probeEngine(const StretchEngine&, const StretchOptions&, double ratio)` on built-in synthetic material (a 110 Hz tone with a click train at 120 bpm, 12 s): duration, tone frequency by autocorrelation / FFT peak ratio, onsets of the clicks against `i * 60 / (120 * ratio)`, level. Used by the tests and later by the audition tool.

Tests `tests/test_trakcrate_stretch.cpp` (new target `vdj-ui-trakcrate-stretch-test`, links `vdj-trakcrate`; every engine case is skipped with `QSKIP` when its tool is missing, and the skip list is printed): registry (order, find, duplicate id refused, available flags, resolveChoice precedence), every available engine at ratios 0.94, 1.0, 1.05 and 1.5: length within 1 frame of `frames / ratio`, no NaN, peak not above 1.05 of the input's; pitch preserved within 1% for the pitch preserving engines and shifted by the ratio for `varispeed`; click onsets within 15 ms of the expected positions for `wsola` and the rubberband / sox engines, within 5 ms for `varispeed`; cancel stops a subprocess engine and leaves no temp files; an external engine from a `*.stretch` file in a temp dir works, a broken file is reported; the cache: a second `get` does not call the engine (count calls with a fake engine registered for the test), the key changes with engine / options / ratio / audio hash, ratio 1 never calls an engine, LRU eviction under a tiny `maxBytes`, concurrent gets run the engine once, a truncated cache file is detected and redone.

USER STEP (optional, ask once): tell the human that in-process `rubberband` and `soundtouch` engines (better quality, no temp files) need `sudo apt install librubberband-dev libsoundtouch-dev`; the plan works without them through the command line tools. Do not block on the answer.

Verify: build, `ctest -R stretch`; paste the quality table (engine x ratio: duration error, pitch ratio, onset error) into the progress file so the human can see which engines are present and how they compare on synthetic material.

On completion:
```sh
geany-progress done 3 -r src/trakcrate/stretch_engine.h -r src/trakcrate/stretch_engines.cpp -r src/trakcrate/stretch_cache.cpp -r tests/test_trakcrate_stretch.cpp -w "SoundTouch and in-process Rubber Band engines are written behind HAVE_ guards and untested here (no dev headers)" -w "synthetic-material quality numbers say nothing about how music sounds: judge engines with Audition in the UI (phase 7)" -w "the cache can grow to the configured limit (default 4 GB) in ~/.cache/vdj-ui/stretch"
```

---

## Phase 4 - Audio engine: curves, 3 band EQ, summing, compressor

Context to load: this plan (Design: Curves, EQ, Master chain), `src/trakcrate/mix_model.h`, `edit_render.h`, `audio_decode.h`, `tests/test_trakcrate_edit_render.cpp` (synthetic signals).

Sonnet writes, in `src/trakcrate/` (no Qt):

1. `mix_eq.{h,cpp}`: `class CrossoverEq` — stereo, per instance state: `configure(bassHz, highHz, slopeDb, rate)`, `process(const float* in, float* out, frames, const float* bassGain, const float* highGain)` (per-sample gain arrays; low * bassGain + mid + high * highGain).
   Linkwitz-Riley splits (order 2 / 4 / 8 as cascaded butterworth biquads), the high band phase-compensated with an all-pass at the lower crossover so that unity gains sum to a flat response. `reset()`.
2. `mix_compressor.{h,cpp}`: `class HardKneeCompressor` (threshold dBFS, ratio, attack ms, release ms, look-ahead 2 ms; peak detector, gain smoothing, brick wall of the final peaks so the output never exceeds 0 dBFS at the chosen threshold/ratio of 10 and above); `process(float* stereo, frames)`; reports the latency
   (look-ahead frames) so the renderer can compensate.
3. `mix_engine.{h,cpp}`: `class MixEngine` rendering the mix in blocks (`render(long long startFrame, int frames, float* out)`; seeking = any startFrame, the engine warms its filters up for 4096 frames before a jump). It owns:
   - a `TrackSource` interface (abstract, so tests and the UI plug different back-ends): `virtual const DecodedPcm* edited(int trackIndex) = 0;` returns the STRETCHED and edited PCM of a track or null; plus `release(int)`. `TrackCache` implementation: decodes via `ensureSource`, gets the stretched audio from the `StretchCache` of phase 3 (engine = the track's choice or the mix default; a `stretchOff` track skips it), applies `renderEdit` of the track's regions on the stretched, nudged grid, keeps at most N (default 3) tracks, thread safe, `prefetch(trackIndex)` to prepare ahead on a worker.
   - per track: gain arrays from the three curves (`curveGain` per sample, using the track's own bpm) * `trim`, the `CrossoverEq`, summing into the mix buffer at the track's start frame; a track whose gain is 0 over the whole block and has no EQ tail is skipped.
   - the master chain: `std::vector<std::function<void(float*, int)>> processors` (set by the owner) after the built-in compressor (`MixFilter` of kind builtin from the model; the Qt layer adds LADSPA processors in phase 5); `latencyFrames` compensation.
   - `long long totalFrames()`.
4. A helper `std::vector<float> renderMixRange(...)` for tests.

Tests `tests/test_trakcrate_mix_engine.cpp` (new target; keep each fast, tracks of a few seconds at 120 bpm built in memory through a fake `TrackSource`): LR bands with unity gains reproduce the input (error < -60 dB after settling) for all three slopes; bass gain 0 removes a 60 Hz tone (> 40 dB down) and keeps a 1 kHz tone; high gain 0 likewise; a cross-fade of two identical sines keeps the level within 1 dB (linear fades of correlated material sum to 1) and of noise stays within 3 dB; a vertical volume change has no step larger than the 5 ms ramp allows; a track's tail past the mix period is audible when its curve is not 0; the compressor keeps a +6 dBFS input at <= 0 dBFS and leaves -20 dBFS signals within 0.1 dB; the engine output is identical when rendered in one block or in many small blocks, and after a seek (warm-up) it matches the straight render within a small tolerance; a track never read outside its PCM; **tempo matched mix**: two tracks of 120 and 126 bpm mixed at master 123 (the `wsola` and `varispeed` engines, synthetic click trains) have their beats within 1 ms of each other and of the master grid all through a 32 beat mix period, and the pitch of a stretched tone is unchanged for `wsola` (shifted by the ratio for `varispeed`).

Verify: build, `ctest -R mix-engine`.

On completion:
```sh
geany-progress done 4 -r src/trakcrate/mix_engine.cpp -r src/trakcrate/mix_eq.cpp -r src/trakcrate/mix_compressor.cpp -r tests/test_trakcrate_mix_engine.cpp -w "EQ and compressor are tested with synthetic tones only: listen in phase 9" -w "memory: at most 3 edited tracks are cached; a long cut-and-loop edit of one track still copies that track"
```

---

## Phase 5 - Streaming MP3 writer and the render job

Context to load: `src/trakcrate/edit_output.cpp` (`writeMp3`, avformat usage), `mix_engine.h`, `mix_file.h`, `ladspa_host.h`, `fx_rack.cpp` (how `LadspaInstance` is driven from a key + values), this plan (Output).

Sonnet writes:

1. `src/trakcrate/mp3_writer.{h,cpp}`: `class Mp3Writer { bool open(path, kbps, bool vbr, tags, std::string* err); bool write(const float* stereo, size_t frames); bool close(); }` (the existing `writeMp3` is rewritten on top of it for CBR, behaviour and tests unchanged) with ABR/VBR average 256 kbps, optional attached picture (`setPicture(const fs::path& jpegOrPng)`: probe the image with avformat, copy its codec parameters into a second stream with `AV_DISPOSITION_ATTACHED_PIC`, write its packet once), `.part` + rename on close, cancel-safe (`abort()` deletes the part file).
2. `src/trakcrate/mix_render.{h,cpp}`: `struct MixRenderParams {Mix mix; fs::path root; fs::path outFolder; std::function<void(float*,int)> extraProcessors...}`, `bool renderMixToMp3(const Mix&, const fs::path& root, const fs::path& outFile, const MixRenderOptions&, progress callback (stage, done / total), const std::atomic<bool>* cancel, std::string* err)`:
   builds facts for each track (grid from the track folder, frames from a probe), validates (`validateMix`, every track analysed), first makes sure every track's stretch is in the `StretchCache` (stage 1, progress "stretching track k of n", cancellable, cached tracks are free), creates the engine with a `TrackCache`, renders in blocks of 8192 frames, writes tags (title, artist, album = playlist, comment) and the picture, and returns the final path (`uniqueMixOutPath(folder, name)` -> `Name.mp3`, `Name_2.mp3`). `MixRenderOptions` carries the LADSPA processor factory (`std::function<std::vector<std::function<void(float*,int)>>()>`) so the core stays Qt free.
3. In the Qt layer `src/ui/mix_filters.{h,cpp}`: `std::vector<std::function<void(float*, int)>> buildLadspaProcessors(const std::vector<trakcrate::MixFilter>&)` using `LadspaHost::find` + `LadspaInstance::create` + `setControl` (control names from `MixFilter::params`), skipping a plugin that cannot load (and reporting it); `std::vector<LadspaPluginInfo>` helper for the dialog. Add to `src/CMakeLists.txt` (ui list).

Tests `tests/test_trakcrate_mix_render.cpp` (new target, links `vdj-cdj` for LADSPA): the streaming writer on a 3 second signal gives a file `decodeAudio` can read back with a length within 3000 frames, average bitrate within 15% of 256 kbps, tags + picture readable (`probeAudio` finds the title, artist and a picture), `.part` removed, CBR `writeMp3` still passes its old tests; a 3 track mix of 6 second synthetic tracks on a temp library (the tracks imported and given exact grids as in `test_trakcrate_edit_dialogs.cpp`) renders to an mp3 whose length is the planned mix length, whose peaks never exceed 0 dBFS even when the tracks are loud, and which is identical when run twice (determinism); cancel stops and leaves no file; an unanalysed or missing track fails with a message and writes nothing; a LADSPA filter processor is applied when a plugin exists (skip the case when none installed).

Verify: build, `ctest -R "mix-render|edit-output"`.

On completion:
```sh
geany-progress done 5 -r src/trakcrate/mp3_writer.cpp -r src/trakcrate/mix_render.cpp -r src/ui/mix_filters.cpp -r tests/test_trakcrate_mix_render.cpp -w "VBR is libmp3lame ABR at 256k average: confirm the files play on the decks" -w "a 2 hour mix renders track by track but takes minutes: progress and cancel must be wired in the UI (phase 9)"
```

---

## Phase 6 - The studio mix window: playlist, lanes, ruler, zoom, play position

Context to load: `src/ui/waveform_widget.{h,cpp}` (how a waveform is painted from `TrackAnalysis`: `paintWaveform`, colour decoding, `timeX`), `src/ui/trakcrate_analysis.h`, `src/ui/trakcrate_window.cpp` (menus), `src/ui/trakcrate_edit_panel.h` (style), `src/trakcrate/mix_model.h`, this plan.

Sonnet does:

1. `src/ui/studio_mix_window.{h,cpp}`: `class StudioMixWindow : public QMainWindow` — a normal top level window (title, maximize / minimize buttons, remembers geometry in `ConfigManager` section "studiomix"). Constructor takes the `trakcrate::Library*` provider and `ConfigManager*`.
   Menus: File (New from playlist..., Open..., Save (Ctrl+S), Save As..., Render MP3..., Close), Edit (Undo / Redo with Ctrl+Z / Ctrl+Shift+Z, wired to a `QUndoStack`), View (track height: Small 50 px / Medium 100 px / Large 200 px as a radio group, Zoom in / out / fit, remembered). Save / Open / Render are stubs that say "later phase" for now.
   Toolbar: a **Master BPM** read-out (editing it is Phase 7), playlist combo ("New mix from playlist"), height combo, zoom slider. A status bar with the play position and the track under the pointer.
2. "New from playlist": a dialog listing `Library::listPlaylists()` (tree if the library has many) with a track list preview; each track is checked with `isAnalysed`: unanalysed tracks are listed in red with the reason, left out of the mix, and an **Analyse missing** button runs `trakcrate::analyseTrack` on a worker (progress, cancel) and refreshes. OK builds `defaultMix` (phase 1) from the playlist order; refuses a playlist with fewer than 1 analysed track ("studio mixes need analysed tracks").
3. `src/ui/mix_canvas.{h,cpp}`: `class MixCanvas : public QWidget` painting one lane per track on a shared time axis (a vertical `QScrollBar` / `QScrollArea` for many tracks, a horizontal scroll + wheel zoom around the pointer, Ctrl+wheel horizontal zoom, plain wheel vertical scroll, a view span from 20 s to the whole mix, `viewChanged` signal):
   - ruler across the top (always visible, not scrolled away) with ticks in `mins:secs` (`mmm:ss`, 120 minutes shows as `120:00`), tick spacing adapts to the zoom;
   - lane `i` starts at `trackStartMs[i]` (`MixModel` derived data from phase 1) and is as long as the stretched, edited track; the label shows the track's bpm, the master bpm and the stretch ("124.0 <- 126.0, -1.6%", orange beyond 8%, red when impossible) and a "stretching..." marker while its stretch is not cached yet (the lane is drawn at once from the waveform scaled by the ratio); a lane label (track number and title, bpm) drawn at its left edge;
   - waveform of the *edited* track (cuts removed, loops repeated) from the colour waveform: add `std::vector<...> editedWaveform(const Waveform&/TrackAnalysis, const EditPlan&, double ratio)` in `src/ui/` or core (maps plan segments to 150 col/s columns, time scaled by the stretch ratio) and draw it like `WaveformWidget::paintWaveformSharp`, scaled to the lane height;
   - beat grid of the effective (nudged) grid drawn over each lane: bar lines (red, full height) and beat lines (white, half height), skipping lines closer than 3 px as the widget does;
   - the play position: a green vertical line over all lanes; a click on the ruler or on any lane sets it (`playPositionChanged(ms)`); drawn on top.
   - lane height from the View setting; the lanes are laid out with a 4 px gap; the canvas size follows content.
   - public API for the owner: `setMix(const Mix*, TrackInfos)`, `setPlayPosition`, `setLaneHeight`, `zoomAt/fit`, `trackAt(y)`, `msAt(x)`, and signals for what phases 7/8 need (declared now, emitted later).
4. Entry points: a "Studio mix..." action in `TrakcrateWindow::setupMenus()` (File menu) and in `MainWindow` next to the trakcrate library action; both create / raise one `StudioMixWindow` (non modal, owned by the parent) giving it the library of their `TrakcratePanel`. Without an open library the action is disabled with a tooltip.
5. Add all new files to `src/CMakeLists.txt` and a test target.

Tests `tests/test_trakcrate_mix_ui.cpp` (new target, offscreen like the other UI tests): window opens with the normal window flags (not `Qt::Dialog`, has min / max hints), the playlist dialog excludes unanalysed tracks and says why, a 3 track playlist makes 3 lanes with the expected lane order and starts, the ruler label for 120 minutes is `120:00`, the three heights give 50 / 100 / 200 px lanes, zoom changes the span and clamps, a click sets the play position, scrolling vertically shows the later lanes, the edited waveform of a track with a cut is shorter by the cut, `grab()` of the canvas differs when a track is added (smoke).

Verify: build, `ctest -R mix-ui`; open the window by hand (note in the progress file what was seen).

On completion:
```sh
geany-progress done 6 -r src/ui/studio_mix_window.cpp -r src/ui/mix_canvas.cpp -r tests/test_trakcrate_mix_ui.cpp -w "lane layout and waveform are tested offscreen only: check them by eye on a real playlist" -w "very long mixes: the canvas must only paint what is visible (check scrolling with 30 tracks)"
```

---

## Phase 7 - Editing the mix: periods, mix types, curves, EQ, grid nudge, filters, undo

Context to load: `src/ui/mix_canvas.h`, `studio_mix_window.h`, `src/trakcrate/mix_model.h`, `src/ui/waveform_widget.cpp` (mouse handling pattern, `paintEditRegions` look), this plan (Design).

Sonnet does:

1. **Undo.** One `QUndoStack` in the window. `MixSnapshotCommand(window, before, after, text, mergeKey)` replaces the whole `Mix` (cheap; the document is small) and refreshes the canvas; `mergeWith` for the same mergeKey (a drag, a slider). Every change below goes through `StudioMixWindow::edit(text, std::function<void(Mix&)>, mergeKey)`. Ctrl+Z / Ctrl+Shift+Z in the Edit menu (ApplicationShortcut conflicts: only the window's own shortcuts, window scope).
2. **Mix period** (canvas): for each pair draw start and end lines on both lanes and a 10% opacity overlay on both (outgoing coloured differently from incoming); the period must be the same length on both. Dragging the start line on the outgoing lane changes `outStartBeat` (the incoming track slides with it: its start is derived), dragging the start line on the incoming lane changes `inStartBeat` (the incoming slides the other way), dragging an end line changes `lenBeats` on both. All snap to bars (Shift: beats), clamp to the tracks, show the value ("bar 57, 8 bars") while dragging, one undo step per drag. Warn (label on the pair) when the two bpm differ by more than 2%.
3. **Mix type**: a context menu / small combo on each pair's overlay with the 8 types; choosing one calls `applyMixType` (the curves change and are redrawn) and is undoable.
4. **Curves**: per lane a yellow line for the `volume` curve with a dot on every point, over the waveform; a lane toolbar (or key 1/2/3) selects which curve is being edited (volume yellow, bass red, high cyan; the other two drawn faint). Mouse: click on the line adds a point (value from the y position, 0 at the lane bottom, 100 at the top; beat snapped to the grid with a modifier for free), drag a dot moves it (both axes; Shift locks the value, Ctrl locks the beat), double click or Delete removes it, a dot dragged onto the same beat as another gives a vertical change. The value shows as a tooltip while dragging. Per lane also a **master volume (trim)** slider / spin box in the lane label (0..200 %).
5. **Global EQ and master filters** (a dock or dialog): bass crossover Hz, high crossover Hz, slope 12/24/48 (spin boxes / combo); a filter list with add LADSPA (chooser of `LadspaHost::plugins()` with search), remove, bypass, up / down, controls as spin boxes with the plugin's ranges, and the built-in compressor with its four numbers. All undoable. Keep it in `src/ui/mix_filters_dialog.{h,cpp}`.
6. **Micro grid adjust** per track: in the lane label buttons -1 / -0.1 / +0.1 / +1 ms (and a drag with a modifier on the lane is not needed) changing `gridNudgeMs`, shown as "+3.4 ms", with a reset; the grid lines, regions and curves follow (positions are in beats); never written to the track's files (test it).
7. **Master tempo and stretch.** A **Master BPM** spin box in the toolbar (40..300, 0.01 steps, a button "use track N's bpm"); changing it is one undo step, recomputes every ratio, lane length and start (beats-based curves, regions and periods stay where they are in beats), and queues the stretches in the background (a `StretchJobQueue` on worker threads filling the `StretchCache`, per-lane progress, a global cancel). A **Stretch** dock: the mix default engine (combo of the registry showing availability, the description as tooltip, the engine's options built from its schema), a per-track override ("mix default / engine X / do not stretch") with its options, the ratio and warning of the selected track, **Re-stretch**, and **Audition...**: stretches an excerpt (the selected pair's mix period plus 4 bars either side, at most 40 s) with the engines the user ticks and plays them one after the other or A/B (temp wavs through an `AudioPlayer`; names hidden until the end when "blind" is ticked) so engines can be compared by ear on the user's own material. Engine and option changes are undoable (the cache itself is not).
8. The lane **selected** state (click on the label) is kept for phase 8 (edit panel) and for Delete.

Tests: extend `tests/test_trakcrate_mix_ui.cpp` (or a new `test_trakcrate_mix_edit.cpp`): dragging each of the lines changes the model as specified and the incoming track moves; period lengths stay equal; undo / redo of each kind of edit restores the model exactly (compare snapshots), a drag is a single undo step; applying a mix type then undoing restores the curves; adding / moving / removing curve points via the mouse; a vertical change; trim range; EQ and filter edits; grid nudge changes the effective grid and `beatgrid.txt` bytes stay identical (snapshot a temp track folder before / after); the bpm warning.

Verify: build, `ctest -R "mix-ui|mix-edit"`.

On completion:
```sh
geany-progress done 7 -r src/ui/mix_canvas.cpp -r src/ui/studio_mix_window.cpp -r src/ui/mix_filters_dialog.cpp -r tests/test_trakcrate_mix_edit.cpp -w "mouse interaction is tested with synthetic events; use it on a real mix and report anything awkward" -w "snapshot undo copies the whole mix per step (small, but merge drags)"
```

---

## Phase 8 - Cuts and loops per track, save / open, the mixes tree

Context to load: `src/ui/trakcrate_edit_panel.{h,cpp}` (the whole file: `pushRegions`, `applyRegions`, `EditRegionsCommand`, `previewRequested`...), `studio_mix_window.cpp`, `mix_file.h`, `library.h`, this plan.

Sonnet does:

1. **Cuts and loops.** Reuse `TrakcrateEditPanel` as the editor of the selected lane's regions, in a dock on the right of the window. Give the panel an *external* mode (small, backwards compatible change in `trakcrate_edit_panel.*`): `setExternalUndo(bool)` / a signal `regionsEdited(before, after, selectedAfter, text, mergeable)` emitted instead of pushing to its own `QUndoStack` when set, and `setRegions(const std::vector<EditRegion>&, int selected)` to load a lane's list without creating history; `setMixMode(true)` hides the Preview / Render / Export loop buttons and the dragged-range label wiring that needs a waveform of its own. Existing editor behaviour and tests must stay unchanged (`ctest -R "edit-ui|edit-dialogs"` stays green).
   The canvas gets the same range selection as the editor (a drag in the "edit regions" mode of the selected lane, snapped to the lane's beats, calls `panel->setRange(startMs, endMs)` in the *track's source time*: convert from the lane's edited timeline back to the source with `outputToSourceMs` (`edit_job.h`)); regions are drawn on the lane as 25% bands (dark grey cuts, yellow loops, like the editor) over the *original* waveform positions? No: the lane shows the EDITED waveform, so draw the regions in a thin strip at the lane bottom instead and keep the full band only while the lane is in region mode (then show the original waveform with bands, edited otherwise). The region edit goes through `StudioMixWindow::edit` (phase 7), so Ctrl+Z covers it and the lane re-renders from `planEdit`. Overlapping with the track's mix period or curves is allowed: curves and periods are in edited beats and are *not* shifted by an edit; after a region edit that changes the length, clamp the pair / curve points of that track into the new range and tell the user in the status bar.
2. **Save / Open.** Ctrl+S: first save asks for a name (a dialog with the name, author and image fields prefilled from the mix; the mix name defaults to the playlist name) and saves `mixes/<name>.mix` through `saveMix`; later saves overwrite the same file silently; the title bar shows the name and a `*` while unsaved (clean state from the undo stack index). **Save As...** with a name that may include a folder (`Folder/Name`). **Open...** with a `QFileDialog` starting in `mixes/` (filter `*.mix`, also accepts a file outside the library); missing tracks are reported in a message listing them and shown as red "missing" lanes. Closing the window with unsaved changes asks (Save / Discard / Cancel).
3. **The tree of mixes** (a dock on the left): `QTreeWidget` of `listMixes(library)` (folders and mixes, refreshed after a save, a refresh button), double click opens (asking about unsaved changes), context menu: Open, Rename (file), Delete (asks), New folder. Never touches anything outside `mixes/`.
4. A mix remembers its source playlist (`<playlist>`); "New from playlist" fills it. Tracks in the file are paths relative to the root (phase 2).

Tests: extend the mix UI tests: selecting a lane and a range creates a cut through the panel, the lane gets shorter, the later tracks move, undo restores; the panel's existing tests unchanged; Ctrl+S saves `mixes/<name>.mix` (temp library), reopening gives an identical model; Save As with a folder; Open with a missing track shows it as missing; the tree lists nested folders and opens a mix; unsaved-changes prompt (inject the answer); the title `*`; the track folder files unchanged after all of it.

Verify: build, `ctest -R "mix|edit"`.

On completion:
```sh
geany-progress done 8 -r src/ui/trakcrate_edit_panel.cpp -r src/ui/studio_mix_window.cpp -r src/ui/mix_tree_dock.cpp -w "the edit panel gained an external mode: its own editor tests must still pass" -w "regions + curves in edited beats: a length-changing region edit moves audio under existing curve points (they are clamped, not shifted)"
```

---

## Phase 9 - Preview playback, render dialog, listening test

Context to load: `src/audio/audio_player.cpp` (`run()`: the PulseAudio simple API part), `mix_engine.h`, `mix_render.h`, `studio_mix_window.cpp`, `trakcrate_track_editor.cpp` (`eventFilter` for Space, `popOut` not needed), `src/ui/trakcrate_edit_render_dialog.cpp` (dialog + worker thread style).

Sonnet does:

1. `src/audio/stream_player.{h,cpp}`: `class StreamPlayer : public QObject` — opens a PulseAudio simple stream (stereo s16 or float32, 44100) on the default device and runs a thread pulling frames from `std::function<void(float*, int)> fill` in blocks of 2048; `play()`, `stop()`, `isPlaying()`, `positionFrames()` (frames handed to the device minus the device latency), a `finished()`/`failed(QString)` signal; thread safe `seek` through the owner (the owner changes what `fill` returns). Model it on the output half of `AudioPlayer::run`.
2. `src/ui/mix_previewer.{h,cpp}`: owns a `TrackCache`, a `MixEngine` (copy of the current mix, rebuilt when the mix changes) and a `StreamPlayer`; `toggle(playFromMs)`: starts at the canvas play position (or stops); prefetches the tracks around the play position on a worker (stretch from the cache, or stretching on demand, then regions) so the stream does not wait on a render (the tracks needed within the next 60 s, rendering the next one when the play position enters the last quarter of the current), underruns produce silence plus a status message instead of blocking; emits `positionChanged(ms)` ~30 times a second which moves the green line; stops at the end of the mix; any edit of the mix while playing stops the preview (as the track editor does) or, better, swaps in the new engine at the next block with the same position (simple version first: stop).
   Space toggles the preview: copy the track editor's application event filter pattern, active only while this window is the active window and the focus is not in a text field; Home goes to 0.
3. **Render MP3...** dialog (`src/ui/mix_render_dialog.{h,cpp}`): fields name (default the mix name), author (default the remembered edit author, `EditPrefs`), optional image (file chooser with a thumbnail; jpeg / png), output folder (default the folder remembered for mixes, else Music), the planned file name and duration, a warning list (missing / unanalysed tracks, bpm differences > 2%, a mix period beyond a track end), a progress bar with percent and the time done, **Cancel**, and a final "Saved ... (size, duration)". The render runs `renderMixToMp3` on a worker thread with the LADSPA factory of `mix_filters.cpp`. The name, author and image are stored in the mix (so Ctrl+S keeps them).
4. Docs: `doc/trakcrate/studio-mix.md` — the window, the lanes, mix periods, the eight mix types (the table of the Design with the assumptions called out), curves, EQ, filters, cuts and loops, grid nudge (not saved to the track), the `.mix` XML format with an example and every attribute, the `mixes/` folder, rendering and the output tags, limits (tempo, memory). Link it from `doc/trakcrate/gui.md` and `track-edits.md`.
5. The docs also cover the master tempo, the stretch engines (a table of what each is good for, how to install the `rubberband` / `sox` / `soundtouch` packages and dev packages, how to write a `*.stretch` external engine file) and the cache location and how to clear it.
6. Run the full `ctest -j4`; list failures that predate this plan separately.

Tests: `StreamPlayer` with an injected fake sink (the class takes a sink factory so tests need no sound server) — frames pulled in order, stop works, position advances; `MixPreviewer` plays from a position (fake sink) and the reported positions are monotonic, an edit stops it, an underrun yields silence not a hang; the render dialog (offscreen): defaults, warnings, a short render to a temp folder creates the mp3 with the tags and picture, cancel leaves nothing, name clash gives `_2`, the mix stores name / author / image.

USER STEP: the human opens a real playlist of 4 or more analysed tracks, listens to the automatic mix, tries each of the eight mix types on a pair, mixes tracks of clearly different tempo (for example 120 and 128 bpm) with every available stretch engine using Audition and picks the default engine, drags a mix period, draws a bass cut with the bass curve, makes a cut and a loop in one track, saves with Ctrl+S, reopens from the tree, renders the mp3 and plays it on a deck. Collect what is wrong (the assumed mix type shapes, stretch artefacts per engine and per ratio, clicks, preview underruns, EQ crossover sound, compressor pumping) and fix before completing. Do not run `geany-progress done` until the human confirms.

On completion:
```sh
geany-progress done 9 -r src/audio/stream_player.cpp -r src/ui/mix_previewer.cpp -r src/ui/mix_render_dialog.cpp -r doc/trakcrate/studio-mix.md -w "listening test result recorded in the progress file" -w "preview of long mixes depends on pre-rendering the next track: report any gap when a track starts"
```
