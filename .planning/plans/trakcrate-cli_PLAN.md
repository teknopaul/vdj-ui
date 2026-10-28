# trakcrate CLI: development plan

Requirements: `doc/trakcrate/cli.md` (written before the GUI was finished; Phase 0 updates it). Goal: a `trakcrate` command (bash wrapper + bash
commands + one compiled helper) in its own package `trakcrate-cli`, usable headless, and the GUI as a front end on it.

## Findings (investigation, 2026-10-28)

### Is `references/libcdj` the right code to continue with? Partly: it is the read/verify side, not the write side

| Need of the CLI                       | In libcdj?                                                                                                  | Where it really is                         |
|---------------------------------------|--------------------------------------------------------------------------------------------------------------|--------------------------------------------|
| Read ANLZ (.DAT/.EXT): beat grid, cues, loops, waveforms, path | **Yes**: `anlz.{c,h}` (761 lines), installed as `<cdj/anlz.h>`, 7 real fixture pairs, snip tests, ASAN target | libcdj                                     |
| Read export.pdb (tracks, artists, albums, genres, keys, colours, artwork, playlists, entries) | **Yes**: `nfs/pdb.{c,h}` (typed table readers), installed as `<cdj/pdb.h>`; `pdb_parse_test` | libcdj                                     |
| Write ANLZ                            | **No**. `anlz_fake.c` is a test builder written for the parser tests only                                    | `src/trakcrate/anlz_writer.cpp` (vdj-ui)    |
| Write export.pdb                      | **No**. `fake_pdb.c` makes a one-track PDB for the NFS server tests                                          | `src/trakcrate/pdb_writer.cpp`, `pdb_build.cpp` (vdj-ui; byte exact round trip, verified against libcdj's reader) |
| Audio decode, beat/BPM, waveform, loudness | No                                                                                                      | `audio_decode`, `analysis*`, `bpm_detect` (vdj-ui; ffmpeg + optional aubio) |
| Library model, import, export, rekordbox import, scripts | No                                                                                            | `library`, `importer`, `exporter`, `rb_import`, `scripts` (vdj-ui) |
| Inspection tools to "wrap or replace"  | **`xdj-pdb` and `xdj-explore` / `xdj-tracks` work over NFS from a live deck (need an IP), not on files.** `xdj-anlz` (standalone byte scanner) and `xdj-track` (libcdj summary + cross-check of a whole export directory) work on files | libcdj `src/bin` |
| Not available: PSSI phrases, PWV6/PWV7 (3-band), memory-point/comment/colour samples | no real data yet (see memory note on the ANLZ parser) | open in libcdj                              |

Conclusion:

1. **libcdj is the right foundation for reading and verifying, and nothing needs to move there for the CLI to proceed.** It is already installed
   system-wide (`/usr/lib/libcdj.so`, `/usr/include/cdj/{anlz,pdb}.h`) and `vdj-trakcrate` already links it (`cdj_imported`) for `verifyPdb` and
   `rb_import`. The CLI package gets `Depends: libcdj` (libcdj has its own `make deb`; its version is `0.1`, so the two version policies differ: pin
   `libcdj (>= 0.1)`).
2. **The CLI's real core already exists, and it is C++17, Qt-free, 6.3k lines, in `vdj-ui/src/trakcrate/`** (`add_library(vdj-trakcrate STATIC ...)`,
   with the comment "so it can move to the command line tools"). `cli.md` says "C helper on a `libtrakcrate`"; that library is already written, in C++.
   Rewriting 6k lines of tested code into C to match a sentence in a requirements doc would be the wrong move. **Recommendation: the helper is a
   thin C++17 program over this library; "bash first" still holds for the commands.** (Decision D1.)
3. libcdj gaps that are *not* blockers: `pdb dump/diff` and `anlz diff` on files do not exist yet (new code, in the helper, on top of `<cdj/pdb.h>`
   and `<cdj/anlz.h>`; if a general dump function belongs in libcdj, add it there with tests and ask for `sudo make install`).

### What blocks a headless build today (the only two Qt dependencies of the core)

| Blocker                                  | Where                                                                                     | Fix                                                                                              |
|------------------------------------------|-------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------|
| Square JPEG encoding for artwork         | `ArtworkEncoder` callback in `exporter.h`; the only implementation is in `src/ui/trakcrate_artwork.cpp` (QImage) | Qt-free encoder: ffmpeg is already a dependency (mjpeg encoder + `libswscale` for the resize) or libjpeg-turbo + stb_image. (D2) |
| Template `export.pdb` bytes              | `ExportOptions::templatePdb` is read from a Qt resource in `trakcrate_panel.cpp:889`      | Embed the file as a generated C array in the helper (like `settings_defaults.cpp` already does for DEVSETTING.DAT). |

### Where the GUI stands against the `cli.md` / `gui.md` decision

The decision was "the GUI links no trakcrate library and runs `trakcrate <command>`". The finished GUI does the opposite: `vdj-cdj` links `vdj-trakcrate`,
and `src/ui/trakcrate_*.cpp` call `Library`, `analyseTrack`, `exportPlaylists` etc. directly. Only the scripts menu uses `QProcess`.
Nothing is lost by this (the logic is in the shared Qt-free library either way) but the docs describe a different architecture. Decision D3 below.

There is no `main()` for any trakcrate command yet (only `src/tools/usb_watch.cpp`). All trakcrate tests are GTest-style in `tests/` through the GUI build.

## Decisions needed before Phase 1

| #  | Question                                                                 | Recommendation                                                                                         |
|----|--------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------|
| D1 | Helper language: C (as `cli.md`) or C++17 (as the code is)?              | C++17 over the existing library. Update `cli.md`.                                                      |
| D2 | Qt-free JPEG: ffmpeg mjpeg+swscale, or libjpeg-turbo?                    | ffmpeg (already a dependency, fewer packages). Check that output is byte-stable across runs.          |
| D3 | GUI: really move to child-process calls (`gui.md`), or keep linking the library? | Keep linking `vdj-trakcrate` for now (it works, and in-process progress/cancel is better for the UI); make **the CLI the reference for behaviour and the scripting API**, and change `gui.md`/`cli.md` to say "same library, two front ends". Move individual GUI actions to the CLI only when a reason appears. Reversible later, because the logic never lived in the UI. |
| D4 | Source layout: `trakcrate/` top-level as in `cli.md`, with the library moved there and vdj-ui consuming it? | Yes, but as a late step (Phase 9). Early phases build the helper from `src/trakcrate/` in place, so the GUI work is not disturbed (the tree has a lot of uncommitted changes). |
| D5 | `--where` query language                                                 | Defer. Phase 5 ships `--format tsv/jsonl`; filtering via `awk`/`jq` until a need is shown.             |

## Phases

Each phase ends with its tests passing and the progress file `.planning/state/TRAKCRATE_CLI_PROGRESS.md` updated.

### Phase 0: update the requirements
- Rewrite `cli.md` to the decisions D1-D5; mark the sections that changed. Remove what is implemented in the GUI-era library from "Implementation notes".
- Specify the **versioned machine interface**: the `--format jsonl` field names per command and the `--progress-fd` line format (listed as open in `cli.md`). Without this the GUI/other tools cannot rely on it.
- Define the command-to-function map (Phase 3 table) and exit codes (0/1/2/3, plus a documented "library busy" status for the lock).

### Phase 1: Qt-free helper skeleton
- New `trakcrate/` build (CMake, no Qt, `add_subdirectory` reachable from the top level and buildable standalone) producing `trakcrate-helper` from the existing sources.
- Replace the two blockers: Qt-free `ArtworkEncoder`; embedded template PDB.
- `main.cpp` with subcommand dispatch, exit status convention, TSV/JSON writers, `--progress-fd`.
- Test: `ldd trakcrate-helper` shows no Qt; runs with `DISPLAY` unset.
- Gate: `export` of a fixture library through the helper produces a drive that `verifyPdb` accepts, and its ANLZ/PDB bytes equal the GUI build's output for the same input (determinism check).

### Phase 2: wrapper, conventions, packaging skeleton
- `trakcrate/bin/trakcrate` (bash): `--help`, `--version` (from `./version`), `--list-commands`, `help CMD`, search order (`$TRAKCRATE_PATH`, `/usr/local/lib/trakcrate`, `/usr/lib/trakcrate`), exported `TRAKCRATE_*` variables, did-you-mean. `set -eu`, `shellcheck` clean, `c-bash` conventions.
- Wrapper tests with fake commands (cli.md "Testing"); a tiny bash test runner (the repo has none for shell yet: pick `bats` or a plain script, decide here).
- `trakcrate/make-deb.sh` and conffile `/etc/trakcrate/trakcrate.conf`; empty `/usr/local/lib/trakcrate/`.

### Phase 3: helper subcommands over existing code

| Helper subcommand            | Existing code                                            | Command that wraps it |
|------------------------------|----------------------------------------------------------|-----------------------|
| `probe FILE`, `tags FILE`    | `audio_probe`                                            | `add`, `import`       |
| `analyse TRACKDIR`           | `analysis.cpp` (`analyseTrack`), `analysis_beats`, `analysis_wave`, `bpm_detect` | `analyse`, `beatgrid`, `waveform`, `loudness` |
| `bpm`, `grid-edit`           | `grid_edit`, `bpm_detect`                                | `beatgrid`            |
| `cues`                       | `cuemark_edit`, `cuemark_render`                         | `cues`                |
| `export`                     | `exporter`, `pdb_build`, `pdb_writer`, `anlz_writer`, `settings_files`, `sha256` | `export`, `verify` |
| `import-rekordbox`           | `rb_import` (+ libcdj readers)                           | `import-rekordbox`    |
| `artwork`                    | `artwork_edit`, `artwork_paths`                          | (artwork handling in `add`) |
| `pdb dump|diff`, `anlz dump|diff` | **new**, on `<cdj/pdb.h>`, `<cdj/anlz.h>` and `pdb_writer` page model | `pdb`, `anlz`, `unknown-bytes` |

`sync`, `check`, and the lock are new (below). Thread pool for `--jobs` with ordered output is new (analysis is per track and independent).

### Phase 4: bash commands, simple ones first
Listing, metadata and symlink work is plain file handling (`trakcrate-db-format.md`), so these are bash:
`init`, `ls`, `info`, `set`, `rm`, `pendrive new|add|ls`, then `import` (folder to master + auto playlist: natural sort, `000_` prefixes, `--append/--replace`; the natural sort and SHA-256 dedupe come from the helper: `natsort`, `sha256`).
Order: `init`, `ls/info`, `set`, `import`, `pendrive`, `rm`. Every command: `--help`, `--summary`, `--dry-run` where it writes, `--format`.

### Phase 5: analysis and export commands
`analyse`, `beatgrid`, `waveform`, `loudness`, `cues`, `export`, `verify`, `import-rekordbox`, `sync` (rsync semantics, orphan warning as in `rekordbox-db-output.md`), `check`.
Output determinism is already required of `analyseTrack`; add a golden test that two runs are byte identical.

### Phase 6: robustness
- **Library lock** (write commands; read-only never lock) and the "library busy" status. The GUI must honour the same lock if D3 keeps in-process calls.
- Atomic writes (temp + rename) audit across `library.cpp`/`track.cpp`; sandboxed paths (`isSafeName` exists; check `..`/symlink escape for imported drives).
- `--progress-fd` implemented on `analyse`, `import`, `export`.

### Phase 7: inspection tools (absorb libcdj's `xdj-*`)
- `pdb dump/diff`, `anlz dump/diff`, `unknown-bytes` (compares with `doc/trakcrate/UNKNOWN_BYTES.md`).
- Keep libcdj's `xdj-pdb`/`xdj-explore`/`xdj-tracks` (NFS from a live deck) as they are: they are not file tools and belong to libcdj. Document the split; `trakcrate verify` and `anlz dump` replace `xdj-track`/`xdj-anlz` for files and the old names stay installed by libcdj.

### Phase 8: tests and packaging
- Bash tests through the wrapper on fixture libraries; helper unit tests with the existing GTest tests moved so they build without Qt where possible (the trakcrate tests that do not touch widgets: `test_trakcrate_{library,import,export,analysis,anlz_roundtrip,pdb_*,rbimport,scripts,cuemarks}`).
- Golden files for every `--format` output; `--dry-run` writes nothing; export then `verify` round trip.
- Packaging test: build the `.deb`, install in a clean container, run `trakcrate init`, remove, check nothing is left in a home directory.
- Man pages (`trakcrate.1` generated from `./version`), bash completion.

### Phase 9: GUI and repository tidy (after D3)
- Move `src/trakcrate/` to `trakcrate/src/` (or keep it and build the CLI from it), make `vdj-ui` consume it as a library, add `Depends: trakcrate-cli (= same version)` to the GUI package.
- Update `gui.md` (and the scripts menu: "CLI equivalents" are listed as not done in the progress file) so the GUI's scripts menu can run `trakcrate` commands.

## Risks and notes
- **Uncommitted work**: the tree has many modified trakcrate files. Do not move or rename sources until the user commits (Phase 9 only).
- **Single data format owners**: the PDB/ANLZ writers have unknown bytes (`UNKNOWN_BYTES.md`), still validated only against 2 sample drives. Moving them into a CLI does not change that; hardware testing (`hardware-test.md`) is independent.
- **libcdj rules**: never run git there; changes to libcdj headers need the user to run `! cd ../libcdj && sudo make install` before vdj-ui can use them.
- **aubio** is optional at build time (`TC_HAVE_AUBIO`); the `.deb` must decide whether it is a hard dependency, because analysis results differ with and without it (determinism across machines).
- **PSSI / 3-band waveforms**: still waiting for the user's pendrive data; the CLI will gain them only after libcdj does.
