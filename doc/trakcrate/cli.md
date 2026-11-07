# trakcrate command line tools (requirements)

`trakcrate` is a set of command line helpers for people who write scripts against a trakcrate library. The library is a directory tree of plain files with a simple, documented
format (`TRAKCRATE-FORMAT.md`, the contract), so **anyone can change it with `sed`, `awk` or a text editor**. The tools do what a shell cannot do well (decode audio, analyse it,
read tags, read and write the binary rekordbox files, checksums) and add conveniences (import, ordered playlists, export, verify), with text in and text out so they compose in pipes, `find | xargs`, cron and Makefiles.

Plan and phases: `.planning/plans/trakcrate-cli_PLAN.md`. Machine readable output and exit codes: `cli-interface.md`.

## Principles

1. **The CLI is independent of the GUI.** The vdj-ui GUI (C++/Qt, `src/trakcrate/`) and these tools are two implementations of the same documented format. There is no shared code and neither
   depends on the other; the format document is the only contract. Both can be used on the same library (taking the library lock, `TRAKCRATE-FORMAT.md` section 16).
   (Earlier drafts had the GUI run these commands as child processes. That was dropped.)
2. **Unix style**: small commands, text in and text out, no interactive prompts unless `--interactive`.
3. **Headless**: no Qt, no display needed, runs over ssh.
4. **stdout is data, stderr is chatter.** Progress and warnings go to stderr, results to stdout. `--quiet`, `-v` (repeatable) control stderr.
5. **Machine readable output** on every reporting command: `--format text|tsv|json|jsonl`, with stable documented field names (`cli-interface.md`).
6. **Idempotent and safe by default**: read-only unless told otherwise; `--dry-run` / `-n` on everything that writes or deletes; `--yes` skips confirmation of destructive steps.
7. **Exit status**: 0 success, 1 runtime failure, 2 usage error, 3 partial success (some tracks failed), 4 library busy (`cli-interface.md`).
8. **Sandboxed paths**: names are single path components; `..` and symlink escapes from untrusted sources (imported drives) are rejected.
9. **Tolerant of hand edits**: unknown keys, files and columns are preserved on rewrite (`TRAKCRATE-FORMAT.md` section 1).

## Architecture: bash first, C where bash cannot cope

Like `git`: one wrapper, many `trakcrate-<command>` programs. It lives in the repository under `src-cli/` and is installed system-wide by its own package.

```
src-cli/
├── c/                                   the C helper and the C library of the format (no Qt, no C++)
│   ├── Makefile                         plain make, gcc -Wall -Werror, output in target/ (like libcdj)
│   ├── lib/                             format library: key/value and table files, atomic write, lock, safe paths, natural sort, sha256
│   ├── helper/                          main.c and one file per subcommand
│   └── test/                            snip tests (.c.snip) and fixtures
├── script/                              the root of the installed system: a package is a copy of this tree
│   ├── usr/bin/trakcrate                the wrapper (bash)
│   ├── usr/lib/trakcrate/trakcrate-*    the commands (bash); the build adds trakcrate-helper (C) here
│   ├── usr/share/man/man1/              trakcrate.1 and one page per command (@VERSION@ filled at build)
│   ├── usr/share/bash-completion/completions/trakcrate
│   ├── usr/share/doc/trakcrate-cli/     README, TRAKCRATE-FORMAT.md, the example scripts
│   └── etc/trakcrate/trakcrate.conf     conffile
├── test/                                bash tests: the wrapper, the commands on fixture libraries
└── deploy/                              DEBIAN/control, build-deb.sh
```

Installed:

```
/usr/bin/trakcrate                       the wrapper
/usr/lib/trakcrate/trakcrate-<command>   bash commands
/usr/lib/trakcrate/trakcrate-helper      the C helper (not on $PATH: an implementation detail with a changing interface)
/usr/local/lib/trakcrate/                commands added by the administrator (any language)
/etc/trakcrate/trakcrate.conf            system configuration
```

- **The wrapper**: `trakcrate <command> [options...]`, a single bash script. It handles `--help`, `--version`, `--list-commands`, `help <command>`, finds `trakcrate-<command>` in the search
  path and `exec`s it (stdin, stdout, stderr and exit status pass straight through). An unknown command prints "trakcrate: 'foo' is not a trakcrate command" with close matches and exits 2.
  Small and auditable: `set -eu`, quoted expansions, `shellcheck` clean, the project's bash conventions (the `c-bash` skill).
- **Version**: `./version` (`VERSION=x.y.z`) is the only source. The wrapper reports it, the man pages are generated from it, `deploy/build-deb.sh` uses it. libcdj has its own version file.
- **A command** is an executable `trakcrate-<command>` (`<command>` is `[a-z0-9][a-z0-9-]*`), preferably bash, any language with a shebang is allowed. Commands call each other through
  `"$TRAKCRATE_BIN" <command> ...`.
- **bash or C**: listing, metadata get/set, playlist symlinks (`000_` prefixes), folder import, copying, rsync wrappers, option parsing and reports are bash. These go to the C helper, which a
  command calls like any other tool: audio decoding, tags and artwork from mp3 and wav, beat grid, waveform and loudness analysis, reading and **writing** `export.pdb`, ANLZ and the settings files
  (CRC), rendering cuemarks, image scaling, the staged export and checksums. When a bash command stops being readable, move that part into the helper.
- **The C helper**: `trakcrate-helper <subcommand> ...` (`analyse`, `probe`, `export`, `pdb`, `anlz`, ...). Built on the C format library, libcdj (`<cdj/anlz.h>`, `<cdj/pdb.h>`: the readers
  and parsers) and ffmpeg's C libraries. Prints TSV/JSON on stdout, errors on stderr, same exit statuses.
- **Search order**, first match wins: `$TRAKCRATE_PATH` (colon separated, development and tests only), `/usr/local/lib/trakcrate/` (`$TRAKCRATE_LOCAL_DIR` replaces it, tests only), then the package's own directory, found relative to the wrapper (`/usr/bin/trakcrate` gives `/usr/lib/trakcrate`; the same arithmetic works in the source tree). The wrapper does **not** search `$PATH` or
  anything under `$HOME`. `--list-commands` prints `name<TAB>directory` per command, with a third column `shadows <directory>` when it hides another one (the completion uses the first column). `trakcrate-help` and `trakcrate-helper` are not commands.
- **User commands**: `sudo install -m 755 trakcrate-foo /usr/local/lib/trakcrate/` makes `trakcrate foo` available to everyone. The packages install nothing in any home directory.
- **Extension contract**: the wrapper exports `TRAKCRATE_BIN` (the wrapper), `TRAKCRATE_LIBEXEC` (`/usr/lib/trakcrate`), `TRAKCRATE_DATA` (`/usr/share/trakcrate`: the template database), `TRAKCRATE_HELPER`, `TRAKCRATE_LIBRARY` (the resolved, absolute library: `--library`, then `$TRAKCRATE_LIBRARY`, `$TRAKCRATE_HOME`, the configuration `library=`, `~/trakcrate`), `TRAKCRATE_FORMAT`, `TRAKCRATE_COLOR`, `TRAKCRATE_JOBS`, `TRAKCRATE_VERBOSE` (count), `TRAKCRATE_QUIET`, `TRAKCRATE_DRY_RUN`, `TRAKCRATE_YES` (`1` when set), `TRAKCRATE_CONFIG`, `TRAKCRATE_API=1`. Global options are parsed by the wrapper and handed on as these variables; they must come before the command, and a command that is run through `$TRAKCRATE_BIN` inherits them. A command prints its usage for `--help` and one summary line for `--summary`.
- **Configuration**: `/etc/trakcrate/trakcrate.conf` (system defaults), overridden per key by an optional `${XDG_CONFIG_HOME:-~/.config}/trakcrate/config` the user creates (or the file given with `--config`); `key=value` lines, `#` comments, a leading `~/` is expanded, never evaluated; keys `library`, `format`, `jobs`, `color`. Precedence: command line, environment, user file, system file, default.
- **Data**: the library is the user's data and lives in their home (`~/trakcrate`) or anywhere; installing never creates it. `trakcrate init` does.
- **Not to be confused with `scripts.md`**: *commands* (`trakcrate-<name>`) extend `trakcrate` system-wide; *scripts* are `track-script-*` / `crate-script-*` files inside one library, run by hand
  from the GUI or `trakcrate scripts run`. A script may call `trakcrate` commands.

### Packaging

- Package `trakcrate-cli`, a `.deb` (`trakcrate-cli_<version>_amd64.deb`), built by `src-cli/deploy/build-deb.sh` (staging tree from `src-cli/script/` plus the built helper, then `dpkg-deb --build`).
- Installs the files above plus `/usr/share/man/man1/trakcrate*.1.gz`, the completion, `/usr/share/doc/trakcrate-cli/`, and an empty `/usr/local/lib/trakcrate/` (Debian policy: created and left, never removed on upgrade).
- `Depends:` coreutils, findutils, rsync, file, **libcdj (>= 0.1)** (shared library), ffmpeg's `libavformat`, `libavcodec`, `libswresample` (and `libswscale` if used for artwork), others as the C code needs (`${shlibs:Depends}`).
- No dependency on Qt or on the vdj-ui package, and the vdj-ui package does not depend on this one.

### Testing

- The wrapper with fake commands in a temp dir via `$TRAKCRATE_PATH`: search order, shadowing, exit status passthrough, unknown command, arguments with spaces and leading dashes, `--help`, `--list-commands`.
- Each bash command through the wrapper against a throw-away library built from fixtures; `shellcheck` (not installed on the development machine yet: `bash -n` and the tests are what runs today). The bash tests use a small plain-bash harness (`src-cli/test/lib.sh`, `run.sh`), no `bats`.
- The C library and helper with snip unit tests and the libcdj-style `make test` / `make test-asan`; golden files for every `--format` output; a `--dry-run` test proving nothing is written.
- Against the GUI implementation: while both exist, the C++ `analyseTrack` and exporter are the oracle (same analysis results, byte-identical export apart from JPEG encoding); the format document decides when they differ.
- Packaging: build the `.deb`, install in a clean container, run `trakcrate --version` and `trakcrate init`, remove, check nothing is left in any home directory.

## Invocation

```
trakcrate [global options] <command> [command options] [args]
```

Global options: `--library DIR` (default `$TRAKCRATE_HOME`, then `~/trakcrate`), `--config FILE`, `--jobs N` (default: number of CPUs), `--format`, `-q`, `-v`, `-n`, `--yes`, `--version`, `--help`, `--color auto|always|never`.

### Library and import

| Command                         | Purpose                                                                                 |
|---------------------------------|-----------------------------------------------------------------------------------------|
| `init [DIR]`                    | create an empty library (`track-master-copy/`, `playlists/`, `user-settings/`)          |
| `add FILE\|DIR...`              | import .mp3/.wav/.flac into `track-master-copy/<folder>/<track>/`; tags and artwork extracted |
| `import DIR`                    | import a folder of bare audio: master storage **and** an auto playlist (below)          |
| `import-rekordbox PENDRIVE`     | import a real rekordbox export, recording provenance (`import-from-rekordbox.md`)       |
| `ls [PATTERN]`                  | list tracks (`--long`, `--format`, `-0`; `--sort FIELD` is planned: filter and sort with `awk`/`sort` on `--format tsv` meanwhile) |
| `info TRACK`                    | all metadata, provenance and analysis summary of one track                              |
| `set TRACK FIELD=VALUE...`      | edit metadata (sets `origin.meta: edited`)                                              |
| `rm TRACK...`                   | remove from the library: to `.trash/<time>/` (`--purge` deletes), links removed from every playlist; needs `--yes` when the track is in playlists |
| `check`                         | verify the library against `TRAKCRATE-FORMAT.md` section 15: missing audio, bad files   |

`import --analyse` imports and then runs `trakcrate analyse` on the new tracks.

`add` options (examples): `--recursive`, `--copy | --move | --link`, `--folder NAME`, `--no-artwork`, `--analyse`, `--skip-existing`, `--follow-symlinks`, `--include '*.wav'`, `--exclude PATTERN`,
`--files-from FILE|-` (NUL separated with `-0`), `--tags-from-filename 'Artist - Title'`.
Not implemented yet: `add` itself (single files), `--follow-symlinks`, `--include/--exclude`, `--tags-from-filename`.

### Importing a folder of bare audio files

```
trakcrate import [options] DIR
```

1. **Master storage**: every audio file in `DIR` becomes `track-master-copy/<folder>/<track>/` (audio, `meta.txt` from the tags, `artwork.jpg`, `source.txt`). `<folder>` is the directory name (`--folder NAME`).
2. **A playlist**: `playlists/<NAME>/symlinks/` filled with relative symlinks prefixed `000_`, `001_`, ... (`TRAKCRATE-FORMAT.md` section 2.3). `NAME` is `--playlist NAME`, default the base name of DIR; `--no-playlist` skips it.
   - Initial order is **alphabetical**, case-insensitive natural sort, locale independent. `--order name|mtime|title|bpm|none`, `--reverse`.
   - Prefix width from the track count (at least 3 digits).
   - Playlist exists: fail unless `--append` (new tracks at the end) or `--replace` (asks unless `--yes`).

Details: `--recursive` includes subfolders (each its own master folder named `<folder> - <sub path>`; one playlist unless `--playlist-per-folder`). `.mp3`, `.wav` and `.flac` only; others are skipped and counted.
`--copy` (default), `--move`, `--link`; the source is never changed unless `--move`. A file whose `audio_sha256` is already in the library is not copied again but is added to the playlist (`--no-dedupe` to disable).
Same name, different file: numeric suffix. Unreadable files are reported and skipped (exit 3). `--analyse` runs analysis afterwards; `--dry-run` prints the plan. Running it twice changes nothing.

```
trakcrate import ~/Music/Acid\ Loops
trakcrate import --playlist friday --recursive --analyse -j 8 ~/Music/new
```

### Analysis

| Command              | Purpose                                                                  | Notes                                                                    |
|----------------------|--------------------------------------------------------------------------|--------------------------------------------------------------------------|
| `analyse TRACK...`   | beat grid, waveforms, loudness in one pass over the decoded audio        | `--only beatgrid,waveform`, `--force`; groups with origin `rekordbox`/`edited` are skipped unless `--force` |
| `beatgrid TRACK...`  | BPM and phase (`beat-grid.md`)                                           | `--bpm-min/--bpm-max`, `--bpm N`, `--first-beat MS`, `--from-tag`, `--print` |
| `waveform TRACK...`  | waveform data and PNG previews (`waveforms.md`)                          | `--png FILE`, `--width`, `--height`; heights are absolute, never normalised per track |
| `loudness TRACK...`  | peak / RMS report                                                        | text/tsv/json                                                            |
| `cues TRACK`         | show or edit cuemarks, hot cues (`cuemarks.md`)                          | `--add`, `--add-ms`, `--rm`, `--hot A=17`, `--check`                     |
| `vocals TRACK...`    | vocal regions (`vocals.md`), later                                       |                                                                          |
| `key TRACK...`       | musical key, later                                                       |                                                                          |

Common options: `--jobs`, `--timeout SEC`, `--continue-on-error` (default) / `--fail-fast`, `--progress`, `--progress-fd N`. Analysis is deterministic: same input and options, same bytes. Not implemented yet: `waveform --png`, `beatgrid --bpm N` / `--first-beat MS`, `--report FILE`, `--cache`, `key`, `vocals`. The detector is the GUI's own (no aubio): the same audio gives the same grid on every machine, and the C++ and C analysers agree byte for byte on the test tracks (`src-cli/test/test_oracle.sh`).

### Export and pendrives

| Command                      | Purpose                                                                                      |
|------------------------------|----------------------------------------------------------------------------------------------|
| `pendrive new NAME`          | create a playlist `playlists/NAME/`                                                          |
| `pendrive add NAME TRACK...` | add tracks (`--at N` position)                                                               |
| `pendrive ls NAME`           | contents and order                                                                           |
| `export NAME... DEST`        | build a rekordbox-format drive (`PIONEER/` + `Contents/`): database, analysis files, artwork and settings are built in a staging folder on tmpfs and checked with libcdj's readers, then every file is written once to `DEST` (a folder that may hold other files, but not `Contents`, `PIONEER`, `SHA256SUMS` or `trakcrate-manifest.txt`) (audio first, `export.pdb` last), then read back and compared by SHA-256 (`rekordbox-db-output.md`) |
| `sync NAME... TARGET`        | update an exported drive with rsync semantics: audio by size and time, metadata by content, `export.pdb` last; `--delete-orphans` lists what goes (inside `Contents/` and `PIONEER/` only) and asks unless `--yes`; `-n` itemizes and changes nothing; the old `SHA256SUMS` is removed (sync does not read the audio) |
| `add-to-drive PLAYLIST TARGET` | add a playlist to the database of a rekordbox pendrive that already has one, without rebuilding it: tracks it holds are referenced, new audio, analysis and artwork are added, `export.pdb` is replaced last by two renames and the old one is kept as `export.pdb.bak` and in `/var/tmp`; refuses a playlist name it already has and `exportLibrary.db` sticks (see "Add to rekordbox") |
| `verify TARGET`              | check a drive with libcdj's readers: `export.pdb` consistent, every track's audio, both analysis files (parse, name the right audio) and artwork, settings CRCs, `SHA256SUMS`; works on a real rekordbox drive too |

The CLI does not know the GUI's settings sets (`user-settings/<set>/`, `TRAKCRATE-FORMAT.md` section 10) yet: it writes its compiled in support files, or those given with `--settings-from`.

`export` options: `--tmpdir DIR`, `--no-verify`, `--exclude-art`, `--settings-from DIR` (DEVSETTING.DAT and MYSETTING.DAT of a real drive), `--raise-limits` (the track, artist and album limits of newer decks: 20000/16000/16000 instead of 10000 each; above the limit in force the export is refused, see `rekordbox-db-output.md`), `--allow-unanalysed`, `--template FILE`, `--no-cuemarks`, `--cuemark-width N`, `--cuemark-green`, `--cuemark-hot`, `-n`, `--progress-fd N`, and for compatibility testing `--no-settings`, `--no-pvbr`, `--no-pwv4`, `--no-pwv5`, `--no-manifest` (the manifest records the variant). `SHA256SUMS` and `trakcrate-manifest.txt` are written in the root of the drive. The template database (`/usr/share/trakcrate/template-export.pdb`, found through `$TRAKCRATE_DATA`) supplies the bytes whose meaning is not known (`UNKNOWN_BYTES.md`); artwork is one 80x80 `a<id>.jpg` per distinct cover, as a real rekordbox drive has. The C export is byte identical to the GUI's C++ exporter, artwork JPEGs aside (`test/test_export_oracle.sh`), rebuilds all 7 real analysis files of libcdj's fixtures tag by tag, and reproduces real `export.pdb` files byte for byte (`test/test_rekordbox_roundtrip.sh`). Not implemented yet: the user defined pendrive entries (`drives/*/playlists/`) as export sources, the optional files copied from another drive (`exportExt.pdb`, `RBFLTR.DAT`, ...), `--keep-tmp`.

### Importing a real rekordbox drive

`import-rekordbox DRIVE` reads a rekordbox export (read only) and fills the library: `meta.txt` from the database (title, artist, album, genre, label, key, rating, colour, comment, dates ...), `beatgrid.txt` (first tempo and first beat to the microsecond), `cuemarks.txt` (memory points, hot cues, loops as `loop_end_ms=`), `waveform.bin` (PWAV, PWV2, PWV3, PWV5), `artwork.jpg`, and the original ANLZ files byte for byte in `anlz/`. Those groups are marked `rekordbox` in `source.txt`, so `analyse` leaves them alone. Every playlist becomes a master folder (a track in several is stored once, in the first by natural name order) and a playlist in `playlists/`; audio already in the library (same SHA-256) is not stored again. Options: `--folder NAME`, `--by-path`, `--no-playlist-folders`, `--no-playlists`, `--link`, `--no-dedupe`, `--no-beatgrid|cues|waveform|artwork|anlz`, `--settings` (keep DEVSETTING.DAT and MYSETTING.DAT, CRC checked, in `user-settings/`), `-n`. Tracks whose audio is not on the drive are skipped and listed. Not done yet: `RBFLTR.DAT`, `exportExt.pdb`, `djprofile.nxs` copies, choosing which playlists become folders (`folderOff`), re-import by drive path and size (the hash is read every time). Tested on the 7 real tracks of libcdj's fixtures (`test/test_import_rekordbox.sh`); the 2195 track fixture imports nothing (no audio) but reads.

### Scripts and inspection

| Command                | Purpose                                                                                          |
|------------------------|--------------------------------------------------------------------------------------------------|
| `scripts ls\|run\|new\|review\|undo` | the library scripts of `scripts.md`                                                |
| `pdb dump FILE`        | tables as TSV (`--table tracks`, also artists albums genres labels artwork playlists entries) or the page list (`--pages`); on libcdj's `<cdj/pdb.h>` |
| `pdb diff A B`         | compare two PDBs structurally                                                                    |
| `anlz dump FILE`       | tags and decoded content (`--tag PQTZ`, `--raw` hex, `--beats`); on libcdj's `<cdj/anlz.h>`      |
| `anlz diff A B`        | compare two ANLZ files                                                                           |
| `unknown-bytes [TRACK...]` | the values rekordbox wrote in the words we have to invent (PQTZ/PQT2 header words, PVBR, cue markers), read from the `anlz/` copies of imported tracks |

libcdj's own tools stay where they are: `xdj-pdb`, `xdj-explore`, `xdj-tracks` read a **live deck over NFS** (they need an IP), `xdj-anlz` and `xdj-track` read files. `anlz dump` and `verify` supersede the
last two for files; the old names remain installed by libcdj.

## Scripting examples (target behaviour)

```
# import and analyse a folder, 8 jobs, JSON lines report
trakcrate add --recursive --analyse --jobs 8 --format jsonl ~/Music/new > report.jsonl

# tracks that are not normalised (peak below -3 dBFS)
trakcrate loudness --format tsv | awk -F'\t' '$3 < -3 { print $1 }'

# tracks without a beat grid, analyse them
trakcrate ls --format paths | while read -r t; do [ -e "$t/beatgrid.txt" ] || echo "$t"; done | xargs trakcrate beatgrid

# two identical gig drives
trakcrate export gig-2026-11 /media/usb1 && trakcrate export gig-2026-11 /media/usb2

# update a drive, show what would be removed first
trakcrate sync gig-2026-11 /media/usb1 --delete-orphans -n
```

No script needs the CLI: `sed -i 's/^rating:.*/rating: 5/' "$TRACK/meta.txt"` is a legitimate edit. The CLI adds the parts that are hard or risky by hand (atomic writes, the lock, provenance `origin: edited`, validation).

## Implementation notes

- **Format library first** (`src-cli/c/lib`): every command and the helper use it, so the reading and writing rules (atomic write, preserve unknown keys, lock, safe names, natural sort) exist once.
- **Decoding**: ffmpeg's C API (libavformat/libavcodec/libswresample) to 44.1 kHz stereo float, once per track for all analysers.
- **Parallelism**: a thread pool bounded by `--jobs`; output order stable (sorted by path) unless `--unordered`.
- **Atomic writes**: temp file in the same directory + rename. **Lock**: `.trakcrate-lock` for multi-file writes; read-only commands never lock.
- **General code goes to libcdj eventually**, but not as part of this work: candidates are logged in `ai-output/refactor-recommendations.md`.
- **Docs**: man pages, `--help` for every command, bash completion that asks the wrapper for the command list.
- **Naming**: check that `trakcrate` and its commands do not clash with anything on the usual `$PATH`.

## Open questions

- `--where` query language: deferred; filter with `awk`/`jq` over `--format` output first.
- Whether `add --analyse` should be the default.

## Add to rekordbox

`trakcrate add-to-drive [options] PLAYLIST TARGET` adds a playlist of the library to the pendrive `TARGET`, which already has `PIONEER/rekordbox/export.pdb` (made by rekordbox or by `trakcrate export`), without rebuilding its database. It is the C port of the GUI's "Add to rekordbox" (`rekordbox-db-output.md`, "Adding a playlist to a pendrive that already has a database"; the rules and what is still a guess are in `pdb-append-notes.md`).

- Tracks the pendrive already holds (same `/Contents/...` path, size and first and last megabyte) are referenced: no new database row, no copy, no analysis file. New tracks get audio, an analysis folder by rekordbox's hash, artwork and rows; ids continue after the highest id of the database, artists, albums, genres, labels and artwork are reused by name.
- Nothing that exists is changed except the database, and that last: `export.pdb.new` is written next to the old one, then `export.pdb` is renamed to `export.pdb.bak` (older copies rotate to `.bak.2` and `.bak.3`) and `export.pdb.new` to `export.pdb`. A pendrive pulled between the two renames has `export.pdb.bak` and `export.pdb.new` and no `export.pdb`: rename `export.pdb.new` to `export.pdb`. Settings files, `USBMNG.DAT`, `exportExt.pdb`, `RBFLTR.DAT`, `SHA256SUMS` and the manifest of an earlier `trakcrate export` are not touched (so that `SHA256SUMS` no longer matches `export.pdb`; `trakcrate verify` reports it), and neither is the library.
- Before anything is written the old database is copied to `/var/tmp/rekordbox-edit-backup-export.<UTC date and time>.pdb` (`--backup-dir DIR`), checked against the original, and never deleted.
- Refused with nothing changed: no `export.pdb`, `exportLibrary.db` present (the newer format), a database that fails the checks, a playlist name the pendrive already has (exact or ignoring case), tracks not analysed (unless `--allow-unanalysed`), more tracks, artists or albums than the decks take (`--raise-limits`), not enough space on the pendrive or in the staging area.
- Ctrl-C (SIGINT or SIGTERM) cancels until the switch and removes what was added; an error before the switch does the same. A stopped run reports the files that stayed.
- Options as for `export`: `--tmpdir`, `--exclude-art`, `--template`, `--raise-limits`, `--allow-unanalysed`, `--no-cuemarks`, `--cuemark-width`, `--cuemark-green`, `--cuemark-hot`, `--no-pvbr|pwv4|pwv5`, `-n` (check everything, write nothing, no backup copy), `--progress-fd N`. The command never asks a question, so `--yes` has no effect. Output (`--format tsv|json|jsonl|paths`): target, playlist, new, existing, files, bytes, backup, status.

The C code is `lib/rbadd.c` (the engine, the order of `addToDrive` in `exporter.cpp`), `lib/rbappend.c` (check and edit the database, the port of `pdb_append.cpp`) and the pieces of `lib/rbexport.c` it shares (`lib/rbexport_internal.h`). Tests: `test_add.sh` (adds, reuse, refusals, dry run, a stick pulled at each step, a failed copy), `test_append_oracle.sh` (the C editor against the C++ one on the committed fixtures, libcdj's 2195 track database and, when mounted, the real sticks: byte identical databases, also plan after plan) and `test_add_oracle.sh` (whole sticks after an add, C helper against `addToDrive`: byte identical apart from the JPEGs). The hidden `--stop-at STEP` option of the helper stops the engine at a step (`backup`, `planned`, `audio`, `artwork`, `analysis`, `database`, `switch`, `between-renames`, `done`) as if the stick had been pulled; the tests use it.

The ANLZ folder names the C helper writes (`USBANLZ/P<partition>/<slot>/ANLZ<counter>.DAT`) now follow rekordbox's hash like the GUI exporter (they used to be derived from a SHA-256, which the decks could not find); `src-cli/test/test_export_oracle.sh` checks that both exporters give byte identical drives.
