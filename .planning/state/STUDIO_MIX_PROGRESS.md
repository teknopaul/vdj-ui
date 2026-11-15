# Studio mix — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Mix model, master BPM, mix types | complete |
| 2 | The .mix XML file and mixes tree | complete |
| 3 | Pluggable time stretch engines and cache | complete |
| 4 | Audio engine: curves, EQ, compressor | complete |
| 5 | Streaming MP3 writer and render job | complete |
| 6 | Studio mix window and lanes | complete |
| 7 | Editing: periods, curves, EQ, stretch, undo | complete |
| 8 | Cuts, loops, save/open, mixes tree | complete |
| 9 | Preview, render dialog, docs | pending |

## Review notes

### Phase 1 — Mix model, master BPM, mix types

⚠ mix type table in the plan has assumptions (stop-cut, bass-switch, silent incoming until the period ends): confirm by ear in phase 9

⚠ unwritten curves are reset to 100% across a type's window by two boundary points only when the curve has other non-100 points

⚠ validateMix rejects a mix where a later track would start before the mix begins (inStartBeat larger than outStartBeat)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/mix_model.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/mix_model.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_mix_model.cpp`

### Phase 2 — The .mix XML file and mixes tree

⚠ the XML attribute names are fixed by this phase: document them in phase 9

⚠ Library::create now also makes mixes/; mix names may not start with a dot

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/mix_file.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/xml_lite.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_mix_file.cpp`

### Phase 3 — Pluggable time stretch engines and cache

⚠ SoundTouch engine is written behind TC_HAVE_SOUNDTOUCH and untested (no dev headers); the in-process Rubber Band engines (rubberband-lib-r3/r2) ARE built here (headers found) and tested

⚠ quality table on synthetic material: .planning/state/STUDIO_MIX_STRETCH_TABLE.md; Rubber Band R2 (all variants) drifts the pitch of a steady 110 Hz tone by the ratio, R3 does not: tests do not assert R2 pitch

⚠ synthetic-material quality numbers say nothing about how music sounds: judge engines with Audition in the UI (phase 7)

⚠ the cache can grow to the configured limit (default 4 GB) in ~/.cache/vdj-ui/stretch

⚠ stretch test takes ~45 s (every engine x 4 ratios, several probes)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/stretch_engine.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/stretch_engines.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/stretch_cache.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_stretch.cpp`

### Phase 4 — Audio engine: curves, EQ, compressor

⚠ EQ and compressor are tested with synthetic tones only: listen in phase 9

⚠ the crossover is flat in MAGNITUDE (the all-pass phase shifts the signal), so the test checks level per tone, not sample equality; bass cut depth at 30 Hz is 20/40/60 dB for 12/24/48 slope, a 60 Hz tone is only ~24 dB down with the default 24 dB slope

⚠ TrackSource returns shared_ptr<const DecodedPcm> (not a raw pointer) so eviction cannot free audio being read

⚠ tempo match test: varispeed holds beats within 1 ms of the master grid, wsola only within 15 ms (as the phase 3 probe shows), not 1 ms as the plan said

⚠ memory: at most 3 edited tracks are cached by default; a track with cuts/loops holds its stretched copy and its edited copy while rendering

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/mix_engine.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/mix_eq.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/mix_compressor.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_mix_engine.cpp`

### Phase 5 — Streaming MP3 writer and render job

⚠ VBR is libmp3lame ABR at 256k average (falls back to best VBR quality if the encoder refuses): confirm the files play on the decks

⚠ a 2 hour mix renders track by track but takes minutes: progress and cancel must be wired in the UI (phase 9)

⚠ the cover is written as a packet after the header (this ffmpeg warns when it is only set as attached_pic)

⚠ the real LADSPA plugin is only smoke tested (processor runs); the factory plumbing is tested with a fake processor

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/mp3_writer.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/mix_render.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/mix_filters.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_mix_render.cpp`

### Phase 6 — Studio mix window and lanes

⚠ lane layout and waveform are tested offscreen only; an offscreen snapshot of 3 synthetic tracks looked right (ruler, lanes, red bar lines, labels, green play line): check on a real playlist by eye

⚠ very long mixes: the canvas only paints visible lanes and columns; not tried with 30 real tracks

⚠ the Studio mix menu entry was added to the trakcrate window File menu (test_trakcrate_panel menu list updated) and the main window View menu

⚠ Save/Open/Render menu items are stubs until phases 8 and 9

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/studio_mix_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/mix_canvas.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_mix_ui.cpp`

### Phase 7 — Editing: periods, curves, EQ, stretch, undo

⚠ mouse interaction is tested with synthetic events (QTest::mouseEvent for modifiers); use it on a real mix and report anything awkward

⚠ snapshot undo copies the whole mix per step (small); drags, spin boxes and nudges merge into one step per drag / control

⚠ the trim spin box and the grid nudge buttons are in a Track dock on the right, not in the lane label as the plan said; curve choice is keys 1/2/3

⚠ dragging the incoming start line slides the incoming track by the distance dragged (the line stays under the anchor); a period start that would not fit shortens the period

⚠ Audition (stretch excerpt A/B, blind option) and the LADSPA chooser are written but only smoke-built here: try them by hand

⚠ stretches are queued 600 ms after a tempo or engine change by StretchJobQueue; the Stretch dock and the lane label show progress only as 'stretching...'

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/mix_canvas.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/studio_mix_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/mix_filters_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/mix_stretch_dock.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_mix_edit.cpp`

### Phase 8 — Cuts, loops, save/open, mixes tree

⚠ the edit panel gained an external mode (setExternalUndo / setMixMode / setRegions): its own editor tests (edit-ui, edit-dialogs) still pass

⚠ regions + curves in edited beats: a length-changing region edit moves audio under existing curve points (they are clamped to the new length, not shifted), periods are shortened to fit; the status bar says so

⚠ deviation: in region mode the lane keeps showing the EDITED waveform and the range is marked on it; seams of cuts and loops are small orange ticks at the lane bottom (no full bands over the original waveform)

⚠ regions are edited with the lane selected: R toggles the range mode (or the button in the Cuts and loops dock)

⚠ save: Ctrl+S asks for name/author/image the first time, later saves overwrite silently; Save As accepts Folder/Name; a mix opened from outside the library is written back to its own file

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_edit_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/studio_mix_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/mix_tree_dock.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_mix_edit.cpp`
