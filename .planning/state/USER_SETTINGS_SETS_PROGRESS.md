# User settings sets — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Settings-set model in the library | complete |
| 2 | djprofile.nxs format and user-name editing | complete |
| 3 | Exporter writes all five files with a policy and a chosen set | complete |
| 4 | User Settings tree and create/import actions | complete |
| 5 | Settings viewer and diff | complete |
| 6 | Virtual pendrive link and export dialog combobox | complete |
| 7 | Documentation and final sweep | complete |

## Review notes

### Phase 1 — Settings-set model in the library

⚠ Flat user-settings/ files are migrated into a set named 'imported' on library open

⚠ Panel exportDrive now reads the default set via resolveSettingsFiles (interim, phase 3/6 replace it); setDjprofileUserName is a stub returning false until phase 2

⚠ A damaged DEVSETTING/MYSETTING on the source drive refuses the whole copy; RB import no longer copies USBMNG.DAT; set deletion has no pendrive-link check until phase 6

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/settings_sets.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/settings_sets.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_settings.cpp`

### Phase 2 — djprofile.nxs format and user-name editing

⚠ Only one djprofile.nxs sample exists (7 identical copies); the 4 bytes at 0x1c matched no checksum tried, so they are left untouched and name edits are UNTESTED on a real deck

⚠ Name limit is 63 bytes (field is 128) as a cautious guess; the 0x04 timestamp is not refreshed

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/settings_files.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/settings-files.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/UNKNOWN_BYTES.md`

### Phase 3 — Exporter writes all five files with a policy and a chosen set

⚠ Builds on the uncommitted graceful-export changes in exporter.cpp

⚠ In update mode USBMNG.DAT is now never overwritten when the drive has one (previously it was replaced by an empty table)

⚠ ExportOptions::settingsBuiltIn added for the phase 6 'Built-in defaults' combobox entry; panel no longer reads flat settings files

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/exporter.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/exporter.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_export.cpp`

### Phase 4 — User Settings tree and create/import actions

⚠ First run: an empty User Settings node shows a double-click hint (opens New settings) instead of a modal prompt; exports still work with built-in files

⚠ The new dialogs and the context menu actions are not exercised by tests (modal); the panel methods behind them are

⚠ rekordbox import dialog now ticks settings/device/profile by default and has a settings-set name field

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_settings_dialogs.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_panel.cpp`

### Phase 5 — Settings viewer and diff

⚠ MYSETTING/DEVSETTING payload bytes are shown one per row but not decoded (grey rows)

⚠ The viewer's only write is the djprofile.nxs name (Save name); that edit is untested on a real deck

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/settings_view.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_settings_viewer.cpp`

### Phase 6 — Virtual pendrive link and export dialog combobox

⚠ Real-deck test of Replace vs AddMissing is manual

⚠ Deleting a set that a pendrive links to is refused; renaming a set retargets the links

⚠ First export of a library with no sets creates a set named 'Default' with the login name on the profile

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_export_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/settings_sets.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_panel.cpp`

### Phase 7 — Documentation and final sweep

⚠ CLI (src-cli) still ignores settings sets; documented in cli.md

⚠ Markdown tables not re-aligned with mdalign

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/TRAKCRATE-FORMAT.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/trakcrate-db-format.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/settings-files.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/gui.md`
