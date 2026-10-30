# User settings sets: plan

Source brief: `ai-prompts/user-settings.prompt.md`. Related: `doc/trakcrate/settings-files.md`, `TRAKCRATE-FORMAT.md` section 10 and 2.4, `doc/trakcrate/trakcrate-db-format.md`.

## Goal

The five player support files (`DEVSETTING.DAT`, `MYSETTING.DAT`, `djprofile.nxs`, `rekordbox/RBFLTR.DAT`, `rekordbox/exportExt.pdb`) are **never optional**: a deck crashes without them. Today:

- `Library::settingsDir()` (`src/trakcrate/library.h:32`) is one flat `user-settings/` folder. A rekordbox import fills it only with the "advanced" checkboxes (`src/trakcrate/rb_import.cpp:527`).
- `TrakcratePanel::exportDrive` (`src/ui/trakcrate_panel.cpp:1039-1047`) reads the flat files and falls back to the compiled-in defaults (`src/trakcrate/settings_files.h`: `defaultDevSetting()` and the others).
- The "User Settings" tree node lists the file names flat (`trakcrate_panel.cpp:503`).
- `djprofile.nxs` carries the shipped author's name (`teknopaul teknopaul`), so every new user's drive shows that name.

Target:

1. **Settings sets**: named folders under `user-settings/`, each holding the five files plus a small `set.txt`. One set is marked default.
2. A library with no sets gets a "Create new settings" flow that asks for the user name, stamps `djprofile.nxs` and uses the built-in files for the rest.
3. A set can be copied from a real rekordbox drive (any number of named sets).
4. Export always writes all five files. Existing files on the drive are left alone, missing ones are added, and an explicit "update" option overwrites them. A combobox picks the set.
5. A virtual pendrive can be linked to a set (new `drives/<name>/drive.txt`).
6. A read-only viewer shows what is known about each file and diffs two sets. Only the `djprofile.nxs` user name is editable.

## Design decisions (fixed here so phases don't re-argue them)

- **Layout** (replaces the flat folder; not published yet, so it may change):
  ```
  user-settings/
  ├── default.txt              one line: "set: <name>"   (absent = first set alphabetically)
  └── <set-name>/              a single path component, validated with the same rule as pendrive names (isSafeName)
      ├── set.txt              key: value — created, source ("built-in", "rekordbox:<label>"), user_name (cached from djprofile)
      ├── DEVSETTING.DAT
      ├── MYSETTING.DAT
      ├── djprofile.nxs
      ├── RBFLTR.DAT
      └── exportExt.pdb
  ```
  A set may be missing individual files (a partial import); missing files fall back to the built-in bytes at export time. `USBMNG.DAT` is never in a set (`emptyUsbmng()` is generated).
- **Migration**: on `Library::open`, if `user-settings/` holds flat files and no set folders, move them into a set named `imported` (marked default). Never lose the user's files.
- **Policy at export**: `enum class SettingsPolicy { AddMissing, Replace }`. `AddMissing` (default) covers "leave existing" and "copy new if missing" per file. `Replace` is "update settings with trakcrate versions". "Always write all five" means the five are guaranteed present afterwards in either policy.
- **Virtual pendrive link**: `drives/<name>/drive.txt`, key/value (`settings: <set-name>`). Missing file or key means the default set. A link to a deleted set falls back to the default set with a warning.
- **Scope**: the C CLI (`src-cli/`) is independent of the GUI and keeps its own compiled-in defaults. This plan only documents the new layout for it (phase 7); porting is a later job.
- **Never use git.** Claude must not run any git command (no `git diff`, `status`, `add`, `commit`, `stash`, `checkout`, `reset`, nothing), in any phase. The user handles all version control. To see what the uncommitted graceful-export work changed, read the files themselves.
- Tests use the snip framework (see the `snip` skill) and are added to the existing `tests/test_trakcrate_*.cpp` files or a new `tests/test_trakcrate_settings.cpp` registered in `tests/CMakeLists.txt`.

## Phase 1: Settings-set model in the library

Read: `src/trakcrate/library.h`, `src/trakcrate/library.cpp` (`open`, line ~21), `src/trakcrate/settings_files.h`, `src/trakcrate/settings_files.cpp`, `src/trakcrate/default_files.cpp`, `src/trakcrate/rb_import.cpp` (lines 520-545), `TRAKCRATE-FORMAT.md` sections 2.1 and 10.

1. New `src/trakcrate/settings_sets.h/.cpp` (add to the `vdj-trakcrate` target) with a struct `SettingsSet { name, created, source, userName }` and `Library` methods or free functions taking `const Library&`:
   - `listSettingsSets()`, `hasSettingsSet`, `settingsSetDir(name)`, `defaultSettingsSet()` / `setDefaultSettingsSet(name)`.
   - `createSettingsSet(name, userName, err)`: built-in bytes for the four non-profile files, `djprofile.nxs` from `defaultDjprofile()` with the user name applied via a hook `setDjprofileUserName` (a stub returning false until phase 2, in which case the stock profile is written and `set.txt` records the name).
   - `copySettingsSetFromDrive(name, driveRoot, err)`: reads `PIONEER/DEVSETTING.DAT`, `MYSETTING.DAT` (must pass `verifySettingsFile`), `djprofile.nxs`, `rekordbox/RBFLTR.DAT`, `rekordbox/exportExt.pdb` from a mounted rekordbox drive; refuses if none of them is found; reports which were found.
   - `renameSettingsSet`, `deleteSettingsSet` (refuse to delete the last set while a virtual pendrive links to it; deleting the default re-points the default), `duplicateSettingsSet`.
   - `resolveSettingsFiles(setName)` returns the five byte strings (set file or built-in fallback), verifying the two `*SETTING.DAT` files.
2. Migration of flat files (see Design decisions) inside `Library::open`, idempotent.
3. `rb_import.cpp`: the existing `settings/deviceFiles/profile` options now create (or fill) a named set instead of flat files; add `std::string settingsSetName` to the import options (default `"rekordbox"`, made unique). The advanced checkboxes stay but the "settings" ones default to on in phase 4.
4. Tests: create/list/default/rename/delete, migration of a flat folder, refusal of bad names (`..`, `/`), copy from a fake drive directory built in a temp dir, a damaged `DEVSETTING.DAT` is refused, fallback to built-ins for a partial set.

Done when: `ctest -R trakcrate` passes and no UI file changed.

```sh
geany-progress done 1 -r src/trakcrate/settings_sets.h -r src/trakcrate/settings_sets.cpp -r tests/test_trakcrate_settings.cpp -w "Flat user-settings/ files are migrated into a set named 'imported' on library open"
```

## Phase 2: djprofile.nxs format and user-name editing

Read: `doc/trakcrate/settings-files.md` (djprofile section), `data/trakcrate/user-settings/djprofile.nxs` (160 bytes, `xxd`), `doc/trakcrate/UNKNOWN_BYTES.md`, any other `djprofile.nxs` on the machine (`find / -name djprofile.nxs 2>/dev/null`, `references/libcdj/`, `/media/*`).

Known layout of the one sample: bytes 0x00-0x0b look like a 64-bit big-endian millisecond timestamp (`00 42 d7 c3 | 00 00 01 8b de 67 e7 e9`; the first 4 bytes may be a separate field), zeros to 0x1b, **4 unknown bytes at 0x1c** (`a2 3f 2b 74`), then at 0x20 the text `teknopaul teknopaul` (NUL terminated), zeros to 160.

1. Investigate with a throw-away script in the scratchpad (not committed):
   - Does 0x1c hold a checksum of the name or of the file? Try CRC-32 (both byte orders), CRC-16/XMODEM (already in `crc16Xmodem`), Adler-32, FNV, sum, over: the name, the name with the NUL, bytes 0x20-0x9f, bytes 0x00-0x1b, the whole file with the field zeroed.
   - Is the name one field "first last" (here the same word twice) or two fields? Check for a second NUL-separated string and the field width.
2. If a second sample (a different name) exists on disk, use it to confirm. **If the checksum cannot be found with one sample**, stop investigating after the hypotheses above, implement editing of the name only with the 4 bytes left untouched, and record a clear warning in the UI/docs and an entry in `doc/trakcrate/UNKNOWN_BYTES.md` asking contributors for a sample drive with a different name (ask the user for one in the summary; a deck test with a changed name is the real verification, and that needs a deck, so mark it untested).
3. Implement in `settings_files.h/.cpp`:
   - `struct DjProfile { uint64_t timestampMs; std::string userName; bool crcKnown; }`, `bool parseDjprofile(const std::string&, DjProfile*, std::string* err)`.
   - `bool setDjprofileUserName(std::string* bytes, const std::string& name, std::string* err)`: UTF-8, at most the field width minus the NUL, no NUL or control characters, recomputes the 4 bytes if the checksum was found, also refreshes the timestamp only if the investigation shows it is the creation time.
   - Wire `createSettingsSet` (phase 1 stub) to it.
4. Update `doc/trakcrate/settings-files.md` with the findings (what is proven, what is guessed).
5. Tests: parse the shipped sample (`teknopaul`), round trip a name change, reject over-long and control-character names, file size stays 160.

Done when: tests pass and the doc states plainly whether the 4 bytes are understood.

```sh
geany-progress done 2 -r src/trakcrate/settings_files.cpp -r doc/trakcrate/settings-files.md -r doc/trakcrate/UNKNOWN_BYTES.md -w "If the 0x1c field is not a decoded checksum, name edits are untested on a real deck"
```

## Phase 3: Exporter writes all five files with a policy and a chosen set

Read: `src/trakcrate/exporter.h` (lines 30-60), `src/trakcrate/exporter.cpp` (lines 205-230, 455-600; the uncommitted graceful-export `update` work is in the same file, so keep it intact and read the current file contents carefully; no git), `tests/test_trakcrate_export.cpp`.

1. `ExportOptions`: add `SettingsPolicy settingsPolicy = AddMissing;` and `std::string settingsSet;` (empty = the library default). Keep the byte fields (`devSetting` ... `exportExt`) as overrides so existing tests and callers keep working; `exportDrive` fills any empty one from `resolveSettingsFiles(settingsSet)`, so the compiled-in defaults remain the last fallback.
2. `includeSettings` stays for compatibility testing but `ExportOptions` documents that production callers leave it true. Remove the need for the "advanced" checkbox path to get files at all: the five files are written whenever they are missing on the destination.
3. Staging: with `AddMissing`, a file already on the destination (update mode or a non-empty drive) is not written (count it as `skipped`); with `Replace` it goes through the existing temp-name-and-rename path used by `update`. Fresh destinations get everything either way.
4. `ExportResult`: add `settingsWritten`, `settingsKept` (vector of relative names) so the UI can report "kept DEVSETTING.DAT, added djprofile.nxs".
5. `summariseExport`: a damaged set file becomes a problem line naming the set.
6. Tests in `tests/test_trakcrate_export.cpp`: fresh export has the five files; update with `AddMissing` leaves a modified `MYSETTING.DAT` byte identical but adds a deleted `djprofile.nxs`; `Replace` overwrites; a chosen set's bytes land on the drive; an unknown set name fails with a clear error; cancel behaviour of the graceful export is unchanged.

Done when: export tests pass, including the existing graceful-export ones.

```sh
geany-progress done 3 -r src/trakcrate/exporter.h -r src/trakcrate/exporter.cpp -r tests/test_trakcrate_export.cpp -w "Builds on the uncommitted graceful-export changes in exporter.cpp"
```

## Phase 4: User Settings tree and create/import actions

Read: `src/ui/trakcrate_panel.h`, `src/ui/trakcrate_panel.cpp` (tree build 440-540, context menu code, `KindRole` handling; `grep -n '"setting"'`), `src/ui/trakcrate_rbimport_dialog.cpp`, `tests/test_trakcrate_panel.cpp`, icons in `img/` (`user-settings-22x22.png`, `pendrive-64x64.png` are present; reuse, do not add art unless needed).

1. Tree: "User Settings" > one item per set (kind `settings-set`, default one in bold with "(default)") > the five files (kind `setting`, as now). Keep expansion state across refreshes like the other nodes.
2. Context menu on "User Settings": **New settings...** and **Copy from rekordbox pendrive...**. On a set: **Make default**, **Rename...**, **Duplicate...**, **Delete** (confirm; blocked with an explanation if a virtual pendrive links to it), **Edit user name...** (enabled when the djprofile is parsed, uses phase 2), **View...** (phase 5).
3. New dialog `src/ui/trakcrate_settings_dialogs.h/.cpp`:
   - `NewSettingsDialog`: set name (default "My settings"), user name (default `$USER` capitalised from `QStandardPaths`/`qgetenv`, never the shipped name). Calls `createSettingsSet`.
   - `CopySettingsDialog`: folder chooser for a rekordbox drive root (reuse the pendrive/mount helpers the rbimport dialog uses), set name, shows which of the five files were found, then `copySettingsSetFromDrive`. Optionally offers to set the user name afterwards.
4. First run: when a library opens with **no** sets, show an info bar or a one-time prompt in the panel that opens `NewSettingsDialog` (not a modal on every start; a "Later" choice is fine because export falls back to built-ins and creates a set named by the library default, see phase 6). Never block opening.
5. Signals follow the existing panel style (`tc*Requested` or direct calls; match what neighbours do). Jobs are short and synchronous.
6. Tests in `tests/test_trakcrate_panel.cpp`: tree shape with two sets and the default marker, creating, renaming, deleting through the panel's methods (call the methods directly as the existing tests do, no modal dialogs).

Done when: panel tests pass and the app builds.

```sh
geany-progress done 4 -r src/ui/trakcrate_panel.cpp -r src/ui/trakcrate_settings_dialogs.cpp -r tests/test_trakcrate_panel.cpp
```

## Phase 5: Settings viewer and diff

Read: `doc/trakcrate/settings-files.md` (DEVSETTING/MYSETTING/RBFLTR sections), `src/trakcrate/settings_files.h`, `src/ui/trakcrate_track_editor.cpp` only for dialog style (do not copy its logic).

1. `src/trakcrate/settings_view.h/.cpp` (no Qt): `describeSettingsFile(kind, bytes)` returns rows `{ field, value, note, known }`:
   - `*SETTING.DAT`: header strings (brand, `rekordbox`, version), payload length, magic, stored vs computed CRC, then the payload as hex with the known leading words (`01 00 00 00`, `02 00 00 00`); MYSETTING bytes with the high bit set are shown as "option code 0x80+n" and marked *not decoded*.
   - `djprofile.nxs`: user name, timestamp as a date, the 4 unknown bytes as hex.
   - `RBFLTR.DAT`: the three header strings (device, firmware) and the `FCND` record count.
   - `exportExt.pdb`: size and header page count only; the rest "unknown".
   - `diffSettingsFiles(a, b)`: rows where the two sets differ (byte offsets for binary payloads).
2. `src/ui/trakcrate_settings_viewer.h/.cpp`: a dialog with a set combobox (A), an optional "Compare with" combobox (B), a file selector, a read-only table (Field / Value / Note) and a pure hex pane; differing rows are highlighted. The only editable control is the **user name** line edit with a Save button (writes through `setDjprofileUserName`, refreshes `set.txt`); it is disabled with a tooltip when the checksum question from phase 2 is unresolved only if saving would be unsafe, otherwise enabled with the warning text.
3. Hook up **View...** and double-click on a set or file in the tree; add "Compare..." on a set.
4. Tests: `describeSettingsFile` for the shipped files (CRC matches, name `teknopaul`), `diffSettingsFiles` on a copy with one payload byte changed, viewer constructs offscreen and shows the diff count (follow `tests/test_trakcrate_panel.cpp` for the offscreen setup).

Done when: tests pass; nothing in the viewer writes a file other than the djprofile name.

```sh
geany-progress done 5 -r src/trakcrate/settings_view.cpp -r src/ui/trakcrate_settings_viewer.cpp -w "MYSETTING/DEVSETTING payload bytes are shown but not decoded"
```

## Phase 6: Virtual pendrive link and export dialog combobox

Read: `src/ui/trakcrate_export_dialog.h/.cpp` (uncommitted graceful-export changes present; read the current contents, no git), `src/ui/virtual_pendrives_dialog.cpp`, `src/ui/trakcrate_pendrive_dialog.cpp`, `src/ui/trakcrate_panel.cpp` (`exportDrive`, lines 1026-1060, and the pendrive context menu), `src/trakcrate/library.cpp` drive functions (`createDrive`, `renameDrive`, `deleteDrive`).

1. Library: `driveSettingsSet(drive)` / `setDriveSettingsSet(drive, set)` reading and writing `drives/<name>/drive.txt` (key/value; keep unknown keys; atomic write via `atomicWrite`). `renameDrive` carries the file along (it moves the folder), `deleteSettingsSet` blocks while linked (phase 1 hook now has real data).
2. Export dialog: a **Settings** group with a combobox (sets, default first, plus "Built-in defaults (no set)") and a radio pair **Add missing files** (default) / **Update from trakcrate (overwrite)**. When the dialog's virtual pendrive is chosen, preselect its linked set. The choice goes into `ExportOptions::settingsSet` and `settingsPolicy`. The old advanced "settings" checkbox stays under Advanced but is relabelled as compatibility testing only.
3. Panel `exportDrive`: remove the flat-file reading at lines 1039-1047 (the library function now resolves the set); if no sets exist yet, create one named "Default" from the built-ins (with the user name prompt skipped, `$USER`) so the library always has one default after the first export.
4. Virtual pendrive UI: in the pendrive context menu and `VirtualPendrivesDialog`, a **Settings set** combobox/menu action that stores the link; tooltip on the tree item shows it.
5. Result summary adds "settings: kept N, added M".
6. Tests: link round trip, rename carries the link, deleting a linked set is refused, dialog preselects the linked set and produces matching `ExportOptions`, export of a linked pendrive writes that set's `djprofile.nxs`.

Done when: all trakcrate tests pass; manual check noted for the user: export to a stick with an existing drive using both policies.

```sh
geany-progress done 6 -r src/ui/trakcrate_export_dialog.cpp -r src/ui/trakcrate_panel.cpp -r src/trakcrate/library.cpp -r tests/test_trakcrate_export.cpp -w "Real-deck test of Replace vs AddMissing is manual"
```

## Phase 7: Documentation and final sweep

Read: `TRAKCRATE-FORMAT.md` (sections 2.1, 2.4, 10), `doc/trakcrate/trakcrate-db-format.md`, `doc/trakcrate/gui.md`, `doc/trakcrate/cli.md`, `doc/trakcrate/import-from-rekordbox.md`, `doc/trakcrate/settings-files.md`.

1. `TRAKCRATE-FORMAT.md`: rewrite section 10 for sets (`default.txt`, `<set>/set.txt`, the five files, fall back to built-ins, never optional), add `drive.txt` to section 2.4 with the `settings:` key, update the tree diagram in 2.1, and add a migration note (flat files, unpublished format, so a version note rather than compatibility promises).
2. `doc/trakcrate/trakcrate-db-format.md`: update the tree (`user-settings/<set>/`, `drives/<name>/drive.txt`) and fix the stale statement that settings are optional.
3. `doc/trakcrate/settings-files.md`: the "Decision" section now describes sets, policies and the viewer; the stale "Suggested handling" and "To do" lists get pruned or marked done.
4. `doc/trakcrate/gui.md` and `import-from-rekordbox.md`: the settings tree, new/copy flows, export combobox and policy, the viewer. `doc/trakcrate/cli.md`: one note that the CLI still uses its compiled-in files and ignores sets (porting is future work); do not touch `src-cli/`.
5. Sweep: `grep -rn "settingsDir()" src tests` for leftovers using the flat path; `grep -rn "user-settings" doc TRAKCRATE-FORMAT.md src`; run the full `ctest` and report failures verbatim if any. Align markdown tables with the `mdalign` skill.
6. Append general refactor suggestions noticed on the way (not done now) to `ai-output/refactor-recommendations.md`.

Done when: docs agree with code, `ctest` is green.

```sh
geany-progress done 7 -r TRAKCRATE-FORMAT.md -r doc/trakcrate/trakcrate-db-format.md -r doc/trakcrate/settings-files.md -r doc/trakcrate/gui.md
```

## Risks

- **djprofile checksum**: with one sample the 4 bytes at 0x1c may stay undecoded; the plan degrades to a warned, deck-untested name edit rather than guessing.
- **Real-deck verification** of the name change and of the `Replace` policy needs hardware; each phase says what is automated and what is manual.
- **Uncommitted work** in `exporter.*`, the export dialog and the panel (graceful export) overlaps phases 3, 4 and 6: the implementing agent must read the current file contents first, avoid reverting that work, and must not use git at all.
