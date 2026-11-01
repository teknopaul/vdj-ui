# trakcrate CLI: development plan

Requirements: `doc/trakcrate/cli.md` (written before the GUI was finished; Phase 0 rewrites it to this plan).

## Decisions (user, 2026-10-28)

1. **The helper is written in C**, not C++. The commands are bash.
2. **The CLI is a separate set of tools, not a front end for the GUI's code.** The trakcrate database is a plain directory tree with a simple, documented format
   (`doc/trakcrate/trakcrate-db-format.md`), so *anyone* can write scripts that change it. The CLI is a set of helpers for those script writers: the things bash
   cannot do (audio decode and analysis, tags, rekordbox PDB/ANLZ reading and writing, checksums) plus conveniences. The GUI (C++/Qt, `src/trakcrate/`) and the CLI
   are two independent implementations of the same documented format. **The format documents are the contract; there is no shared code and no dependency either way.**
   (This supersedes the "GUI runs the CLI as a child process" decision in `gui.md` / `cli.md`, and the earlier idea of moving `src/trakcrate/` into the CLI.)
3. **New base directory `./src-cli/`** with `c/` and `script/`. `script/` mimics the installed file system, so a package is a copy of that tree.

## Layout

```
src-cli/
├── c/                                   the C helper and its library (no Qt, no C++)
│   ├── Makefile                         like libcdj: plain make, gcc -Wall -Werror, output in target/
│   ├── lib/                             library of the db format (meta.txt, beatgrid.txt, cuemarks.txt, lock, atomic write, natsort, sha256, safe paths)
│   ├── helper/                          main.c and one file per helper subcommand (analyse, probe, export, pdb, anlz, ...)
│   └── test/                            snip tests (.c.snip, as in libcdj), fixtures
├── script/                              = the root of the installed system
│   ├── usr/bin/trakcrate                the wrapper (bash)
│   ├── usr/lib/trakcrate/trakcrate-*    the bash commands  (trakcrate-helper is built from c/ and installed beside them)
│   ├── usr/share/man/man1/              trakcrate.1 and per-command pages (@VERSION@ substituted at build)
│   ├── usr/share/bash-completion/completions/trakcrate
│   ├── usr/share/doc/trakcrate-cli/     README, the db format docs, example scripts
│   └── etc/trakcrate/trakcrate.conf     conffile
├── test/                                bash tests: wrapper, commands against fixture libraries
└── deploy/                              DEBIAN/control, build-deb.sh (libcdj has the same shape)
```

Installing is `cp -a script/. $DESTDIR/` plus `make -C c install DESTDIR=...` for the helper (and the helper's libs) into `usr/lib/trakcrate/`; `build-deb.sh` stages
both and runs `dpkg-deb --build`. The version comes from the project's `./version` file. `/usr/local/lib/trakcrate/` is created empty by the package (user commands),
as in `cli.md`. Everything else in `cli.md` about the wrapper (search order, `TRAKCRATE_*` variables, extension contract, exit codes, `--format`) stands.

## What libcdj gives us (investigated)

libcdj is C, so the new helper fits it directly. `Depends: libcdj` (shared library, `/usr/lib/libcdj.so`, headers in `/usr/include/cdj/`).

| Need                                               | libcdj                                                                                         |
|----------------------------------------------------|------------------------------------------------------------------------------------------------|
| Read ANLZ (.DAT/.EXT): beat grid, cues, loops, waveforms, path | **Yes**: `<cdj/anlz.h>`, real fixtures, snip tests, ASAN target              |
| Read export.pdb (tracks, artists, albums, genres, keys, colours, artwork, playlists) | **Yes**: `<cdj/pdb.h>` typed table readers                          |
| Write ANLZ / export.pdb                            | **No** (`anlz_fake.c`, `fake_pdb.c` are test builders). New C code in `src-cli/c`, see below   |
| Decode audio, beats, waveform, loudness, tags      | No. New C code; ffmpeg's C API (libavformat/libavcodec/libswresample) and optionally aubio     |
| File inspection tools                              | `xdj-anlz`, `xdj-track` work on files; `xdj-pdb`, `xdj-explore`, `xdj-tracks` are NFS-from-a-deck tools and stay in libcdj. `pdb dump/diff`, `anlz diff` on files are new |
| Not available yet                                  | PSSI phrases, PWV6/PWV7 (3-band): no real sample data (see the ANLZ parser memory note)        |

The C++ code in `src/trakcrate/` (6.3k lines, working, tested against two real drives) is **a reference, not a dependency**: its algorithms, byte layouts and
`doc/trakcrate/pdb-write-notes.md`, `UNKNOWN_BYTES.md` are the source for the C port. While both exist, **the C++ implementation is the test oracle**:
for the same input library the C tool must produce the same `analyse` results (beatgrid.txt, waveform.bin) and byte-identical `export` output (export.pdb, ANLZ, artwork
modulo JPEG encoder). Where they differ, the doc decides and the loser is fixed.

## Keep an eye on duplication (standing rule for every phase)

General library code is better placed in libcdj, and much of it is in C++ in `src/trakcrate/` today. **That is fine for now and is not fixed in this work.**
Instead, while building `src-cli/c`, whenever code is not specific to the CLI (a format reader/writer, an algorithm, a utility), or exists twice, or libcdj
is missing something the CLI needs, add it to **`ai-output/refactor-recommendations.md`** (created, with the candidates known so far). Each phase's progress
update checks that file. Nothing is moved into libcdj unless the user asks.

## Open decisions

| #  | Question                                              | Recommendation                                                                                                  |
|----|-------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------|
| D1 | Build system for `src-cli/c`                          | **Decided (Phase 1): plain Makefile like libcdj** (`make`, `make test`, `make test-asan`, `make install DESTDIR=`). |
| D2 | JPEG scaling/encoding for artwork in C                | ffmpeg's mjpeg encoder + libswscale (ffmpeg is already needed for decode); the alternative is libjpeg-turbo.     |
| D3 | Test runner for the bash side                         | **Decided (Phase 1): plain-bash harness** `src-cli/test/lib.sh` + `run.sh` (bats is not installed).             |
| D4 | aubio: hard dependency of the package or optional?    | **Decided (Phase 4): neither. The C analyser is a port of the GUI's own detector (no aubio), so results do not depend on the machine.** Original question: | Analysis output must be the same on every machine, so decide once: either always use it, or write our own detector in C and drop it. Compare with the C++ results first. |
| D5 | `--where` query language                              | Defer; ship `--format tsv/jsonl` and let users use `awk`/`jq`.                                                  |

## Phases

Each phase ends with its tests passing and `.planning/state/TRAKCRATE_CLI_PROGRESS.md` updated. The GUI code is not touched by any phase.

### Phase 0: documents (the contract)
- Rewrite `cli.md` to this plan: C helper, `src-cli/` layout, independent of the GUI; remove "GUI Depends: trakcrate-cli", "libtrakcrate used only by the CLI" and the rest of the stale text; keep the command reference.
- Make `trakcrate-db-format.md` precise enough to implement from: file names, field syntax, encodings, ordering rules (the `000_` prefixes), what a reader must ignore. List the gaps found while writing the C code back into it.
- Specify the machine interface: `--format jsonl` field names per command, `--progress-fd` lines, exit codes including "library busy". Versioned (`TRAKCRATE_API`).

### Phase 1: skeleton
- `src-cli/` tree, `script/usr/bin/trakcrate` wrapper (`--help`, `--version`, `--list-commands`, `help CMD`, search order, exported variables, did-you-mean), `shellcheck` clean, `c-bash` conventions.
- `c/Makefile`, a `trakcrate-helper --version` that builds and installs, `make test`.
- Wrapper tests with fake commands in a temp dir via `$TRAKCRATE_PATH` (search order, shadowing, exit status passthrough, args with spaces and leading dashes).
- `deploy/build-deb.sh` producing `trakcrate-cli_<version>_amd64.deb`; packaging test: install in a clean container, `trakcrate --version`, remove, nothing left in any home.

### Phase 2: the database commands, mostly bash
Plain file handling, as a script writer would do it: `init`, `ls`, `info`, `set`, `rm`, `pendrive new|add|ls`, `import` (folder to master storage plus an auto playlist: natural sort, `000_` prefixes, `--append/--replace`, dedupe by content hash).
The helper supplies only what bash lacks: `probe`/`tags` (read mp3 and wav tags and artwork, via ffmpeg), `sha256`, `natsort`, safe-path checks. Every command has `--help`, `--summary`, `--dry-run` if it writes, `--format`.
Order: `init`, `ls/info`, `set`, `import`, `pendrive`, `rm`, `check`.

**Status (2026-10-28): done.** `src-cli/script/usr/lib/trakcrate/`: `common.sh` (options, library and track lookup, key/value files with atomic replace, the library lock, playlist write/load, confirm), `init`, `ls`, `info`, `set`, `pendrive new|add|ls`, `rm` (to `.trash/`, links removed and playlists renumbered), `check`, `import` (copy/move/link, dedupe by sha256, recursive, ordering, `--append`/`--replace`, exit 3 on partial failure). Helper: `natsort`, `probe` (tags and cover via libavformat), with C unit tests (`natsort`, `kv`, `dispatch`, also under ASAN) and bash tests (`test_library_commands.sh`, `test_import.sh`, `test_helper.sh`). The `.deb` now depends on the helper's ffmpeg libraries (computed from the binary).
Left over, not blocking: `ls --sort`, `add` for single files, `import --analyse` (refused until Phase 4), `--follow-symlinks`/`--include`/`--exclude`/`--tags-from-filename`. The format library in C (Phase 3) will absorb the awk key/value code of `common.sh` where bash is too slow.

### Phase 3: the C library of the format
`src-cli/c/lib`: atomic write (temp + rename), library lock (writers only; documented "library busy" status), meta/beatgrid/cuemarks readers and writers, path sandboxing (`..`, symlink escapes), natural sort, sha256. Unit tests with snip; ASAN target.

**Status (2026-10-28): done.** `src-cli/c/lib/`: `fsutil` (`tc_safe_name`, `tc_read_file`, `tc_write_atomic`, `tc_path_within` incl. dangling links, the lock `tc_lock_acquire/release`), `kv` (parse/get/set/remove/save, comments and unknown keys kept, untouched lines byte for byte), `beatgrid` (read/write, `tc_beatgrid_beat_us`), `cuemarks` (parse/serialise, sorted, extra columns kept), `sha256`, `natsort`. Helper subcommands `kv get|set|unset` and `sha256`. Snip tests for every module (also under ASAN/UBSan) and `test_format_agreement.sh`: bash (`common.sh`) and C read and write key/value files identically (7 sample files, 9 operations, 17 keys).
Not done: the bash commands still use their own awk code (they work, and are tested against the C code); `waveform.bin` reading/writing (arrives with Phase 4, where the analysers produce it); `vocals.txt`.

### Phase 4: analysis in C
`analyse`, `beatgrid`, `waveform`, `loudness`, `cues` commands calling the helper: decode once (44.1 kHz stereo float), beat grid (BPM and phase), waveform data, peak/RMS. Deterministic output (same input, same bytes). `--jobs` with ordered output; `--progress-fd`.
Gate: results compared against the C++ `analyseTrack` on the fixture tracks (and rekordbox values from the 7-track drive, `beat-grid.md`, `waveforms.md`).

**Status (2026-10-28): done.** C: `lib/decode` (ffmpeg to 44.1 kHz stereo float, mono keeps its level), `lib/pcm.h`, `lib/beats` (the detector: onset envelope, autocorrelation, fold, least squares fit), `lib/wave` (PWAV/PWV2/PWV3/PWV5, `waveform.bin` write and parse, loudness), helper `analyse TRACKDIR`. Bash: `analyse` (several tracks or `--all`, `-j`, `--only`, `--force`, `--print`, `--timeout`, `--fail-fast`, `--progress`, `--progress-fd` JSON events, ordered output, formats), `beatgrid`, `waveform`, `loudness`, and `import --analyse`.
**Gate passed:** `test_oracle.sh` runs the GUI's C++ `analyseTrack` (built from `build/src/libvdj-trakcrate.a`, `test/oracle/`) and the C helper on 8 synthetic tracks (128/90/174/123.7 bpm, mono and stereo, quiet, 3 s, silence, noise): beatgrid.txt, waveform.bin, meta.txt and source.txt are **byte identical**. Not yet compared on real music or on rekordbox's own values (`beat-grid.md`, `waveforms.md`): the synthetic tracks are clean click tracks.
Not done: `waveform --png`, `beatgrid --bpm N` / `--first-beat`, `analyse --report FILE`, `--cache`, key and vocals analysers.

### Phase 5: rekordbox output in C
ANLZ writer (.DAT/.EXT), PDB writer/builder, settings files with CRC, artwork JPEGs, the staged export (build in tmpfs, verify with libcdj's readers, write once to an empty destination, read back and compare SHA-256), `verify`, `sync`.
Gate: export of a fixture library is byte-identical to the C++ exporter's output (artwork aside), and `verify` accepts it; round trip on the real drives' files (as `test_trakcrate_anlz_roundtrip`, `test_trakcrate_pdb_roundtrip` do).

**Status (2026-10-28): done.** C: `lib/rbpdb` (page model, DeviceSQL strings, template based builder), `lib/rbanlz` (.DAT/.EXT writer, beats from a grid, PWV4 approximation), `lib/rbsettings` (CRC-16/XMODEM, the two default files), `lib/rbcue` (cuemark spikes), `lib/artwork` (ffmpeg: centre crop, 80x80 baseline JPEG), `lib/rbdb` (libcdj based reader, comparison with a plan, `verify` of a whole drive), `lib/rbexport` (gather, stage on tmpfs, check with libcdj, copy once each with SHA-256, read back, clean up on failure; `--link-audio` for sync), `lib/fsops`, `lib/buf.h`; helper `export` and `verify`. Bash: `export`, `verify`, `sync` (work folder of links + rsync in three passes, `--delete-orphans` confined to `Contents/` and `PIONEER/`, asks unless `--yes`). `TRAKCRATE_DATA` (the template database, `/usr/share/trakcrate/template-export.pdb`) is a new variable of the extension contract; the `.deb` now depends on `libcdj (>= 0.1)` and `libswscale`.
**Gate passed:** `test_export_oracle.sh` (C++ exporter from `build/src/libvdj-trakcrate.a`, `test/oracle/cxx_export.cpp`): the whole drive of a 3 track, 2 playlist library is byte identical without artwork, and with artwork identical apart from the JPEGs and the `SHA256SUMS` lines that list them; `test_rekordbox_roundtrip.sh`: the 7 real analysis pairs of libcdj's fixtures are rebuilt tag by tag, `export.pdb` of the 7 track drive, the template and the 2195 track export come back byte for byte, row counts agree with libcdj, and `verify` accepts the real 7 track drive (0 errors). `make test-asan` and the whole bash suite on a sanitizer build of the helper are clean.
Not done: user defined pendrive entries (`drives/*/playlists/`) as export sources, the options that copy files from another drive (`exportExt.pdb`, `RBFLTR.DAT`, `USBMNG.DAT`, `djprofile.nxs`), `--keep-tmp`. The bytes whose meaning is unknown are the template's or guessed, exactly as in the C++ exporter: nothing here was tried on a real player.

### Phase 6: import from rekordbox and inspection
`import-rekordbox` (on libcdj readers, provenance recorded as in `import-from-rekordbox.md`), `pdb dump|diff`, `anlz dump|diff`, `unknown-bytes`. Keep libcdj's `xdj-*` NFS tools where they are; `anlz dump` and `verify` supersede `xdj-anlz`/`xdj-track` for files, those names stay installed by libcdj.

**Status (2026-10-28): done.** Helper: `rbdrive` (database as T/P/E lines), `rbtrack` (writes one drive track's rekordbox values into a new track directory), `pdb` (tables or pages as TSV), `anlz` (tags, decoded content, `--unknown`), `settings-check`. Bash: `import-rekordbox` (folders from playlists, dedupe by SHA-256, playlists recreated, `--settings`, dry run), `pdb dump|diff`, `anlz dump|diff`, `unknown-bytes`.
**Gate passed on the test data** (`test_import_rekordbox.sh`, libcdj's real 7 track drive): 7 imported with grids, cues, waveforms and byte-identical `anlz/` copies, `check` is clean, `analyse` skips them, a second import finds 7 duplicates, an export of the imported library verifies, and `pdb diff` against the real database shows the differences. Clean under ASAN/UBSan; the 2195 track fixture database reads (no audio, so nothing imported).
Not done: copies of `RBFLTR.DAT`/`exportExt.pdb`/`djprofile.nxs`, per playlist folder choices, skipping tracks by drive path and size, keys and colours in `pdb dump`, an executable comparison of the import with the GUI importer's output (the folder and track names differ in spelling: bash `tc_sanitize_name` against the GUI's `sanitizeName`).

### Phase 7: docs, completion, release
Man pages, bash completion, example scripts (`scripts.md`), README for script writers (the format, the contract, how to add `trakcrate-<name>` in `/usr/local/lib/trakcrate/`), golden-file tests for every `--format` output, `--dry-run` writes nothing, export then `verify` round trip, `.deb`.

## Risks
- **Two implementations of the same format.** The cost is the price of the independent-tools decision: mitigated by the documents as contract, shared fixtures, and the C++ output as oracle until the C side is trusted.
- **The unknown bytes** (`UNKNOWN_BYTES.md`) in PDB/ANLZ output are validated against only two sample drives, whichever language writes them; hardware testing (`hardware-test.md`) is independent of this plan.
- **Uncommitted work in the tree** (many `src/trakcrate` and `src/ui` files): nothing here touches them, `src-cli/` is purely additive.
- **libcdj rules**: never run git there; header changes need the user to run `! cd ../libcdj && sudo make install`. Its version (`0.1`) is separate from `./version`, pin `libcdj (>= 0.1)`.
- **PSSI and 3-band waveforms** wait for the user's pendrive data; the CLI gains them after libcdj does.
