# Concurrent and multi-target export

## Goal

Today the trakcrate panel runs one job at a time (`TrakcrateJobRunner` holds one `QThread*`), so only one pendrive can be written at once.
Allow several exports to run at the same time, each with its own progress and cancel, and add a "Write to all selected drives" action that starts one export per drive.

## Findings (why this is feasible)

- `trakcrate::exportDrive(lib, opts, progress, cancel)` (src/trakcrate/exporter.cpp) is re-entrant: each call makes its own staging dir (`makeTempDir`), copy buffers are `thread_local`, nothing global. Two exports to different destinations share nothing but read-only library files.
- The one-job limit is only in the GUI: `TrakcrateJobRunner` (src/ui/trakcrate_job.{h,cpp}), and the panel's single-job state: `export_`, `exportDest_`, `exportRegisterName_`, `watchSuspended_`, one `progress_`/`progressLabel_`/`cancelButton_`, and `setBusyUi()` greying everything.
- Staging defaults to `/dev/shm` (`pickStagingBase`, exporter.cpp ~285). N concurrent exports means N times the staging RAM (database, ANLZ files, artwork; not audio). The free-space check only sees free space at start time.
- `jobs_->busy()` is used at ~12 places in trakcrate_panel.cpp (import, analyse, orphans, context menus, `TrakcratePanel::busy()`, line ~337 wait loop) and in tests.

## Design decisions

1. **Keep `TrakcrateJobRunner` unchanged.** One runner instance per export, owned by a new `ExportJob` struct. `jobs_` stays the runner for exclusive work (import, rekordbox import, analyse, BPM). No change to the existing call sites' semantics.
2. **Exclusion rules.** Exports vs exports: allowed, unless the destinations are the same or one is inside the other (compare `weakly_canonical`). Exports vs exclusive jobs: mutually exclusive in both directions (imports and analysis write into the library that exports read). `busy()` = exclusive job running OR any export running.
3. **Library watcher.** Exports only read the library, so they no longer set `watchSuspended_`. Only exclusive jobs do.
4. **Staging budget.** `exportDrive` gets a process-wide atomic "staging bytes reserved" counter (in exporter.cpp, small anonymous-namespace static); the free-space check subtracts reservations by other running exports, reserves its need, releases on exit (RAII). Prevents two simultaneous exports exhausting `/dev/shm`.
5. **UI.** Replace the single bottom progress strip with a jobs strip: no jobs = today's status line; each running export gets a row (destination name, progress bar, current file, Cancel). Pendrive tree rows being written get a small busy badge. Finish messages queue into a short history (tooltip/expander) rather than overwriting each other.
6. **Multi-target.** "Write to all ticked drives..." opens one dialog (playlist ticks and options shared), lists the detected mounted drives with checkboxes, and starts one export per ticked drive via the same `startExport`. Each drive's "Update" mode is decided per drive (drive already has a trakcrate export = update).

## Phase 1: Export engine: staging reservation and same-destination guard

Files: src/trakcrate/exporter.cpp, exporter.h, tests/test_trakcrate_export.cpp (or the existing exporter test file).

- Add the process-wide staging reservation (atomic bytes reserved + RAII `StagingReservation`). Use it where the staging free-space check is (`sp.available < meta + (8u << 20)` becomes `sp.available < reservedByOthers + meta + 8 MiB`).
- Add `bool destinationsOverlap(const fs::path& a, const fs::path& b)` to exporter.h (canonicalise, equal or prefix on path components). Declared in the trakcrate namespace so the UI and tests use it.
- Tests: overlap cases (same, nested, sibling, nonexistent destinations); reservation released after success, failure and cancel; two `exportDrive` calls run on two `std::thread`s to different temp destinations produce trees byte identical to running them serially (compare file lists + cksum).
- Build and run `ctest -R trakcrate-export`.

```sh
geany-progress done 1 -r src/trakcrate/exporter.cpp -r src/trakcrate/exporter.h -w "staging reservation is process wide: cancelled or crashed exports must release it (RAII)"
```

## Phase 2: Panel: per-export jobs, exclusion rules, no watcher suspend

Files: src/ui/trakcrate_panel.h, trakcrate_panel.cpp.

- New `struct ExportJob { int id; std::unique_ptr<vdj::TrakcrateJobRunner> runner; std::shared_ptr<trakcrate::ExportResult> result; QString dest, registerName; }` and `std::vector<std::unique_ptr<ExportJob>> exports_` (stable ids). Remove `export_`, `exportDest_`, `exportRegisterName_` as members; `orphans_` becomes per-destination (the orphan button acts on the most recently finished export that has orphans; keep `orphanDest_`).
- `exportDrive(opts, registerName)` (line ~1063): refuse when `jobs_->busy()` or the destination overlaps a running export (`destinationsOverlap`); create an `ExportJob`, connect its runner's `progress`/`finished` to lambdas capturing the job id; no `watchSuspended_`.
- Split `onJobFinished`: the export branch becomes `onExportFinished(int id, bool cancelled)` using the code now under `if (export_)` (summary, warnings, orphans, `exportFinished`, `registerPendriveRequested`), then removes the job. The remaining body handles exclusive jobs only.
- `busy()`: `jobs_->busy() || !exports_.empty()`. Exclusive starts (import, rbimport, analyse, BPM, orphan delete, line 1268 check) refuse while exports run, with the existing "a job is running" text. The wait loop at ~337 (destructor/close) cancels and waits for all export runners.
- `setBusyUi`: split into exclusive busy (disables import/analyse/rbimport/new pendrive as now) and export busy (Export button stays enabled while only exports run; disabled while an exclusive job runs).
- Panel destructor / `closeLibrary`: cancel and wait for every export runner.
- Tests (tests/test_trakcrate_panel.cpp): two exports to two temp dirs at once both finish ok and `exportFinished` fires twice; a same-destination second export is refused; import is refused while an export runs and an export is refused while an import runs; closing while two exports run does not hang.
- Build, run `ctest -R "trakcrate-panel|trakcrate-export"`.

```sh
geany-progress done 2 -r src/ui/trakcrate_panel.cpp -r src/ui/trakcrate_panel.h -w "busy() semantics changed: check every jobs_->busy() call site and tests that wait on it"
```

## Phase 3: Jobs strip and tree badges

Files: src/ui/trakcrate_panel.cpp/.h, optionally new src/ui/trakcrate_jobs_strip.{h,cpp} (add to CMakeLists.txt and the resource list used by tests).

- New `TrakcrateJobsStrip : QWidget`: `addJob(id, title)`, `setProgress(id, done, total, current)`, `removeJob(id)`, `signal cancelRequested(int id)`. One row per job: label, `QProgressBar` (max width 220 like today), Cancel button. Hidden when empty.
- The existing bottom status line (`progressLabel_`) stays for single exclusive jobs and for messages. The existing `progress_`/`cancelButton_` stay for exclusive jobs; export jobs use strip rows only. Hide a row's Cancel when its progress reaches `rekordbox/export.pdb` (same rule as line 909).
- Finished-export messages: keep a `QStringList exportLog_` (last 20); `progressLabel_` shows the latest, its tooltip shows the history.
- Pendrive tree: items whose export destination matches a running export (the pendrive's registered folder) show a "writing" suffix/icon; cleared in `onExportFinished`.
- Tests: strip shows N rows for N running exports, row disappears on finish, cancelling one export leaves the other running and finishing ok.

```sh
geany-progress done 3 -r src/ui/trakcrate_panel.cpp -r src/ui/trakcrate_jobs_strip.cpp -w "layout: check the panel still fits the narrow window; rows must not grow the splitter"
```

## Phase 4: Multi-target export

Files: src/ui/trakcrate_export_dialog.h/.cpp, trakcrate_panel.cpp/.h, tests/test_trakcrate_panel.cpp, doc/trakcrate/gui.md.

- Export dialog: when the "Write to a physical device" mode is used, the drives list (`scanDriveRows`, already async) becomes multi-select (`QAbstractItemView::ExtendedSelection`); the summary shows totals per selected drive (the async `refreshSummary` computes one `SummaryData` per drive; keep the generation counter; Export enabled only if every selected drive is fine).
- `options()` stays single-target for existing callers; add `std::vector<trakcrate::ExportOptions> allOptions()` returning one per selected drive (shared playlists/settings, per-drive dest and update flag).
- Panel: after `dlg.exec()`, call `exportDrive` for each option set; start them in order, report any refused ones (overlap) in one message box. The `registerAsVirtualPendrive` choice applies only when exactly one drive is chosen (disable the checkbox otherwise).
- Staging: phase 1's reservation protects memory when several start at once; if a start is refused for lack of staging space, the message names the drive and the others proceed.
- Tests: dialog with two fake drive folders selected gives two option sets with distinct destinations; panel starts two exports and both complete.
- Docs: doc/trakcrate/gui.md gets a "Writing to several drives at once" section; note USB hub bandwidth is shared, and the `/dev/shm` staging memory cost per export.

```sh
geany-progress done 4 -r src/ui/trakcrate_export_dialog.cpp -r src/ui/trakcrate_panel.cpp -r doc/trakcrate/gui.md -w "multi drive selection: Update mode is per drive; virtual pendrive registration only for a single drive"
```

## Phase 5: Hardening and manual checks

- Run the full ctest suite and `make test` in src-cli (unrelated oracle failures if the helper is not installed are known).
- Stress: three exports to three temp dirs while the library watcher is active; cancel mid-run on one; verify no leftover `trakcrate-export*` dirs in `/dev/shm`, no `.trakcrate-tmp` files, and the reservation counter returns to zero (debug assertion in a test hook).
- Manual (human): two real USB sticks, same library, concurrently; close the app mid-export; confirm each stick works on the decks.

```sh
geany-progress done 5 -w "needs real USB sticks to verify; speed on a shared hub is hardware bound"
```

## Effort estimate

Phase 1: half a day. Phase 2: one day. Phase 3: one day. Phase 4: one day. Phase 5: half a day. About 4 days; a minimal version (phases 1, 2 and a plain jobs list) is about 1.5 days.
