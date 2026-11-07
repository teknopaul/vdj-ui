# Export dialog UI — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Reorder, reword, remove dead options | complete |
| 2 | Space data model and size scan | complete |
| 3 | Space table and per-location buttons | complete |
| 4 | Status pane polish | complete |
| 5 | Delete button flow (inert) | complete |
| 6 | Empty trash and cleanup | complete |

## Review notes

### Phase 1 — Reorder, reword, remove dead options

⚠ registerAsVirtualPendrive/virtualPendriveName removed: exportDrive now always gets an empty register name from the dialog

⚠ ExportOptions::optionalFilesFrom/copy* left in the exporter on purpose

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_export_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_export_dialog.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_panel.cpp:1149`

### Phase 2 — Space data model and size scan

⚠ Free-space blocking now compares free against Required (estimate minus current rekordbox size), not the full estimate

⚠ Current size is cached per location for the dialog's lifetime and is computed off the GUI thread

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_export_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/dir_usage.cpp`

### Phase 3 — Space table and per-location buttons

⚠ Clean up and Delete are inert in this plan by design (see brief)

⚠ Trash directory is .Trash-<uid>, not a hard coded .Trash-1000

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_export_dialog.cpp`

### Phase 4 — Status pane polish

⚠ Not visually checked in the running app (no display here): please eyeball both modes, spacing and the summary_ minimum height next to the new table

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_export_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_export_dialog.h`

### Phase 5 — Delete button flow (inert)

⚠ The default deleter is a stub: nothing is deleted. Real deletion needs its own design and tests

⚠ Dialog close/Esc/Cancel set the cancel flags via done(int)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_export_dialog.cpp`

### Phase 6 — Empty trash and cleanup

⚠ Empty trash really deletes; Clean up and Delete remain inert

⚠ Empty trash only guards against a symlinked/non-folder trash path; it does not check that the location is a mount root (plan asked for it)

⚠ Empty trash is enabled only while the trash folder has entries

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_export_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/ai-output/refactor-recommendations.md`
