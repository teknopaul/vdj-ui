# Export dialog UI improvements: plan

Source brief: `ai-prompts/export-ui-improvements.prompt.md`. Code: `src/ui/trakcrate_export_dialog.{h,cpp}` (607 lines), callers in `src/ui/trakcrate_panel.cpp` (`exportDialog` ~1135, `exportPendrive` ~1490), tests in `tests/test_trakcrate_panel.cpp` (~965-1040, `TrakcrateExportDialog` cases) and `tests/test_trakcrate_settings.cpp`.

## Goal

Reorder and reword the "Write pendrive to a physical device / as a folder" dialog, remove options that are no longer needed, and replace the misleading "Free on X (needs about Y)" text with a per-location disk-space table with Clean up / Delete / Empty trash controls. **Clean up and Delete are UI only in this plan: they do nothing yet** (the brief says the real code needs separate careful design and testing). Empty trash and the delete-progress enable/disable behaviour are specified in the brief; see the scope note in phase 5.

## Design decisions (fixed here so phases don't re-argue them)

- **Two modes**, both via `setPendrive(drive, physical)`: `physical_ == true` (mounted stick, destination chosen from the drives combo only) and folder mode (`physical_ == false`, destination typed / browsed). The dialog is also used without `setPendrive` (playlist export from `TrakcratePanel::exportDialog`): treat that like folder mode.
- **Final row order** (top to bottom):
  1. `Destination:` row (larger font, bold path). Folder mode: line edit + Browse. Physical mode: read-only line edit.
  2. Pendrive icon + drives combo + refresh. **Physical mode only** (hidden otherwise). Below it the "Simultaneous export destination(s)" list (physical mode only), excluding the main destination.
  3. `update_` checkbox.
  4. Status pane: summary text (warnings in red, USB rate, sizes) followed by the per-location space table (phase 4).
  5. Tracklist icon + folder (entry/playlist) list.
  6. User Settings group (no title).
  7. "Advanced Export Options" group at the end.
- `raiseLimits_` stays where it is today (directly under the status pane, shown only when over the limits).
- **Removed**: `register_` checkbox, `registerAsVirtualPendrive()`, `virtualPendriveName()`, `realDrive_` + the `exportExt_/rbfltr_/usbmng_/djprofile_` "copy from a real drive" checkboxes and the matching `baseOptions` block. The `ExportOptions::optionalFilesFrom/copy*` fields in `src/trakcrate/exporter.{h,cpp}` are **left alone** (the exporter and its tests keep working; removing them is a separate cleanup).
- **Space table model** (one per location: the main destination plus each ticked simultaneous drive): `struct LocationSpace { QString name, mountPoint; qint64 freeBytes; qint64 currentBytes; int currentTracks; bool currentKnown; qint64 requiredBytes; }`. `requiredBytes = max(0, estimatedWrite - currentBytes)` is the brief's "never show a negative number". "Current size" is `du`-like over `<mount>/PIONEER` and `<mount>/Contents` and is computed on a worker thread (never the GUI thread); until it arrives the cell shows "calculating...". "(xxx tracks)" counts files under `Contents/` that are audio (cheap during the same walk).
- **Required estimate**: for a fresh/empty location `audioBytes + metadataBytes` (as today). For an update, the same total minus what is already there (`currentBytes`), floored at 0; the existing `need` computation in `applySummary` is the single place to change.
- **Enable/disable of Export** is driven by one `bool deleting_` count (number of in-flight deletions) combined with the existing `good` flag; never touch `ok_` from two places. Introduce `updateOkEnabled()` that sets `ok_->setEnabled(good_ && deleting_ == 0)` and call it from `applySummary` and the delete-finished path.
- **Strings** exactly as in the brief (see each phase). Keep the existing comment density and Qt idioms of the file.
- The brief's `Cancel` => "deleting stops immediately" requires a cancellable deletion (an atomic flag checked by the worker). Since deletion does nothing yet, the flag and the hook exist but no filesystem call is made.

## Phases

Each phase leaves the tree building and `ctest` green. Build with `cmake --build build` (see the `build/` dir); run the relevant tests with `ctest --test-dir build -R trakcrate`.

---

### Phase 1: Reorder, reword, remove the dead options

Files: `src/ui/trakcrate_export_dialog.{h,cpp}`, `src/ui/trakcrate_panel.cpp`, `tests/test_trakcrate_panel.cpp`.

1. Remove `register_`, `registerAsVirtualPendrive()`, `virtualPendriveName()` (header and cpp) and the `register_->setEnabled(extraMounts.empty())` line in `refreshSummary`. In `trakcrate_panel.cpp` change `exportDrive(all[i], i == 0 && dlg.registerAsVirtualPendrive() ? ... : QString())` (line ~1149) and `exportPendrive` (line ~1499) to pass `QString()`. Do not change `exportDrive`'s signature in this phase. Remove the `QVERIFY(!mdlg.registerAsVirtualPendrive())` at `tests/test_trakcrate_panel.cpp:1037`; grep the other tests for `registerPendriveRequested` expectations that depended on the checkbox and adjust (the signal itself belongs to other UI, leave it).
2. Remove `realDrive_`, `exportExt_`, `rbfltr_`, `usbmng_`, `djprofile_`, the "Copy from a real drive..." label and the `copies` layout; remove the `adv && !realDrive_...` block in `baseOptions`. Fix the header members.
3. Advanced group: title `Advanced Export Options` (was "Advanced / compatibility testing"). Keep the checkboxes and expand/collapse behaviour. Also update the class comment in the header that mentions the old title.
4. User Settings group: remove the group-box title (`new QGroupBox()` with no text; keep a frame so it reads as a block, or use a plain `QWidget` with a `QFrame::HLine` above, whichever matches the surrounding style), label `User Settings: ` (was "Settings to use:"), radio `addMissing_` text `Add missing User Settings` (default checked, already is), radio `replace_` text `Overwrite All User Settings`.
5. Simultaneous list label: `Simultaneous export destination(s)` (was "Also write the same to these drives...").
6. Reorder the layout to the "Final row order" above. Concretely, in the constructor: build the Destination row first (row 0), then the pendrive icon + `drivesRow` and `extraBox_` (rows 1-2, wrapped in one `physicalBox_` `QWidget*` so a single `setVisible` handles it), then `update_`, `summary_`, `raiseLimits_`, then the tracklist icon + `playlists_` (this is the stretch row, `layout->addWidget(..., 1)`), then the User Settings box, then `advanced_`, then buttons. The grid used today (`form`) is replaced by a vertical layout of these blocks, keeping the tracklist block as the only stretching one so extra dialog height still goes to the list.
7. Destination row: `QLabel("Destination:")` and the line edit use a font about 1.2x larger (`QFont::setPointSizeF(font().pointSizeF() * 1.2)`); keep the existing bold-when-non-empty slot, applying it on top of the larger font. In `setPendrive(.., physical)`: `physicalBox_->setVisible(physical)` (currently only `extraBox_` is toggled and the drives combo is always visible); in folder mode the line edit stays editable with Browse visible, in physical mode keep today's read-only + hidden Browse. Default (no `setPendrive`) = folder mode, so `physicalBox_` starts hidden.
8. `tickedExtraDrives()` already skips the primary. Also stop offering it: in `rescanDrives` `show()` and `addExtraDrive`, and whenever the destination changes (`dest_` `textChanged` and `detectedChosen`), hide (`item->setHidden(true)`) the `extraDrives_` row whose mount point equals `QDir::cleanPath(dest_->text())`, and uncheck it (`QSignalBlocker`). Add a private `syncExtraDrives()` that does this and call it from those places.

Tests (extend `tests/test_trakcrate_panel.cpp`): physical dialog: pendrive combo and extra list visible, folder dialog: both hidden (`isHidden()` on the objects exposed through new tiny test accessors only if the existing tests already use that style; otherwise `findChild` by `setObjectName`: name the new widgets `destRow`, `physicalBox`, `extraBox`); after `setDestination(mp)` the item for `mp` is hidden and unchecked; the radio texts and the group titles; `allOptions().size()` excludes the primary.

Review: run `build/vdj-ui` (or the dialog test harness) and look at both modes: order, larger Destination row, no register option, no "copy from a real drive".

```sh
geany-progress done 1 -r src/ui/trakcrate_export_dialog.cpp -r src/ui/trakcrate_export_dialog.h -r src/ui/trakcrate_panel.cpp:1149 -w "registerAsVirtualPendrive/virtualPendriveName removed: exportDrive now always gets an empty register name from the dialog" -w "ExportOptions::optionalFilesFrom/copy* left in the exporter on purpose"
```

---

### Phase 2: Space data model and the off-thread "current size" scan

Files: `src/ui/trakcrate_export_dialog.{h,cpp}` (data only, no new widgets), maybe a small helper `src/ui/dir_usage.{h,cpp}` (add to `src/CMakeLists.txt` next to the other ui sources; follow how `trakcrate_export_dialog.cpp` is listed).

1. `dir_usage.h`: `struct DirUsage { qint64 bytes = 0; int audioFiles = 0; bool ok = false; };` `DirUsage measureRekordbox(const QString& mountPoint, const std::atomic<bool>* cancel = nullptr);` Walks `<mount>/PIONEER` and `<mount>/Contents` with `QDirIterator` (no symlink following), sums `QFileInfo::size()`, counts files under `Contents/`. Returns `ok = false` when neither directory exists (a fresh stick: current size 0, 0 tracks, but `currentKnown = true`). Checks `cancel` every few hundred entries so a closed dialog stops it.
2. In the dialog: `struct LocationSpace` (see Design decisions) and `std::vector<LocationSpace> spaces_` ; a per-mount cache `QHash<QString, DirUsage> usageCache_` filled by the existing `spawn(alive_, ...)` helper (one worker per location, generation counter `usageGen_` so a stale answer after the destination changed is dropped). `ensureUsage(mountPoint)` starts a scan if not cached and not already running; on completion store in the cache and call `rebuildSpaces()`.
3. `rebuildSpaces()` computes `spaces_` from the last `SummaryData` (`avail` per location, `s.audioBytes + s.metadataBytes`), the cache, and `requiredBytes = max(0, estimate - current)`. For the main destination the mount point is the storage root reported by `QStorageInfo` (`rootPath()`), not the typed folder: in folder mode a typed subfolder measures `<dest>/PIONEER` and `<dest>/Contents` instead. Decide per location: `base = physical_ ? mountPoint : destination`. Put `base` in the struct.
4. Extend `SummaryData::Extra` with `avail` (already there) and make the main-destination `avail` / each extra's `avail` come through `LocationSpace`. `applySummary` keeps all its existing red warnings (not enough space, USB downgrade, destination exists but Update not ticked, ...). **Change the free-space decision to use `requiredBytes`**: `good = false` with the red "Not enough space." when `freeBytes < requiredBytes` for any location, once the usage is known; while still calculating use the old full estimate as the conservative bound only if the destination holds no earlier export (no clash), otherwise do not block.
5. Remove the misleading `Free on %1: %2 (needs about %3)` line and the per-drive `%1 free` bullets in the "Also writing to" paragraph from `applySummary` (the table in phase 3 replaces them); keep "Also writing to N more drive(s)" only as the lines that carry the update/new-export and USB-speed warnings, minus the free-space figures.

Tests (`tests/test_trakcrate_panel.cpp` or a new `tests/test_dir_usage.cpp` registered in `tests/CMakeLists.txt` like the siblings): build a temp tree with `PIONEER/x` (1000 B) and `Contents/a/b.mp3` (2000 B): `measureRekordbox` gives 3000 B and 1 audio file; an empty dir gives 0 and `ok == false`; a pre-set cancel flag returns early. For the dialog: with a synchronous dialog (`async == false`) and a destination holding an earlier export, `requiredBytes` never goes negative (estimate smaller than current).

Review the arithmetic in `rebuildSpaces()` and the free/required comparison.

```sh
geany-progress done 2 -r src/ui/trakcrate_export_dialog.cpp -r src/ui/dir_usage.cpp -w "Free-space blocking now compares free against Required (estimate minus current rekordbox size), not the full estimate" -w "Current size is cached per mount point for the dialog's lifetime and is computed off the GUI thread"
```

---

### Phase 3: The space table and per-location buttons (UI only)

Files: `src/ui/trakcrate_export_dialog.{h,cpp}`.

1. A `SpaceTable` private `QFrame`-based widget class in the dialog's cpp (or nested widget struct): one block per location, shown inside the status pane under `summary_`. Layout per block:
   ```
   <drive name> - <mount path>                     (bold)
   Free          XX.00 GiB
   Current size  YY.00 GiB (xxx tracks)            "calculating..." until known
   Required      ZZ.00 GiB
   [Clean up] [ ] [Delete] [Empty trash]
   ```
   Use `QLocale().formattedDataSize(bytes, 2, QLocale::DataSizeIecFormat)` so values read "GiB". A `QGridLayout` per block, parented to a container `spacesBox_` (`QVBoxLayout`); `setSpaces(const std::vector<LocationSpace>&)` reuses the block widgets when the count is unchanged (no flicker) and otherwise rebuilds.
2. Buttons per block: `Clean up` (`QPushButton`), a `QCheckBox` with an empty label and tooltip `Clean up after export` (accessibleName likewise), `Delete`, `Empty trash`. Object names `cleanUp`, `cleanUpAfter`, `delete`, `emptyTrash` plus the mount point as a dynamic property, for tests.
3. Behaviour in this phase: **`Clean up` and `Delete` do nothing** (the slots exist and are empty apart from a `// TODO` that points to the future clean-up work; the clean-up checkbox is stored in the block and exposed via `bool cleanUpAfterExport(const QString& mountPoint) const` but nothing acts on it). `Delete` is wired in phase 5. `Empty trash` is wired in phase 6; here it is only shown, enabled iff `<mount>/.Trash-<uid>/` exists (`getuid()`; the brief says `.Trash-1000`, the real name is `.Trash-$UID`).
4. Hook `rebuildSpaces()` to `setSpaces`. Position: `summary_` (warnings) then `spacesBox_`, inside the same status area; raise `summary_`'s fixed minimum height logic so the dialog does not jump (the table is taller than six text lines; keep the dialog resizable).
5. Hide the table while there are no playlists/entries selected or no destination.

Tests: dialog with a temp destination holding `PIONEER/x` bytes: the block shows the right free / current / required texts (`findChild<QLabel*>` by object name `freeValue`, `currentValue`, `requiredValue`); `Required` never shows a leading `-`; the checkbox tooltip is exactly `Clean up after export`; with one simultaneous drive ticked there are two blocks; `Empty trash` disabled without the trash dir, enabled with it.

```sh
geany-progress done 3 -r src/ui/trakcrate_export_dialog.cpp -w "Clean up and Delete are inert in this plan by design (see brief)" -w "Trash directory is .Trash-<uid>, not a hard coded .Trash-1000"
```

---

### Phase 4: Status pane polish and the "update" checkbox position

Files: `src/ui/trakcrate_export_dialog.cpp`.

1. Confirm the status area reads cleanly in all four states: fresh write to an empty drive, update of an existing export, folder mode, physical mode with two drives. Red warnings stay exactly as before (not enough space, destination not a folder, "already has ... Tick Update", USB downgraded, problems list).
2. The `update_` checkbox sits directly under the pendrive block (order item 3), with its text unchanged; it stays hidden unless the destination holds an earlier export (`update_->setVisible(!d.clash.empty())` already). Check the layout does not leave a gap when hidden.
3. Update tooltips and the `ExportDialog` class comment in the header to describe the new layout and the removal of the register option.
4. Manual pass with `build/vdj-ui`: both modes, a nearly full stick (table says Required > Free and Export is disabled), an update of a 27 GiB export (Required small, no misleading text).

If the visual pass shows spacing problems, fix them here; no new features.

```sh
geany-progress done 4 -r src/ui/trakcrate_export_dialog.cpp -r src/ui/trakcrate_export_dialog.h -w "Visual pass done by running the app; automated tests only cover the logic"
```

---

### Phase 5: Delete button flow (dialogs, enable/disable, cancel), still not deleting

Files: `src/ui/trakcrate_export_dialog.{h,cpp}`.

Scope note: the brief says nothing is to be deleted yet. This phase builds the **complete user flow** around a no-op worker so the real deletion is a single function body later.

1. `Delete` click: `QMessageBox` (Yes/Cancel, default Cancel) with text naming the two absolute paths, e.g. `Delete the contents of:\n<base>/PIONEER\n<base>/Contents\n` and, when `currentBytes > 1 GiB`, `This may take a while.` appended. (Only the sub-directories the brief lists will be deleted by the future code: `PIONEER/USBANLZ/*`, `PIONEER/rekordbox/*`, `PIONEER/Artwork/*`, `Contents/*`; the popup names the two parents as the brief says.)
2. On Yes: mark this location busy (`deleting_++`), disable that block's `Clean up` and `Delete` buttons, call `updateOkEnabled()` (Export disabled). Start a worker through `spawn(alive_, ...)` with a per-location `std::shared_ptr<std::atomic<bool>> cancel` that calls `deleteRegenerable(base, cancel)`: a **stub that returns immediately** with a comment saying real deletion is not implemented. On completion: re-enable the block's buttons, `deleting_--`, drop the usage cache entry for that mount, rescan usage and free space (`rebuildSpaces`), `updateOkEnabled()`.
3. Two blocks can be busy at once (independent `cancel` flags and per-block busy state). Export enabled only when `deleting_ == 0`.
4. Closing the dialog by `Cancel`, `Esc` or the window close: override `done(int)` (covers `reject`, `accept`, Esc and the close button; for `accept` there is nothing running because Export is disabled while deleting): set every `cancel` flag, then `QDialog::done`. The worker checks the flag at each directory entry once real deletion exists.
5. Empty destination of `alive_` already protects completion lambdas from running after the dialog is destroyed.

Tests: stub worker made injectable (`std::function<void(const QString&, const std::atomic<bool>&)> deleter_` member with a test setter, default = the stub): a test deleter that blocks on a latch lets the test assert Export and that block's buttons are disabled, a second block's buttons stay enabled, and after releasing the latch everything is re-enabled; with two blocks both deleting, Export only re-enables after both finish; `done(Rejected)` sets the cancel flags (the test deleter observes it). Use the synchronous dialog mode plus `QTRY_VERIFY` as the surrounding tests do. The confirm popup is not exercised (use a `static std::function` hook or a `confirmDelete` virtual-free seam: a `std::function<bool(const QString&)> confirm_` test setter, default shows the `QMessageBox`); test the popup text builder as a free function (`deleteConfirmText(base, currentBytes)`): contains both absolute paths, contains "This may take a while" only above 1 GiB.

```sh
geany-progress done 5 -r src/ui/trakcrate_export_dialog.cpp -w "deleteRegenerable() is a stub: nothing is deleted. Real deletion needs its own design and tests" -w "Dialog close/Esc/Cancel set cancel flags via done(int)"
```

---

### Phase 6: Empty trash flow and final cleanup

Files: `src/ui/trakcrate_export_dialog.{h,cpp}`, `doc/` if a dialog description exists (grep `doc/trakcrate` for "Also write" / "Advanced / compatibility"; update wording, no new docs).

Scope note: the brief specifies `Empty trash` fully (delete `./.Trash-<uid>/`, confirm popup, update free space). It is in the "careful consideration" bucket only for Clean up, so it is implemented for real, but guarded.

1. `Empty trash` click: confirm `QMessageBox` naming the absolute trash path; on Yes disable that block's Empty trash, `deleting_++` (it also blocks Export; same enable rules as phase 5), delete the **contents** of `<base>/.Trash-<uid>/` with `QDir::removeRecursively` on a worker (never follow symlinks; refuse if the path is not a real directory directly under the mount point root or if `base` is a folder that is not a mount root), then refresh free space and re-evaluate the button's enabled state (directory still exists but empty: keep it enabled only if it has entries).
2. Remove every remaining reference to the removed options (`grep -rn "Also write\|Advanced / compatibility\|Settings to use\|Keep the settings files\|Update the settings files\|Register the folder" src tests doc`) and fix wording in comments, docs and the man page `vdj-ui.1` if mentioned.
3. Add a short entry to `ai-output/refactor-recommendations.md` listing: the now-unused `ExportOptions::optionalFilesFrom/copy*` fields, and the `exportDrive(opts, registerName)` parameter that the dialog no longer feeds.
4. Run the whole suite (`ctest --test-dir build`) and fix any fallout.

Tests: `Empty trash` removes a temp `.Trash-<uid>` tree's contents and leaves the base files alone; refuses a symlinked trash dir; button disabled with no trash directory; free-space refresh is requested afterwards (spy on a `spacesChanged` seam or re-read `LocationSpace`).

```sh
geany-progress done 6 -r src/ui/trakcrate_export_dialog.cpp -w "Empty trash really deletes; Clean up and Delete remain inert" -w "Leftover unused exporter fields recorded in ai-output/refactor-recommendations.md"
```
