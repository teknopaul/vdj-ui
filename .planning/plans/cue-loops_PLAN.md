# Cue loops: show, edit and play loops on cue marks

Source prompt: `ai-prompts/cue-loops.prompt.md`.

Goal: a cue mark can carry a loop end. Loops are read from a rekordbox pendrive (already shown in the rekordbox viewer), survive
"copy back" into the trakcrate master, are drawn as a 25% shaded band on both waveform views, can be created / resized / removed in
the trakcrate track editor, and are played by the Local Track Player's cue buttons.

## Findings from reading the code (do not re-derive)

- **The loop already reaches the master.** `cuesFromAnlz()` (`src/trakcrate/deck_edits.cpp:71`) writes loops into `cuemarks.txt` as an
  extra column `loop_end_ms=<ms>` (`Cuemark::extra`, `src/trakcrate/track.h:56`, columns 6+ of the tab separated line). The importer
  (`rb_import.cpp`) uses the same function. So nothing is lost in the files; the **readers ignore it**:
  - `trakcrateTrackAnalysis()` (`src/ui/trakcrate_analysis.cpp`) builds `AnalysisCue` without `loop` / `loopEndMs` / `loopNum` / `loopDen`.
    This is why the Local Track Player and the track view show no loop, while the rekordbox viewer (ANLZ via `track_analysis.cpp:93`) does.
  - `TrakcrateTrackEditor::showMarks()` builds `WaveCuemark` (`ms`, `label`, `hot` only).
- **Drawing:** `WaveformWidget::paintCues()` (`waveform_widget.cpp:~367`) already shades `AnalysisCue` loops (alpha 45, yellow `kCueColor`).
  `paintCuemarks()` (`waveform_widget.cpp:233`) draws editable marks in `kCuemarkColor` (green) and knows nothing of loops.
  Context menu: `WaveformWidget::contextMenuEvent` (`:586`), signals `cuemarkAdded/Moved/Removed/HotChanged`.
- **Editing:** pure functions in `src/trakcrate/cuemark_edit.{h,cpp}` (no Qt, tested by `tests/test_trakcrate_cuemarks.cpp`), driven by
  `TrakcrateTrackEditor` (`addCuemark`, `moveCuemark`, ... through an undo stack, single writer `applyMarks()`).
  Edit mode is the checkable button `editMarks_` (`trakcrate_track_editor.cpp:119`).
- **Playing:** `PlayerPanel::jumpToCue(ms)` (`player_panel.cpp:468`) does quantized / sequencer-synced jumps with `AudioPlayer::seekMs` /
  `seekAtMs`. `AudioPlayer` (`src/audio/audio_player.cpp`) renders in a worker thread; a pending jump is split inside the render chunk by
  `splitAtTrigger()`. There is no loop concept in it. Cue buttons are built in `rebuildCueButtons()` (`player_panel.cpp:~550`), keys A-C via
  `hotCueForKey`. Both the rekordbox viewer and the trakcrate track view load `TrackAnalysis`/`AnalysisCue`, so fixing `AnalysisCue` fixes both.
- **Confirmed by the user:** copy back already copies the loop into `cuemarks.txt`; the only gap is that loops are neither visible nor playable in the UI.
- **Decisions by the user:** (a) cue marks can still be created on ungridded tracks (keep working as today); only *creating or resizing loops* needs a beat grid.
  (b) Cue button toggle: the first press of a loop cue jumps (quantized) to the loop and starts looping; pressing the **same** cue again does not move the
  position and just stops the looping (playback continues); pressing a **different** cue leaves the current loop and, if that cue has a loop, enters it.
- **Loop length rule** (prompt): allowed lengths are 1/2, 1, 2, 4, 8, 16, 32 beats. Cue marks need not be on a beat; the loop *end* is
  `start + beats * 60000/bpm` of the constant grid (`BeatGrid`, `beatGridFrom(t.grid)`).
- **Out of scope:** exporting loops to a deck as loop hot cues is the optional last phase (needs a deck test); XML; variable tempo grids.

## Ground rules (read before every phase)

- Version control: the human does git. **Never run git commands.** Touch only files a phase names.
- Core logic stays free of Qt Widgets, in `src/trakcrate/` or `src/audio/`, so it is unit-testable. GUI changes are small.
- Build: `cd build && cmake . && make -j8`; tests `ctest` (use `cmake .` inside `build/`, never `cmake ..`).
  libcdj is installed system wide and current.
- Shared build files (`src/CMakeLists.txt`, `tests/CMakeLists.txt`): minimal edits.
- Loop data format stays `loop_end_ms=<ms>` in the extra columns. Do not add a column. Other extras must be preserved in order.
- Match the surrounding comment density (comments explain why, one line where possible).
- Human steps are marked `USER STEP`: stop, tell the human exactly what to do, and do not run `geany-progress done` until they confirm.

---

## Phase 1 - Loop data model and edit functions (no UI)

Context to load: this plan's findings, `src/trakcrate/track.h` (Cuemark), `src/trakcrate/cuemark_edit.{h,cpp}`,
`tests/test_trakcrate_cuemarks.cpp` (style), `doc/trakcrate/cuemarks.md` (data model section).

Sonnet writes:

1. In `cuemark_edit.{h,cpp}` (namespace `trakcrate`):
   - `constexpr double kLoopBeats[] = {0.5, 1, 2, 4, 8, 16, 32};`
   - `bool cuemarkLoopEnd(const Cuemark& m, double* endMs)` reads `loop_end_ms=` from `extra` (false when absent or not > `m.ms`).
   - `double snapLoopBeats(double beats)`: nearest allowed length (compare on a log scale so 3 beats -> 2 or 4 consistently; document the tie rule).
   - `double loopLengthMs(const BeatGrid&, double beats)`.
   - `bool setCuemarkLoop(CuemarkFile*, const BeatGrid&, int index, double beats)`: sets/replaces `loop_end_ms` (formatted like `deck_edits.cpp` does:
     integer ms is fine, keep it consistent) to `start + beats*period`, snapped to an allowed length; **false without a grid**, with a bad index,
     or when the end would pass `durationMs` (add a `durationMs` parameter; 0 = unknown, no limit).
   - `int loopIndexBefore(const CuemarkFile&, double ms)`: the last mark with `m.ms < ms` that is **not** already a loop, or -1. (This is the mark
     "end loop here" applies to. Decide and document: a looped mark between that mark and `ms` does not block it, but the menu in phase 4 only
     offers "end loop here" when the *directly preceding mark* has no loop. Implement `precedingMark(file, ms)` returning the directly preceding mark
     of any kind, and let the caller check whether it has a loop.)
   - `bool clearCuemarkLoop(CuemarkFile*, int index)`: removes only the `loop_end_ms=` item.
   - `bool scaleCuemarkLoop(CuemarkFile*, const BeatGrid&, int index, bool doubleIt, double durationMs)`: current length in beats =
     `(end-start)/period`; snap it to the allowed list first (imported loops can be odd lengths), then step one allowed size up/down; false at the
     ends of the list (0.5 / 32) or past the track end.
   - `moveCuemark` must carry the loop with its mark: when a mark with a loop moves, shift `loop_end_ms` by the same delta (length unchanged).
     `addCuemark` / `removeCuemark` need no change (removing the mark removes its extras). Check `setCuemarkHot` and label edits keep `extra`.
2. Tests in `tests/test_trakcrate_cuemarks.cpp` (same fixtures): parse/serialise round trip of a mark with `loop_end_ms=` plus an unrelated extra column;
   `snapLoopBeats` table (0.3->0.5, 3->2 or 4 per your tie rule, 40->32); set / double / half / clear; double at 32 and half at 0.5 refuse;
   no grid refuses; move keeps length; a loop running past the end refuses.
3. `doc/trakcrate/cuemarks.md`: a short "Loops" section: the column, allowed lengths, end defined by the grid, imported odd lengths are kept until
   resized. Run the `mdalign` skill on it if you added a table.

Verify: `cd build && cmake . && make -j8 vdj-ui-trakcrate-cuemarks-test && ctest -R cuemarks --output-on-failure`.

Finish: `geany-progress done 1 -r src/trakcrate/cuemark_edit.h -r src/trakcrate/cuemark_edit.cpp -r tests/test_trakcrate_cuemarks.cpp -w "Tie rule for snapping odd loop lengths is a judgement call: check the doc comment"`

---

## Phase 2 - Loops reach the analysis (track view, Local Track Player, copy back)

Context to load: `src/ui/trakcrate_analysis.cpp`, `src/ui/track_analysis.h` (AnalysisCue), `tests/test_trakcrate_analysis.cpp`,
phase 1's header (`cuemarkLoopEnd`).

Sonnet writes:

1. `trakcrateTrackAnalysis()`: for each mark with a loop (`cuemarkLoopEnd`), set `c.loop = true`, `c.loopEndMs`, and when the grid is known
   `loopNum/loopDen` from `(end-start)/period` (use the nearest allowed length as a fraction: 1/2 -> 1,2; 8 -> 8,1; if not an allowed length use
   a plain 1/1000 rounding or leave 0 so `loopBeats()` is 0 and the UI omits it - pick one, test it).
2. Extend `tests/test_trakcrate_analysis.cpp`: a track with a looped cuemark and a hot cue -> `cues` holds the loop with the right end and beats.
3. Copy back already writes the loop (user confirmed). Add a regression test in `tests/test_trakcrate_deckedits.cpp` (follow its existing fixtures)
   showing a deck loop ends up as `loop_end_ms=` in the master's `cuemarks.txt` and that `trakcrateTrackAnalysis` then reports it. No production change
   to copy back is expected.
4. `src/ui/track_details.cpp:128` prints `loop N beats to m:ss` for the rekordbox viewer; check the trakcrate track view's detail text uses the same
   `AnalysisCue` path and shows the same line (the prompt's example: `A 0:14.351 loop 1 beats to 0:14,799`). Reuse, do not duplicate the formatter.

Verify: `ctest -R "analysis|deckedits" --output-on-failure`.

Finish: `geany-progress done 2 -r src/ui/trakcrate_analysis.cpp -r tests/test_trakcrate_analysis.cpp -r tests/test_trakcrate_deckedits.cpp -w "Say in your report whether copy back already worked (readers were the only gap)"`

---

## Phase 3 - Draw loops: 25% band in the cue colour, on both views

Context to load: `src/ui/waveform_widget.{h,cpp}` (`paintCues`, `paintCuemarks`, `WaveCuemark`, `kCueColor`, `kCuemarkColor`, `timeX`/`cueX`),
`src/ui/trakcrate_track_editor.cpp` `showMarks()`.

Sonnet writes:

1. `WaveCuemark` gains `double loopEndMs = 0;` (0 = no loop).
2. `paintCuemarks()`: for a mark with `loopEndMs > ms` fill a rect from the mark's x to the loop end's x, full height, colour `kCuemarkColor` with
   **alpha 64 (25%)**, drawn *before* the line so the start line stays crisp. Use `timeX()` (the zoomed view), clamp to the widget, minimum 2 px wide, skip
   when entirely off screen. While a mark is dragged (`markDrag_`) the band moves with it.
3. `paintCues()` (rekordbox / player view): change the existing band alpha from 45 to 64 so both views agree; colour stays the cue colour of the line.
4. `showMarks()` in the editor sets `w.loopEndMs` from `cuemarkLoopEnd`. Add the loop to the list-widget text (`... loop 4 beats`).
5. A test: render a `WaveformWidget` offscreen (`QT_QPA_PLATFORM=offscreen` is how other widget tests run; look for an existing `grab()` or `QImage`
   test, e.g. in `tests/` for waveform/player widgets; if none exists, add a small one to `test_trakcrate_cuemarks.cpp` or a new target) and assert a pixel inside the
   band differs from the same pixel without the loop, and a pixel after the end does not. If widget rendering cannot be tested offscreen here, say so and
   skip the test instead of faking it.

Verify: build, `ctest`, then `USER STEP`: open a track with a deck loop in the trakcrate track view and in the rekordbox viewer; confirm the shading is visible,
25%, and the same colour as the line (green in the editor, yellow in the player view).

Finish: `geany-progress done 3 -r src/ui/waveform_widget.cpp -r src/ui/trakcrate_track_editor.cpp -w "Band alpha 64 = 25%; the rekordbox view band changed from 45 to 64 too"`

---

## Phase 4 - Editing: require a grid, "end loop here", double / half / remove loop

Context to load: `src/ui/waveform_widget.{h,cpp}` (`contextMenuEvent`, signals), `src/ui/trakcrate_track_editor.{h,cpp}` (the `addCuemark` /
`moveCuemark` pattern, `CuemarkEditCommand`, `applyMarks`, `editMarks_` enabling at `:266`), phase 1 functions.

Sonnet writes:

1. **Grid required for loops only.** Adding, moving and deleting cue marks on an ungridded track keeps working exactly as today; do not change that.
   Loop creation / resizing is what needs a grid: without one, "End loop here", "Double loop" and "Half loop" are not offered (or are shown disabled with the
   reason "needs a beat grid"), and `setLoopEnd` / `scaleLoop` refuse with a status message (they can be reached from other callers). "Remove loop" is
   always offered (it needs no grid), as is moving a looped mark (the loop keeps its length in ms).
2. **Context menu** (in `WaveformWidget::contextMenuEvent`; keep the widget dumb: it computes nothing about beats, it asks the owner). Add a way for the
   owner to describe the menu state, for example `std::function<LoopMenuState(double ms, int hit)>` set by the editor, where `LoopMenuState` says
   which of these are offered, using a pure function in `cuemark_edit` (add `loopMenuState(file, grid, ms, hit)` and test it in phase 1's test file):
   - "Add cuemark here" (existing, unchanged).
   - "End loop here": when the directly preceding mark (`precedingMark`) has no loop. Emits `cuemarkLoopEnded(int markIndex, double ms)`. The editor converts
     the clicked point to beats `(ms - start)/period`, quantizes with `snapLoopBeats`, and calls `setCuemarkLoop`. If the quantized end would pass the track end
     or the point is closer than 1/4 beat to the start, show a status message and do nothing.
   - Right click **inside a loop band** (strictly between start and end, not on the line): "Double loop", "Half loop" (disabled entries when at 32 / 0.5).
   - Right click **on a mark that has a loop**: "Remove loop" (the cue becomes a plain jump cue and then qualifies for "End loop here" again), plus the existing
     delete / hot-cue entries.
3. Editor methods `setLoopEnd(markIndex, ms)`, `scaleLoop(markIndex, bool doubleIt)`, `removeLoop(markIndex)`; each goes through the existing undo stack and
   `applyMarks()` (same pattern as `setCuemarkHot`), so undo/redo, the list and the waveform refresh for free.
4. Update the `editMarks_` tooltip text and `doc/trakcrate/track-editor.md` (menu entries, grid requirement, allowed lengths).
5. Tests: the pure `loopMenuState` cases (no preceding mark, preceding mark already looped, click inside a band, on a looped mark). If a widget level test of the
   editor already exists, add undo/redo of "end loop here"; otherwise a test on the command level that exercises `setLoopEnd` through the undo stack.

Verify: `ctest`; `USER STEP`: in the trakcrate track editor, with a gridded track: add a cue, right click later -> "End loop here" gives 1/2..32 beat lengths
only, band appears; right click inside -> double / half; right click the cue -> remove loop; undo/redo each; open a track without a grid -> "Edit cuemarks"
still lets you add and move cue marks but offers no loop entries (only "Remove loop" on an existing loop).

Finish: `geany-progress done 4 -r src/ui/waveform_widget.cpp -r src/ui/trakcrate_track_editor.cpp -r src/trakcrate/cuemark_edit.cpp -r doc/trakcrate/track-editor.md -w "Ungridded tracks still allow cue marks; only loop creation/resizing is refused"`

---

## Phase 5 - AudioPlayer loop engine with click-safe fades

Context to load: `src/audio/audio_player.{h,cpp}` (`run()`, the `pendingJump_` handling around `splitAtTrigger`, `seekMs`, `stretcher_`),
`src/audio/time_stretcher.h`, `tests/test_audio.cpp`.

Sonnet writes:

1. API on `AudioPlayer`: `void setLoop(double startMs, double endMs)` (replaces any loop; invalid if end <= start + 10 ms), `void clearLoop()`, `bool hasLoop() const`.
   Cleared by `load`/`unload`/`stop`. Not cleared by pause. `seekMs` and `seekAtMs` do **not** clear it by themselves: the owner (phase 6) decides, because a
   cue jump *into* a loop must keep it. Position inside a loop wraps; playing from before the loop start plays normally into the loop and then loops
   (as on a deck, activating at the end frame).
2. In `run()` (render chunk loop, under `mutex_`): if a loop is set and the loop end falls inside this chunk, render up to the end, `stretcher_.seek(startFrame)`,
   continue the chunk from the start frame (this is the same split mechanism as the pending jump; share the code, do not duplicate it; a pending
   `seekAtMs` jump has priority when both fall in one chunk).
3. **Fades**, applied in the chunk buffer sample-accurately: a linear **20 ms fade out** ending exactly at the loop end and a **5 ms fade in** starting at the
   loop start (`DecodedAudio::kSampleRate` frames: 882 and 220). Fades are in output time (the stretcher output), so tempo does not change their length.
   Fades apply only at the loop wrap, never to a normal cue jump or normal playback. A loop shorter than 25 ms is rejected by `setLoop`.
   Put the fade arithmetic in a free, testable function in `audio_player.h` (like `splitAtTrigger`), e.g. `void applyLoopFades(qint16* chunk, int frames, int fadeOutStart, int fadeInEnd)`.
4. Emit `loopWrapped()` (queued signal; optional, for a future UI flash; only if trivial).
5. Tests in `tests/test_audio.cpp`: pure functions (`splitAtLoopEnd`-style arithmetic and the fade ramps: first/last sample values, monotonic, lengths
   882/220 frames); and, if the existing tests can load a generated WAV without an audio device, a render level test that the position wraps. Do not require a
   sound card in tests; follow what the file already does.

Verify: `ctest -R audio --output-on-failure`.

`USER STEP` (after phase 6 wires it up, listed there): the human listens to the fades. Do not tune the 20 ms / 5 ms values without the human's report.

Finish: `geany-progress done 5 -r src/audio/audio_player.cpp -r src/audio/audio_player.h -r tests/test_audio.cpp -w "20 ms fade out / 5 ms fade in are the prompt's starting values: the human must judge how they sound" -w "Loop wrap and pending jump in one chunk: pending jump wins"`

---

## Phase 6 - Local Track Player: cue buttons start and respect loops

Context to load: `src/ui/player_panel.{h,cpp}` (`jumpToCue`, `rebuildCueButtons`, `hotCueForKey`, `keyPressEvent`, `sequencerGridWaitMs`, the
`player_->jumped` signal use), `src/ui/track_analysis.h`, phase 5 API.

Sonnet writes:

1. `jumpToCue(const AnalysisCue&)` overload (keep `jumpToCue(double)` for the non cue callers: the waveform click, beat jumps `jumpBeats`, which must **clear** the loop):
   - cue has a loop: after the jump lands, `player_->setLoop(c.timeMs, c.loopEndMs)`. Immediate jump: set the loop before/at `seekMs`. Quantized or sequencer
     queued jump (`seekAtMs`): set the loop when the `jumped` signal fires (store a `pendingLoop_` and apply it in the slot; cancel it if a newer jump/seek replaces it).
   - cue without a loop: `player_->clearLoop()`.
   - **Toggle (user decision):** keep `activeLoopCue_` (the cue whose loop is running, by time). Pressing a loop cue's button or key:
     - if that cue's loop is the active one: **do not seek**; only `player_->clearLoop()` (playback continues from where it is) and clear `activeLoopCue_`;
     - otherwise: jump to the cue as above (quantized / sequencer queued) and start its loop (setting `activeLoopCue_` when the loop is applied).
     Pressing a different cue (loop or not) leaves the old loop first: it jumps there, and enters that cue's loop if it has one.
     The first press after the loop was left (by the toggle) enters it again. If a queued jump into the loop is still pending when the same button is pressed
     again, cancel the pending jump and the pending loop (`cancelJump`) and do nothing else.
   - a jump to a plain time (waveform click, PageUp/Down beat jumps, stop, load of another track, tempo is fine) clears the loop.
   Loop start is the cue time as stored (not quantized), consistent with the cue jump landing at the cue; with quantize on the *jump* waits for the grid but the loop
   points stay exact. Document this.
2. Cue buttons (`rebuildCueButtons`) and keys A-C call the new overload; loop cues show "loop" in the tooltip (`player_panel.cpp:558` already says "Loop: jump to the
   start of the loop"; change it to say that it loops) and may be marked with a small glyph on the button text. Memory points that have loops get buttons the same way.
3. Show the active loop on the waveform: `WaveformWidget` draws cues' bands already (phase 3), so no extra drawing is needed; just confirm it stays visible while looping.
4. The toggle above is the way out of a loop; also leaving: pressing another cue, clicking the waveform, beat jumps, stop. Give an active loop's button a
   checked / highlighted look so the state is visible. Mention the behaviour in `doc/trakcrate/gui.md` or the player docs (find where the player panel is documented).
5. Tests: if `tests/test_player_quantize.cpp` or another test exercises `PlayerPanel` logic without a device, add cases for "cue with loop sets loop / plain jump clears it /
   queued jump applies the loop on `jumped`". If `PlayerPanel` cannot be tested without audio output, extract the decision into a small pure function
   (cue + jump kind -> loop action) and test that.

Verify: `ctest`; `USER STEP` (the sound test the prompt asks for): in the Local Track Player, load a track with a loop (made in phase 4 or copied back from a deck), press its cue
button, and listen: the loop repeats seamlessly on the beat; try with Q on, with the sequencer sync on, at tempo != 1; confirm there is no click at the wrap (20 ms fade out /
5 ms fade in); report what you hear (too soft/long fades, still clicking, gap). Then press the same cue again: the position must not jump and the audio carries on past the loop end. Press it a third time: it jumps back (quantized) and loops again.
Press a different (non loop) cue while looping: the loop ends and it jumps there; press a different loop cue: it enters that loop. Click the waveform: the loop stops.

Finish: `geany-progress done 6 -r src/ui/player_panel.cpp -r src/ui/player_panel.h -w "Loop points are exact; only the jump into the loop is quantized" -w "Not hardware tested: sound at the wrap (human listening test)"`

---

## Phase 7 (optional) - Write loops to the deck on export

Only do this if the human asks after phases 1-6. The exporter ignores loops today: `exporter.cpp:~273` makes hot cues from marks with a letter A-C and never sets
`AnlzCue::isLoop` / `loopTimeMs` (`src/trakcrate/anlz_writer.h:24`), although `anlz_writer.cpp:112` already writes the loop entry layout seen in real files.

Sonnet writes: when a hot-cue mark has a loop (`cuemarkLoopEnd`), set `a.isLoop = true; a.loopTimeMs = round(end)`. Test with the ANLZ round trip pattern in
`tests/test_trakcrate_anlz_roundtrip.cpp` (read it back with libcdj: `is_loop`, `loop_time_ms`). Update `doc/trakcrate/cuemarks.md`.

`USER STEP`: this has to be tried on a real deck (does the loop show and play from the pad?). Until the human reports, mark the doc "not seen on a deck".

Finish: `geany-progress done 7 -r src/trakcrate/exporter.cpp -r tests/test_trakcrate_anlz_roundtrip.cpp -w "Untested on a deck: loop hot cues written by us"`
