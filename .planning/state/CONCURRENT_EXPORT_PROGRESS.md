# Concurrent export — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Engine staging guard | complete |
| 2 | Panel per-export jobs | complete |
| 3 | Jobs strip and badges | complete |
| 4 | Multi-target export | complete |
| 5 | Hardening | complete |

## Review notes

### Phase 1 — Engine staging guard

⚠ staging reservation is process wide: cancelled or crashed exports must release it (RAII)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/exporter.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/exporter.h`

### Phase 2 — Panel per-export jobs

⚠ busy() semantics changed: exports now exclude the other jobs and vice versa; one shared progress bar and Cancel for all exports until phase 3

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_panel.h`

### Phase 3 — Jobs strip and badges

⚠ pendrive-tree 'writing' badge NOT done: the tree has no link from a drive row to its export folder; layout of the strip not checked visually

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_jobs_strip.cpp`

### Phase 4 — Multi-target export

⚠ multi drive selection is a 'Also write to' list next to the single destination combo (physical mode only), not a multi-select of the combo; per-drive summary lines show space and update/new; virtual pendrive registration is off when extra drives are ticked; not looked at on screen

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_export_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/gui.md`

### Phase 5 — Hardening

⚠ automated checks only: full ctest 65/65 in build-dev, panel test x3; /dev/shm/trakcrate-export-EuIdhj (11:33) belongs to your running export, not a test; real USB sticks and a restart of the GUI still to try
