# Track View keyboard control — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Remote keys from track list | complete |
| 2 | Cue keys, labels, q | complete |
| 3 | Tooltips and typed BPM | complete |
| 4 | BPM lock | complete |
| 5 | Documentation | complete |

## Review notes

### Phase 1 — Remote keys from track list

⚠ Page Up/Down no longer scroll the track list while a track is loaded

⚠ Space falls through to VDJ play/pause when no track is loaded

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/player_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_table_view.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp:2455`

### Phase 2 — Cue keys, labels, q

⚠ B is also the global copy-BPM shortcut: the player only claims it when a track is loaded and the player has focus

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/player_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/waveform_widget.cpp`

### Phase 3 — Tooltips and typed BPM

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/player_panel.cpp`

### Phase 4 — BPM lock

⚠ No clamping in the panel, but the time stretcher (src/audio/time_stretcher.h kMinTempo/kMaxTempo) still limits tempo to 0.5x..2x; the BPM display shows the locked BPM regardless

⚠ Lock state is not persisted

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/player_panel.cpp`

### Phase 5 — Documentation

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/track-view-keyboard-control.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/gui.md`
