# Cue loops — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Loop data model and edit functions | complete |
| 2 | Loops reach the analysis | complete |
| 3 | Draw loops 25% band | complete |
| 4 | Editing: grid required, end/double/half/remove loop | complete |
| 5 | AudioPlayer loop engine with fades | complete |
| 6 | Local Track Player cue buttons loop | complete |
| 7 | Write loops to deck on export (optional) | complete |

## Review notes

### Phase 1 — Loop data model and edit functions

⚠ Tie rule for snapping odd loop lengths is a judgement call: check the doc comment

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/cuemark_edit.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/cuemark_edit.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_cuemarks.cpp`

### Phase 2 — Loops reach the analysis

⚠ Copy back already worked (readers were the only gap); test added as a regression guard

⚠ track_details.cpp now prints 'loop to m:ss' for odd-length loops instead of '0 beats'

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_analysis.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_editor.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_deckedits.cpp`

### Phase 3 — Draw loops 25% band

⚠ Band alpha 64 = 25%; the rekordbox view band changed from 45 to 64 too

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/waveform_widget.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_track_editor.cpp`

### Phase 4 — Editing: grid required, end/double/half/remove loop

⚠ Ungridded tracks still allow cue marks; only loop creation/resizing is refused

⚠ Added Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y shortcuts to the editor (there were only Undo/Redo buttons next to the stars)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/waveform_widget.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_track_editor.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/cuemark_edit.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/track-editor.md`

### Phase 5 — AudioPlayer loop engine with fades

⚠ 20 ms fade out / 5 ms fade in are the prompt's starting values: the human must judge how they sound

⚠ Loop wrap and pending jump in one chunk: pending jump wins

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/audio_player.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/audio_player.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_audio.cpp`

### Phase 6 — Local Track Player cue buttons loop

⚠ Loop points are exact; only the jump into the loop is quantized

⚠ Wrap sound confirmed by the human; toggle/leave behaviour covered by tests

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/player_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/player_panel.h`

### Phase 7 — Write loops to deck on export (optional)

⚠ Deck-tested on a VBR mp3: loops play and draw; deck byte offsets differ 1-3 frames from ours

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/exporter.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/anlz_writer.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_anlz_roundtrip.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/cuemarks.md`
