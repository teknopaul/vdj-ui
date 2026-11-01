# Rename the library's `pendrives/` directory to `playlists/`

Investigation only. No source file has been changed.

## Summary

- The library's playlist directory is `<library>/pendrives/`. It must become `<library>/playlists/`.
- The same name is used a second time, inside each user defined pendrive: `<library>/drives/<name>/pendrives/<NNN_entry>/`.
  - `Library::driveLibrary()` builds a `Library` rooted at `drives/<name>`. It then calls the same `pendrivesDir()` accessor.
  - Changing `pendrivesDir()` therefore moves the drive entries directory too. So `drives/<name>/pendrives` becomes `drives/<name>/playlists`.
  - Each entry is "one playlist on the exported drive" (`TRAKCRATE-FORMAT.md` 2.4), so `playlists` is the right name there as well. **Recommendation: rename both.** The alternative is a second accessor and a second name for the entries (for example `entries/`). It is more code and it gains nothing.
- Symlinks are all relative (`../../../track-master-copy/...`). I checked the live DB: 4740 symlinks, 0 absolute. The depth does not change, so **no symlink needs rewriting**.
- No file inside the library stores the string `pendrives` as content. `drive.txt`, `entry.txt`, `meta.txt` and the `user-settings/*` files do not contain it, so there is nothing to rewrite. The change is purely a directory rename.
- Out of scope, because they are not the filesystem directory: the GUI's "Virtual Pendrives", `PendriveRegistry` and the config section `virtual_pendrives`. The `trakcrate pendrive` CLI subcommand is the same case. It manages the user defined drives, and its name is correct.
  - Its script file `trakcrate-pendrive` contains path uses that do need changing. Those are listed below.

## 1. One-off migration of the existing DB

Run with nothing (GUI, CLI) using the library:

```sh
cd /var/vdj/trakcrate
mv pendrives playlists
for d in drives/*/; do mv "$d/pendrives" "$d/playlists"; done
```

Check afterwards:

```sh
find /var/vdj/trakcrate -name pendrives     # expect no output
find -L /var/vdj/trakcrate/playlists -xtype l | head     # expect no broken links
```

Current state of the DB:

- Top level: `drives pendrives track-master-copy user-settings`.
- Drives: `1track`, `full`, `Mini4`, `Tester`. Each has a `pendrives/` subdirectory.

Optional hardening, not required: have `Library::open` or `trakcrate init` notice a legacy `pendrives/` and print a hint. Only the author has a DB, so I would skip it. If you want it, `Library::open` is the place to put it.

## 2. C++ library (`src/trakcrate/`)

| File                         | Line                    | Change                                                                                                    |
|------------------------------|-------------------------|-----------------------------------------------------------------------------------------------------------|
| `src/trakcrate/library.h`    | 16                      | comment `<root>/pendrives/<name>/symlinks/...` becomes `playlists/`                                       |
| `src/trakcrate/library.h`    | 18                      | comment "A playlist is a pendrives/<name> entry" becomes `playlists/<name>`                               |
| `src/trakcrate/library.h`    | 31                      | `pendrivesDir() { return root_ / "pendrives"; }` becomes `playlistsDir() { return root_ / "playlists"; }` |
| `src/trakcrate/library.h`    | 66                      | comment `drives/<name>/pendrives/<NNN_entry>/symlinks/...` becomes `playlists/`                           |
| `src/trakcrate/library.h`    | 113                     | `symlinksDir`: `pendrivesDir()` becomes `playlistsDir()`                                                  |
| `src/trakcrate/library.cpp`  | 22                      | `Library::create`: the `"pendrives"` literal in the list of subdirectories becomes `"playlists"`          |
| `src/trakcrate/library.cpp`  | 77, 82, 89, 99          | `pendrivesDir()` becomes `playlistsDir()` (listPlaylists, hasPlaylist, createPlaylist, deletePlaylist)    |
| `src/trakcrate/library.cpp`  | 273                     | `createDrive`: `drivesDir() / name / "pendrives"` becomes `"playlists"`                                   |
| `src/trakcrate/library.cpp`  | 328, 352, 357, 385, 387 | `d.pendrivesDir()` becomes `d.playlistsDir()` (driveEntries, reorder, addDriveEntry)                      |
| `src/trakcrate/exporter.h`   | 28                      | comment `(library "pendrives")` becomes `(library "playlists")`                                           |
| `src/ui/trakcrate_panel.cpp` | 1691, 1693              | `lib_.pendrivesDir()` becomes `lib_.playlistsDir()`                                                       |

Renaming the accessor is optional, since the literal alone would work, but it removes the confusing name. If you prefer a smaller diff, keep the accessor name and change only the literals at `library.h:31`, `library.cpp:22` and `library.cpp:273`. I recommend the full rename. It is a mechanical `sed` and the compiler finds any misses.

Check: `grep -rn pendrivesDir src tests` must return nothing afterwards.

## 3. C++ tests

Accessor rename only: `lib.pendrivesDir()` becomes `lib.playlistsDir()`.

- `tests/test_trakcrate_import.cpp:157`
- `tests/test_trakcrate_library.cpp:251, 302, 303, 304, 305, 318, 350`

Comment only: `tests/test_trakcrate_library.cpp:459` ("the pendrives are not playlists of the library") is about drives. It stays as it is.

## 4. CLI scripts (`src-cli/script/usr/lib/trakcrate/`)

| File                         | Line          | Change                                                                                                               |
|------------------------------|---------------|----------------------------------------------------------------------------------------------------------------------|
| `common.sh`                  | 484           | `for d in "$tc_lib"/pendrives/*/ "$tc_lib"/drives/*/pendrives/*/` becomes `playlists/*/` and `drives/*/playlists/*/` |
| `trakcrate-init`             | 10            | help text `pendrives/` becomes `playlists/`                                                                          |
| `trakcrate-init`             | 60            | `mkdir -p ... "$dir/pendrives"` becomes `"$dir/playlists"`                                                           |
| `trakcrate-import`           | 11            | help text `pendrives/<NAME>/` becomes `playlists/<NAME>/`                                                            |
| `trakcrate-import`           | 110           | `playlist_dir=$tc_lib/pendrives/$playlist` becomes `playlists`                                                       |
| `trakcrate-import-rekordbox` | 16, 23        | help text `pendrives/` becomes `playlists/`                                                                          |
| `trakcrate-import-rekordbox` | 217, 218, 219 | `$tc_lib/pendrives/$pname` becomes `playlists`                                                                       |
| `trakcrate-export`           | 11            | help text `(pendrives/NAME)` becomes `(playlists/NAME)`                                                              |
| `trakcrate-export`           | 80            | `pdir=$tc_lib/pendrives/$name` becomes `playlists`                                                                   |
| `trakcrate-sync`             | 77            | `pdir=$tc_lib/pendrives/$name` becomes `playlists`                                                                   |
| `trakcrate-info`             | 78, 80        | `"$tc_lib"/pendrives/*/symlinks/*` and `${p#"$tc_lib"/pendrives/}` become `playlists`                                |
| `trakcrate-pendrive`         | 10            | help text `create the playlist pendrives/NAME/` becomes `playlists/NAME/`                                            |
| `trakcrate-pendrive`         | 54            | `printf '%s\n' "$tc_lib/pendrives/$1"` becomes `playlists`                                                           |
| `trakcrate-pendrive`         | 151           | `for dir in "$tc_lib"/pendrives/*/` becomes `playlists`                                                              |

`trakcrate-sync` and `trakcrate-pendrive` may also have `drives/.../pendrives` uses that my grep missed because they are built from variables. Grep `src-cli/script` again for `pendrives` after the edit and read every remaining hit. Hits that are about the `pendrive` subcommand or the user defined drives are fine.

## 5. CLI tests (`src-cli/test/`)

| File                       | Lines                                                                                      |
|----------------------------|--------------------------------------------------------------------------------------------|
| `test_import.sh`           | 91, 128, 190 (`"pendrives/second"` is the expected text in the output), 228, 229, 230, 247 |
| `test_export.sh`           | 159, 164                                                                                   |
| `test_import_rekordbox.sh` | 45, 75, 84, 97, 119                                                                        |
| `test_library_commands.sh` | 27 (`assert_file "pendrives" ...` is the label and the path), 186, 192, 198, 208, 215      |

`test_sync.sh` and `export_fixture.sh` mention pendrive only in other senses, so they are probably untouched. Confirm with a grep.

## 6. Documentation

| File                                     | Line   | Change                                                                                                                                                                  |
|------------------------------------------|--------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `TRAKCRATE-FORMAT.md`                    | 40     | `├── pendrives/   the library's playlists (the directory keeps its historic name)` becomes `├── playlists/   the library's playlists`. Drop the "historic name" remark. |
| `TRAKCRATE-FORMAT.md`                    | 47     | `drives/<pendrive>/pendrives/` becomes `playlists/`                                                                                                                     |
| `TRAKCRATE-FORMAT.md`                    | 69     | "A playlist is a directory `pendrives/<name>/`" becomes `playlists/<name>/`                                                                                             |
| `TRAKCRATE-FORMAT.md`                    | 79     | "its `pendrives/` directory holds the **entries**" becomes `playlists/`                                                                                                 |
| `doc/trakcrate/trakcrate-db-format.md`   | 19, 26 | same as `TRAKCRATE-FORMAT.md` lines 40 and 47 (remove the "historic name" text)                                                                                         |
| `doc/trakcrate/scripts.md`               | 18     | tree `└── pendrives/` becomes `playlists/`                                                                                                                              |
| `doc/trakcrate/gui.md`                   | 28     | "the playlists (`pendrives/*`)" becomes `playlists/*`                                                                                                                   |
| `doc/trakcrate/cli.md`                   | 102    | `init` row                                                                                                                                                              |
| `doc/trakcrate/cli.md`                   | 125    | `pendrives/<NAME>/symlinks/`                                                                                                                                            |
| `doc/trakcrate/cli.md`                   | 157    | `pendrive new NAME` row, "create a playlist `pendrives/NAME/`"                                                                                                          |
| `doc/trakcrate/cli.md`                   | 166    | "(`drives/*/pendrives/`)" in "Not implemented yet"                                                                                                                      |
| `doc/trakcrate/cli.md`                   | 170    | "a playlist in `pendrives/`"                                                                                                                                            |
| `doc/trakcrate/import-from-rekordbox.md` | 36, 95 | `pendrives/<name>/`                                                                                                                                                     |
| `.planning/plans/trakcrate-ui_PLAN.md`   | 80     | `pendrives/`                                                                                                                                                            |
| `.planning/plans/trakcrate-cli_PLAN.md`  | 117    | `drives/*/pendrives/`                                                                                                                                                   |

These are the only places where the word `pendrives` means the filesystem directory. Everything else in `README.md`, `vdj-ui.1`, `doc/vdj-ui.1.in`, `ai-prompts/` and `.planning/state/` is about physical or virtual pendrives and should stay. Historical plan and state files in `.planning/` could be left alone as history. I listed the two plan lines because they read as current format documentation.

Search for anything missed with:

```sh
grep -rInE "pendrives/|/pendrives|\"pendrives\"|historic" --exclude-dir=.git --exclude-dir=ai-output .
```

Every remaining hit should be about a physical or virtual pendrive.

## 7. Not affected

- Packaging: `src-cli/deploy/DEBIAN/control.in`, `data/*.desktop` and `resources.qrc` only say "pendrives" in prose.
- The `export.pdb` or `PIONEER/` output on a real drive. The exporter reads the library through the `Library` API and writes elsewhere.
- Config: the `virtual_pendrives` section holds folder paths of rekordbox exports, not trakcrate library paths.
- Settings sets (`user-settings/`) and `drives/<name>/drive.txt`.
- Symlink targets, as noted in the summary.

## 8. Suggested order

1. Edit the C++ (sections 2 and 3), build, and run the C++ tests.
2. Edit the CLI scripts and tests (sections 4 and 5), then run `src-cli/test/*.sh`.
3. Edit the docs (section 6).
4. Migrate `/var/vdj/trakcrate` (section 1), then open it in the GUI and with `trakcrate info`. Playlists and the four drives should show as before.

Do the migration only after the new binaries are installed. Old binaries would otherwise create an empty `pendrives/` again on `init` or `open`.
