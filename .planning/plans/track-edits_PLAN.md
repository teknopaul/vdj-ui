# Track edits: cut / loop DJ edits and acid loops from the trakcrate track editor

Source prompt: `ai-prompts/making-track-edits.prompt.md`.

Goal: in the trakcrate track editor the user marks bar ranges of a gridded track as **cut** (removed, dark grey) or **loop** (repeated, yellow), each with a
click-avoidance algo and parameters; previews the whole result; then renders a **new** `.wav` / `.mp3` (`<name>_<user>_edit.<ext>` + a `.txt` info file).
A second flow exports a 4-beat loop of the track as a `.wav` for Acid. The source audio is never modified.

## Findings from reading the code (do not re-derive)

- **Decode:** `trakcrate::decodeAudio(path, DecodedPcm*)` (`src/trakcrate/audio_decode.h`, ffmpeg, no Qt) gives the whole track as interleaved stereo float at
  44100 Hz. Use it as the edit source. The Qt side `vdj::decodeAudioFile` (int16) is not needed.
- **Encode:** ffmpeg (`libavformat/libavcodec/libswresample/libavutil`) is already linked to `vdj-trakcrate` (`src/CMakeLists.txt:250`). `libmp3lame` and
  `pcm_s16le` encoders are present on this machine. No tag writing library exists; `src/nfs/id3_tags.*` only *reads*. Tags are written through the ffmpeg muxer's
  metadata dictionary (the mp3 muxer writes ID3v2 incl. `TBPM`, `TKEY`).
- **WAV tempo:** `vdj::wavBpmFromFile()` (`src/audio/wav_info.h`) reads the RIFF `acid` chunk tempo. The ffmpeg wav muxer cannot write `acid`, so the `.wav` writer
  is hand-written (fmt, data, `acid`, `LIST/INFO`). Use `wavBpmFromFile` to verify the round trip in tests.
- **Grid:** `BeatGrid {bpm, firstBeatUs}` (`src/trakcrate/track.h:46`), `beatGridFrom(track.grid)`; beat i is at `firstBeatUs/1000 + i*60000/bpm` ms, constant tempo only.
  `cuemark_edit.h` has `snapMs`, `beatOf`; downbeat = beat index multiple of 4 from beat 1 (`Snap::Bar` in `snapMs`).
- **Editor:** `TrakcrateTrackEditor` (`src/ui/trakcrate_track_editor.{h,cpp}`) owns a `WaveformWidget* wave_`, a checkable `editMarks_` button, `snapBox_`, a
  `QListWidget* markList_`, a `QUndoStack* undo_` and one writer per file (`applyMarks`, `applyGrid`). The waveform paints editable marks in `paintCuemarks()`
  (`waveform_widget.cpp:~233`) and loops as 25% bands (`paintCues`). Edit-mode toggles are mutually exclusive (see `gridEdit_` / `editMarks_` lines ~194-197).
- **Pure-function pattern:** edit logic lives in `src/trakcrate/*.{h,cpp}` without Qt and is tested by `tests/test_trakcrate_*.cpp` registered in
  `tests/CMakeLists.txt` (copy the `vdj-ui-trakcrate-cuemarks-test` block, ~line 363). Follow it.
- **Library API:** `trakcrate::Library` (`library.h`): `masterDir()`, `listTracks(folder)`, `createTrackDir`, `playlistTracks(name)`, `hasPlaylist(name)`, `addToPlaylist(name, trackDir, position)` (position 0 = first, inserts and renumbers).
  `importFolder` (`importer.h`) copies/moves a folder of audio into a master folder with tags and artwork extracted, with `ImportOptions.folder / noPlaylist / dedupe / analyse`. `analyseTrack(trackDir, AnalyseOptions)` (`analysis.h`) writes beatgrid, waveform and loudness.
  The editor knows the original's track dir (`dir_`); the library root is held by the trakcrate panel (`trakcrate_panel.cpp:389`): find how the editor can get the open `Library` (add a setter, like `setGridConfirmer`).
- **Preview playback:** `vdj::AudioPlayer` (`src/audio/audio_player.h`: `load(path)`, `play()`, `seekMs`, state) plays a file. Preview = render to a temp `.wav`
  and load it into a *separate* AudioPlayer inside a small dialog, so the panel's deck/track is untouched.
- **No existing UI-preference store was found by grep for `QSettings`.** Phase 3 must find how the app persists its own prefs (look in `src/config/` and
  how `quantize_settings.*` / the sampler folder are stored) and reuse that, not invent a new mechanism.

## Design (decided here, so phases do not re-argue it)

**Everything is a `join`.** The output is a list of source ranges `[srcStartFrame, srcEndFrame)` played in order. The seam between two consecutive ranges is a
`Join {outPoint=e, inPoint=s, algo, param}` meaning "audio up to source frame e continues into audio from source frame s".
- A **cut** of `[a,b)` is the join `e=a -> s=b`.
- Each **loop repeat** of `[a,b)` is the join `e=b -> s=a` (a loop played T times = T-1 repeats; "times" is the total number of plays, 2..16).
- So there is one DSP routine per algo, shared by cuts, loops and acid loops. All positions are source frames computed from the grid:
  `frame(beatIndex) = round((firstBeatUs/1e6 + beatIndex*60/bpm) * 44100)`, always from the grid (never accumulated), so there is no drift.

**Regions** are in whole beats, so cut and join points follow the grid exactly:
`Region {kind: Cut|Loop, startBeat:int, lenBeats:int, algo, algoParam, ...kind params}`. Regions may not overlap or touch-overlap (a bar is cut or looped, not both).
Cuts are whole bars (`lenBeats % 4 == 0`) but may start on any beat only through `earlycut`; loops are 1,2,4,8,16 bars (`lenBeats` 4,8,16,32,64) starting on any beat.

**Algos** (`param` in brackets). `N` beats = `N*60/bpm` s. Fades are equal-power for crossfades, linear for fadeout/cut fades.
- `cut` [fade ms: 5, 10, 20, 50]: raw join; fade-out of the last `ms` before `e`, fade-in of the first `ms` from `s`.
- `cross` [beats: 0.5, 1, 2]: pre-roll crossfade. Over the N beats before the join, `src[e-N..e]` fades out while `src[s-N..s]` fades in; then play on from `s`.
  Length and bar alignment are unchanged. (Alternative "post-roll" mixing `src[e..e+N]` was rejected: it drags cut material into the kept part; that is `tailoverlap`.)
- `fadeout` [beats: 0.5, 1, 2]: linear fade to silence over the last N beats before `e`; the next range starts hard with a 5 ms fade-in (the beat drops in).
- `tailoverlap` [beats: 0, 1, 2, 4] (cuts only): as `cut` (20 ms fades), plus `src[e..e+K]` (the first K beats *of the removed range*, i.e. the reverb tail) mixed on top of
  the start of the next range with a linear fade-out over K beats. K=0 equals `cut`. K is clamped so it stays inside the removed range.
- `earlycut` [shift beats: 1, 2, 3; direction: before|after; join algo: cut|cross] (cuts only): the bar-aligned removed range `[a,b)` is moved to `[a-shift, b-shift)` ("before")
  or `[a+shift, b+shift)` ("after"); the length stays whole bars so the track stays in time. The join then uses `cut` or `cross` with its own param.
  (Resolved to a plain cut by `resolveRegions()`; the renderer never sees `earlycut`.)
- Loops accept only `cut`, `cross`, `fadeout`.

**Output grid:** same bpm; beat 1 is the output time of the first kept source beat >= source beat 1 (cuts/loops at the very start shift it). Provide `outputGrid()`.

**Where an edit goes (decided by the user):** an edit becomes a **new track in the library**, in the **same master folder as the original track**
(`track-master-copy/<folder>/<new track>/`, `folder` = the name of the original track dir's parent). It is then **analysed automatically** so it can be exported to a pendrive,
and if a **playlist with exactly the same name as that folder** exists and contains the original track, the edit is inserted into it **immediately before the original** (at the original's index).
Rendering always goes to a temp `.wav`/`.mp3` first; only an accepted render is imported. The original track dir is never touched.
- Import: `importFolder` has no single-file entry point, so stage the rendered file (+ the info `.txt`) in a fresh temp dir and call `importFolder(lib, tempDir, opts)` with `opts.mode=Move`, `opts.folder=<folder>`,
  `opts.noPlaylist=true`, `opts.dedupe=false`, `opts.analyse=false`; the report's `items[0].trackDir` is the new track. (If that is awkward, extract a small `importFile` from `importer.cpp`; the report type must not change.)
- Analysis: the grid of the edit is *known* (`outputGrid()`), so do not detect it. Run `analyseTrack(trackDir, {beatgrid=false, waveform=true})` (waveform + loudness), then write `beatgrid.txt` from the output grid with origin `edited`
  (so a later re-analyse leaves it alone). Cuemarks are not carried over.
- Playlist: `playlistTracks(folder)` -> find the original's index `i` (compare canonical paths) -> `addToPlaylist(folder, newTrackDir, i)`. No such playlist, or the original is not in it: do nothing and say so in the status line (never create a playlist).
- The info `.txt` is kept in the new track dir as `edit.txt` (copy it in after import if the importer does not keep it).
- **Optional file-only output** (for sharing the audio elsewhere, and for acid loops): saved to a chosen folder as `<original stem>_<user>_edit.<ext>` (+ `.txt`), never overwriting (`_2`, `_3`).
  That folder defaults to the folder the original file was imported from (see `source.txt`; check which key `importer.cpp` writes) when it still exists, else the Music folder (`QStandardPaths::MusicLocation`), else home; remembered after the first choice.
The library track's folder name comes from `createTrackDir` (it appends " 2" on clashes), its audio file is named `<original stem>_<user>_edit.<ext>`. `<user>` = the remembered "edit author" pref, default `$USER` (lower-case in file names, Capitalised in the title).
**Titles:** strip a trailing ` (Original Mix)` (case-insensitive), then append ` (<User> Edit)`. Artist untouched. For acid loops the suffix is ` (<name> <User> Edit)`, name asked in the save dialog.

**Out of scope:** variable-tempo grids; moving cuemarks into the edit; artwork copy into the output; saving the edit list inside the
master (the list is in memory + the `.txt` info file; reloading from the `.txt` is a possible later step); non-4/4 bars.

## Ground rules (read before every phase)

- Version control: the human does git. **Never run git commands.** Touch only files a phase names.
- Core logic in `src/trakcrate/` (no Qt) so it is unit-testable; GUI code stays thin. New files are added to `src/CMakeLists.txt` in the existing `vdj-trakcrate` list; minimal edits.
- Build: `cd build && cmake . && make -j8`; tests `ctest` (use `cmake .` inside `build/`, never `cmake ..`). Test fixtures are synthetic (generate PCM in the test); no big audio files.
- The audio file of the source track is only ever read. Output paths are always new files.
- Match the surrounding comment density (comments explain why, one line where possible).
- Human steps are marked `USER STEP`: stop, tell the human exactly what to do, and do not run `geany-progress done` until they confirm.

---

## Phase 1 - Edit model and segment planning (no audio)

Context to load: this plan, `src/trakcrate/track.h` (BeatGrid), `src/trakcrate/cuemark_edit.{h,cpp}` (style, `snapMs`), `tests/test_trakcrate_cuemarks.cpp` (test style).

Sonnet writes `src/trakcrate/edit_plan.{h,cpp}` (namespace `trakcrate`):

1. Types: `enum class EditKind {Cut, Loop}`, `enum class EditAlgo {Cut, Cross, Fadeout, TailOverlap, EarlyCut}`, `struct EditRegion` (kind, startBeat, lenBeats, algo, algoParam
   (ms for Cut, beats otherwise), loopTimes (2..16), earlyShift (1..3), earlyAfter (bool), earlyJoinAlgo (Cut|Cross), earlyJoinParam), `struct EditJoin {long long outFrame, inFrame; EditAlgo algo; double param;}`,
   `struct EditSegment {long long start, end;}` (source frames), `struct EditPlan {std::vector<EditSegment> segments; std::vector<EditJoin> joins;}` (`joins.size() == segments.size()-1`).
2. `long long beatFrame(const BeatGrid&, double beatIndex, int rate = 44100)` and `double beatFrames(const BeatGrid&, double beats)`.
3. `std::string validateRegions(const BeatGrid&, const std::vector<EditRegion>&, long long totalFrames)`: empty string = OK, otherwise a human message. Rules: grid present, regions inside
   the track, no overlap (sort first), cut `lenBeats % 4 == 0`, loop lengths in {4,8,16,32,64}, loop algos only Cut/Cross/Fadeout, TailOverlap/EarlyCut only for cuts, legal params, `earlycut` shifted
   range still inside the track and not overlapping neighbours.
4. `std::vector<EditRegion> resolveRegions(...)`: applies earlycut (see Design) and returns plain cuts; sorted by start.
5. `EditPlan planEdit(const BeatGrid&, std::vector<EditRegion> regions, long long totalFrames)`: builds segments and joins from the validated, resolved, sorted regions. Loop `[a,b)` x T becomes segment
   `[prev, b)`, then `T-1` copies of segment `[a,b)` joined `b->a`, then the following content; a cut ends a segment at `a` and the next begins at `b`.
6. `BeatGrid outputGrid(const BeatGrid& in, const EditPlan&, int rate = 44100)` and `long long planOutputFrames(const EditPlan&)`.
7. Beat-count helpers for the UI: `int barsToBeats(int bars)`, `kLoopBars[] = {1,2,4,8,16}`, `kCutFadeMs[] = {5,10,20,50}`, `kCrossBeats[] = {0.5,1,2}`, `kTailBeats[] = {0,1,2,4}`, `kEarlyShift[] = {1,2,3}`.

Tests `tests/test_trakcrate_edit_plan.cpp` (new target `vdj-ui-trakcrate-edit-plan-test`): beatFrame exactness at 128 bpm and 123.4 bpm over beat 1000 (no drift), validation failures one by one,
overlap rejection, a cut gives 2 segments + 1 join with the expected frames, a loop x3 gives the expected segment list, earlycut before/after resolves with the same removed length, outputGrid after a cut at the
very start, mixed cut + loop sequence total length equals the sum of beats.

Verify: build, `ctest -R edit-plan`.

On completion:
```sh
geany-progress done 1 -r src/trakcrate/edit_plan.h -r src/trakcrate/edit_plan.cpp -r tests/test_trakcrate_edit_plan.cpp -w "cross/earlycut semantics are defined in the plan Design section; check them against what you want before phase 2"
```

---

## Phase 2 - DSP: render a plan to PCM

Context to load: `src/trakcrate/edit_plan.h`, `src/trakcrate/audio_decode.h`, this plan's Design section. Do not load UI files.

Sonnet writes `src/trakcrate/edit_render.{h,cpp}`:

1. `bool renderEdit(const DecodedPcm& src, const EditPlan& plan, const BeatGrid& grid, DecodedPcm* out, const std::atomic<bool>* cancel = nullptr)`: copies each segment, then applies each join.
   Process joins on the output buffer using source frames read directly from `src` (so tail/pre-roll material is always the original audio). All fade lengths are clamped to the available audio
   (a segment shorter than the fade shortens the fade; never reads outside `src`). Return false on cancel.
   - `cut`: linear fade-out over `ms` ending at the join, fade-in over `ms` after it.
   - `cross`: replace the last N beats before the join with `equalPower(src[e-N..e] out, src[s-N..s] in)`; the following content is unchanged.
   - `fadeout`: linear fade of the last N beats to zero; 5 ms fade-in on the next segment.
   - `tailoverlap`: K>0: no fade-out on the outgoing audio (the tail continues it seamlessly), 20 ms fade-in on the next part, plus `src[e..e+K]` x linear fade-out added into the start of the next segment (K clamped to the removed range); K=0 is `cut` with 20 ms fades.
2. `DecodedPcm renderLoopSample(const DecodedPcm& src, const BeatGrid&, long long startBeat, long long lenBeats, EditAlgo algo, double param)`: one pass of `[a,b)` made loop-safe: the join `b->a`
   is applied *inside the sample* so that playing it end-to-end repeated is click-free (`cut`: fades at both ends; `cross`: last N beats replaced by the pre-roll crossfade; `fadeout`: last N beats
   faded, 5 ms in). Length is exactly `lenBeats` of the grid.
3. A helper `float maxStepDelta(const DecodedPcm&, size_t frame, int radius)` is test-only; put it in the test.

Tests `tests/test_trakcrate_edit_render.cpp` (new target): synthesise a 120-bpm track (e.g. a 110 Hz sine plus a click every beat, 32 beats) and check
(a) output length equals `planOutputFrames`; (b) a naive raw cut at a non-zero crossing produces a sample step above a threshold while each algo's join keeps the max sample-to-sample step below
the sine's natural maximum step x 1.5 (this is the "no click" test); (c) `fadeout` reaches ~0 at the join; (d) `cross` midpoint level is about -3 dB for uncorrelated material and the output length is unchanged;
(e) `tailoverlap` K=2 output differs from K=0 only in the first 2 beats after the join and K=0 is identical to `cut` 20 ms; (f) cancel flag returns false; (g) `renderLoopSample` length is exact and its
wrap-around step (last sample -> first sample) is below the threshold; (h) edge cases: fade longer than a segment, cut at track start, loop at track end.

Verify: build, `ctest -R edit-render`.

On completion:
```sh
geany-progress done 2 -r src/trakcrate/edit_render.cpp -r tests/test_trakcrate_edit_render.cpp -w "click test uses a synthetic sine; do a listening test in phase 7"
```

---

## Phase 3 - Output: wav / mp3 writers, tags, naming, info file, library import, preferences

Context to load: `src/trakcrate/audio_decode.cpp` (ffmpeg usage style), `src/trakcrate/fsutil.h`, `src/audio/wav_info.cpp` (acid chunk layout), `src/trakcrate/track.h` (KvDoc/meta), how prefs are stored (find it, see Findings),
`tests/test_trakcrate_cuemarks.cpp` (style).

Sonnet writes:

1. `src/trakcrate/edit_output.{h,cpp}` (no Qt):
   - `struct AudioTags { std::map<std::string,std::string> tags; }` and `bool readAudioTags(const fs::path&, AudioTags*)` via `avformat` metadata (title, artist, album, genre, date, comment, track, TKEY/initial_key, etc.; keep ffmpeg's key names).
   - `std::string editTitle(const std::string& title, const std::string& user)` ("Foo (Original Mix)" -> "Foo (Alice Edit)"; no suffix doubling if already ends with "(Alice Edit)") and `loopTitle(title, name, user)`.
   - `fs::path uniqueEditPath(const fs::path& folder, const std::string& stem, const std::string& user, const std::string& ext)` -> `<stem>_<user>_edit.<ext>`, `_2`, `_3` on clash; user sanitised for file names.
   - `enum class OutFormat {Wav, Mp3}`; `struct OutOptions {OutFormat fmt; int mp3Kbps; /*CBR: 128,192,256,320*/ }`.
   - `bool writeWav(const fs::path&, const DecodedPcm&, double bpm, const AudioTags&, std::string* err)`: 16-bit PCM with TPDF-free simple rounding + clip, RIFF `fmt `, `acid` chunk (tempo = bpm; same layout `wavBpmFromFile` reads), `LIST/INFO` (INAM, IART, IPRD from album, ICMT, ICRD). `bpm == 0` omits `acid`.
   - `bool writeMp3(const fs::path&, const DecodedPcm&, int kbps, double bpm, const AudioTags&, std::string* err)`: libmp3lame CBR at 44.1 kHz stereo via libavformat (`id3v2_version=3`, metadata incl. `TBPM` = bpm rounded to 2 decimals when bpm > 0). Writes to `<path>.part` then renames.
   - `bool writeInfoFile(const fs::path& txt, const EditInfo&, std::string* err)` where `EditInfo` has: name (output file name), source file, author, ISO timestamp, bpm, first beat ms (output grid), source bpm and first beat, output duration, and one line per region (`#1 cut bars 33-36 algo=cross 1 beat`, `#2 loop beats 65..68 x4 algo=fadeout 0.5`). Plain `key: value` lines then a `regions:` list.
   - Library integration, in `src/trakcrate/edit_library.{h,cpp}`: `struct EditImportResult {bool ok; std::string error; fs::path trackDir; std::string playlist; int playlistIndex /* -1 none */; std::string note;}` and
     `EditImportResult importEditToLibrary(const Library&, const fs::path& originalTrackDir, const fs::path& renderedFile, const fs::path& infoFile, const BeatGrid& outputGrid, const std::atomic<bool>* cancel = nullptr)`
     implementing the "Where an edit goes" section exactly: stage + `importFolder`, `analyseTrack` (waveform only), write the known grid with origin `edited`, copy `edit.txt` in, then the same-name-playlist insertion before the original. `note` says why no playlist was used.
2. Preference store (extending the mechanism found above, minimal): `edit.format` (wav|mp3), `edit.mp3_kbps` (default 256), `edit.folder` (file-only output; default per the Design section), `edit.author` (default `$USER`), `edit.loop_folder` (default = `edit.folder`). Getter/setter pair in a small `src/ui/edit_prefs.{h,cpp}` if the mechanism is Qt-based, otherwise next to the existing code.
3. Tests `tests/test_trakcrate_edit_output.cpp` (new target): title rules (several inputs incl. "(original mix)" case, no suffix, already-edited), unique-name clash handling in a temp dir, wav written -> `decodeAudio` gives same length/near-same samples and `vdj::wavBpmFromFile` returns the bpm (link `vdj-cdj`), mp3 CBR written -> decodes with length within one mp3 frame (1152) + encoder delay allowance, `TBPM`/title tags read back with `readAudioTags`, mp3 bitrate read from the stream equals the requested CBR, info file contains the region lines, existing file never overwritten.
   `edit_library` tests on a temp library with a fixture track (a synthetic wav imported by the test): the edit lands in the same master folder as the original, has `waveform.bin`, the exact grid (origin `edited`) and `edit.txt`; with a playlist named like the folder holding tracks A,B,C the edit
   of B ends at index 1 (A, edit, B, C) and the symlink numbering is consistent; no such playlist -> nothing created, `playlist` empty; playlist exists but lacks the original -> not added; the original track dir is byte-identical afterwards; a second edit gets a distinct track dir.

Verify: build, `ctest -R edit-output`.

On completion:
```sh
geany-progress done 3 -r src/trakcrate/edit_output.cpp -r src/trakcrate/edit_library.cpp -r tests/test_trakcrate_edit_output.cpp -w "playlist insertion only when the playlist name equals the folder name exactly" -w "acid chunk layout copied from wavBpmFromFile; untested in real Acid until phase 7" -w "mp3 artwork is not copied"
```

---

## Phase 4 - Editor UI: draw, select and manage regions (no audio yet)

Context to load: `src/ui/trakcrate_track_editor.{h,cpp}`, `src/ui/waveform_widget.{h,cpp}` (paintCuemarks, mouse handling, context menu, `setCuemarkEditing`, snap), `src/trakcrate/edit_plan.h`, `doc/trakcrate/track-editor.md`.

Sonnet does:

1. `WaveformWidget`: new API `setEditRegions(QVector<WaveEditRegion>)` (`startMs`, `endMs`, `kind`, `number`, `selected`), `setEditing(bool)`; paints each region as a 25% opacity band (alpha 64) over the whole height: **dark grey for cut, yellow for loop**, with its
   number (and "x4" for loops, an algo abbreviation) at the top-left, brighter 1 px edges when selected. In edit-regions mode a left drag selects a range that snaps to beats (loops/cuts both snap to the grid exactly, no Alt free mode); the
   dragged range shows as an outlined preview band; on release it emits `editRangeSelected(startMs, endMs)`. Clicking a band emits `editRegionClicked(index)`. Without a grid the mode refuses to enable (status message, same wording style as loops).
   The mode is exclusive with `gridEdit_` and `editMarks_` like the existing toggles.
2. `TrakcrateTrackEditor`: a checkable `editRegions_` button ("Edit track") beside `editMarks_`, a row of "Cut" / "Loop" buttons that turn the current selected range into a region (cut: rounds to whole bars from the nearest downbeat-snapped
   start, loop: rounds length to the nearest allowed bars), rejecting overlaps with a status message; a `QListWidget regionList_` ("1 cut bars 33-36 cross 1b", "2 loop 4 bars x4 fadeout 1b"); a small parameter panel for the selected
   region (algo combo limited per kind, param combo that follows the algo, loop bars combo, loop times spin 2..16, earlycut shift/direction/join algo). Region edits go through the editor's `QUndoStack` (new `EditRegionCommand` following
   `CuemarkEditCommand`), `Delete` removes the selected region. The region list lives only in the editor (not written to the master), reset when another track is loaded. Show the resulting duration ("Edit: 5:42 -> 4:51") using `planEdit` +
   `planOutputFrames`; show validation messages from `validateRegions`.
   Keep this in a new `src/ui/trakcrate_edit_panel.{h,cpp}` widget owned by the editor to avoid growing `trakcrate_track_editor.cpp` (636 lines) further; the editor only wires it to `wave_` and the undo stack.
3. Tests `tests/test_trakcrate_edit_ui.cpp` (new target, offscreen like the other UI tests): selecting a range via the panel API creates a cut snapped to bars, overlap rejected, undo/redo of add/remove/param change, changing algo
   resets the param to a legal default, duration readout, region list texts.

Verify: build, `ctest -R edit-ui`; run `vdj-ui`, load a gridded track and check by eye (note in the progress file what was checked).

On completion:
```sh
geany-progress done 4 -r src/ui/trakcrate_edit_panel.cpp -r src/ui/waveform_widget.cpp -r src/ui/trakcrate_track_editor.cpp -w "regions are in memory only; they are lost when another track is loaded"
```

---

## Phase 5 - Preview and render dialogs

Context to load: `src/ui/trakcrate_edit_panel.h`, `src/trakcrate/edit_render.h`, `src/trakcrate/edit_output.h`, `src/trakcrate/edit_library.h`, `src/audio/audio_player.h`, the prefs from phase 3, one existing small dialog in `src/ui/` for style (e.g. the clone dialog).

Sonnet does:

1. A worker (`QtConcurrent`/`QThread`, follow what `trakcrate_analysis` or the exporter already uses) that decodes the source once (cached in the panel while the track is loaded), runs `planEdit` + `renderEdit`
   with a cancel flag, and writes a temp `.wav` in a `QTemporaryDir` (removed with the dialog). The UI shows a progress/busy state and a Cancel.
2. `EditPreviewDialog`: owns its own `vdj::AudioPlayer`, Play/Pause/Stop, a position slider and time readout, and a "jump to next join" button (buttons seek 2 beats before each join in the *output* timeline; the
   join output positions come from the plan). Closing stops playback and deletes the temp file. "Render..." inside the dialog calls the render dialog.
3. `EditRenderDialog`: destination (radio: **"Add to library, next to the original" - the default**, which shows the target master folder and the playlist that will be used, if any; or "Save file only" with a folder chooser), format (WAV / MP3), MP3 bitrate (128/192/256/320, CBR), author name; all initialised from and saved back to the prefs on accept ("UI should remember the user's preference").
   Shows the resulting file name. On OK: reuses the already rendered PCM (re-render if the regions changed since), reads the source tags, sets title via `editTitle`, artist untouched, writes the file (to a temp dir for the library destination, else to the chosen folder via `uniqueEditPath`)
   with `writeWav`/`writeMp3` and the output grid bpm, writes the info file, and for the library destination calls `importEditToLibrary` on the worker thread (busy state, cancellable) then emits a signal so the track list / playlist view of the trakcrate panel refreshes. The status line reports the new track and, on a playlist insert,
   "added to playlist X before <original>". Errors keep the dialog open.
4. Panel buttons "Preview..." and "Render..." (disabled with a tooltip when no regions or invalid).
5. Tests: dialog logic with the file output injected (offscreen): prefs round-trip, name preview, file-only render creates `<stem>_<user>_edit.wav` and `.txt` in a temp folder (second render `_2`); library render creates the track in the original's master folder, analysed, and inserted in the same-name playlist before the original; source track bytes unchanged (hash before/after); the dialog text names the playlist (or says there is none).

Verify: build, ctest; manual: preview a real edit and render both formats.

On completion:
```sh
geany-progress done 5 -r src/ui/trakcrate_edit_preview.cpp -r src/ui/trakcrate_edit_render_dialog.cpp -w "after a library render the trakcrate panel must refresh: check the track and playlist views show the new track" -w "the whole track is decoded into memory (about 600 MB for a 1 hour float stereo track)"
```

---

## Phase 6 - Acid loops

Context to load: `src/trakcrate/edit_render.h` (`renderLoopSample`), `src/trakcrate/edit_output.h`, the phase 5 dialogs, `src/ui/trakcrate_edit_panel.cpp`.

Sonnet does:

1. Core: `renderLoopSample` already exists (phase 2); add `LoopExportInfo` handling in `edit_output` if needed: the title `loopTitle(title, name, user)` => "Title (<name> <User> Edit)" (name prompted), file name `<name>.wav` sanitised
   (default stem `<original stem>_<name>`), `.wav` or `.mp3` with the bpm in the metadata (`acid` chunk for wav, `TBPM` for mp3), id3/tags copied from the source with the new title.
2. UI: panel action "Export loop..." for a selected loop region (and a quick action on the selected range, snapped to a beat start, default 4 beats, lengths 1/2/4/8/16 bars available; the 4-beat case is the prime use). The dialog asks for the name, algo
   (`cut`/`cross`/`fadeout` + param, default `cross` 1/2 beat... choose `cut` 10 ms as the default), format, and the loop folder (choose button; defaults to the same folder rule as file-only edits: where the original was imported from; `edit.loop_folder` is remembered from then on). Loops are plain sample files and are **not** imported into the library or put in playlists.
   "Preview" plays the loop seamlessly repeating (render the sample to a temp wav and loop it in the preview player: add a `loop` flag to the preview dialog that seeks to 0 at the end; if `AudioPlayer` has the cue-loop engine from the cue-loops
   plan, use it, otherwise render the sample repeated 8x into the temp file, which is simplest).
3. Tests: exported wav has exact length of N beats at the bpm, `wavBpmFromFile` returns the bpm, mp3 has the TBPM tag, title suffix, loop folder pref is remembered, never overwrites.

Verify: build, ctest.

On completion:
```sh
geany-progress done 6 -r src/ui/trakcrate_edit_panel.cpp -r src/trakcrate/edit_output.cpp -w "unverified in Acid itself: see phase 7"
```

---

## Phase 7 - Docs, final checks, listening test

Context to load: `doc/trakcrate/track-editor.md`, `doc/trakcrate/gui.md` (where editor features are listed), this plan.

Sonnet does:

1. New `doc/trakcrate/track-edits.md`: user-facing description (modes, colours, algos with their parameters and when to use each, naming and tags rules, the `.txt` info file, preferences, acid loops, what is not saved), link it from
   `track-editor.md`. Keep the algo table identical to the Design section.
2. Run the full `ctest`; list any failures that predate this plan separately.
3. Check that no function in this work reads from or writes to the master track folder (grep for `saveMeta|saveCuemarks|saveBeatGrid` in the new files: there must be none).

USER STEP: the human listens to three real edits (a bar cut with `cut`, a 4 bar break removed with `cross`, a loop x4 with `fadeout`) and loads one exported loop in Acid. Report what they find; if a click is audible, fix the
fade lengths / algo before completing. Do not run `geany-progress done` until they confirm.

On completion:
```sh
geany-progress done 7 -r doc/trakcrate/track-edits.md -w "listening test result recorded in the progress file"
```
