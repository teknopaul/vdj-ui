# Track edits — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Edit model and segment planning | complete |
| 2 | DSP render a plan to PCM | complete |
| 3 | Output writers, tags, library import, prefs | complete |
| 4 | Editor UI regions | complete |
| 5 | Preview and render dialogs | complete |
| 6 | Acid loops | complete |
| 7 | Docs and listening test | pending |

## Review notes

### Phase 1 — Edit model and segment planning

⚠ cross/earlycut semantics are defined in the plan Design section; check them against what you want before phase 2

⚠ touching cuts are rejected (merge them into one cut); a cut starting after a lead-in before beat 1 keeps the lead-in

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/edit_plan.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/edit_plan.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_edit_plan.cpp`

### Phase 2 — DSP render a plan to PCM

⚠ click test uses a synthetic sine; do a listening test in phase 7

⚠ tailoverlap with K>0 leaves the outgoing audio unfaded (the tail continues it) - differs from the plan's first wording

⚠ a cross with no audio before the incoming point (loop at the track start) falls back to 10 ms fades

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/edit_render.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_edit_render.cpp`

### Phase 3 — Output writers, tags, library import, prefs

⚠ playlist insertion only when the playlist name equals the folder name exactly

⚠ acid chunk layout copied from wavBpmFromFile; untested in real Acid until phase 7

⚠ mp3 artwork is not copied; edit_library stages the file and uses importFolder, so the track dir is named after the edit file stem

⚠ source.txt gets edit_of=<original track dir name>; imported_from points at a temp staging path (the original's key is imported_from)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/edit_output.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/edit_library.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/edit_prefs.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_edit_output.cpp`

### Phase 4 — Editor UI regions

⚠ regions are in memory only; they are lost when another track is loaded

⚠ not looked at in a real window (offscreen tests only): check the grey/yellow bands, the drag selection and the panel layout by eye

⚠ vdj-ui-trakcrate-artwork-edit-test failed once during a full make+ctest and passed on every rerun (3 full trakcrate runs); probably unrelated flakiness

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_edit_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/waveform_widget.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_track_editor.cpp`

### Phase 5 — Preview and render dialogs

⚠ after a library render the trakcrate panel is refreshed through trackChanged(new folder): check the track and playlist views show the new track in a real window

⚠ the whole track is decoded into memory and kept (source + render) while the track stays loaded in the editor (about 600 MB for a 1 hour float stereo track)

⚠ preview and render dialogs are modal (exec); the preview was only tested up to loading the player, not audible playback

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_edit_preview.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_edit_render_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/edit_job.cpp`

### Phase 6 — Acid loops

⚠ unverified in Acid itself: the acid chunk (flags 0x10, beats, 4/4, tempo) is checked only with wavBpmFromFile - see phase 7

⚠ loop preview renders the sample repeated 8 times into a temp wav (no cue-loop engine used); default seam is cut 10 ms

⚠ loops are plain files: never imported into the library or added to playlists

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_edit_loop_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/edit_job.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_edit_panel.cpp`
