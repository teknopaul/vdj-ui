# trakcrate command line tools (requirements)

Everything the GUI does with a library must also be possible from a shell, so Linux users can script imports, analysis and exports (cron, `find | xargs`, Makefiles, CI) without Qt or a display. The GUI and the CLI share one library; neither contains logic the other lacks.

## Principles

1. **Unix style**: small tools or subcommands, text in and text out, composable with pipes, no interactive prompts unless `--interactive`.
2. **No GUI dependency**: the CLI binaries must not link Qt Widgets and must run headless over ssh. Commands are bash scripts; heavy work is in a C helper (no Qt at all; see Architecture).
3. **Exit status**: 0 success, 1 runtime failure, 2 usage error, 3 partial success (some tracks failed; details on stderr / in the report). Documented in each man page.
4. **stdout is data, stderr is chatter**: progress and warnings go to stderr, results to stdout. `--quiet`, `-v` (repeatable) control stderr.
5. **Machine readable output** on every reporting command: `--format text|tsv|json` (`json` one object per line with `--format jsonl`). Stable field names, documented.
6. **Idempotent and safe by default**: read-only unless told otherwise, `--dry-run` / `-n` on everything that writes or deletes, `--yes` to skip confirmation of destructive steps (orphan removal needs a warning, see the main doc).
7. **Sandboxed paths**: reject `..` and symlink escapes from untrusted sources (imported pendrives).
8. **Reuse existing tools**: libcdj already ships `xdj-pdb`, `xdj-anlz`, `xdj-track`, `xdj-tracks` and `xdj-explore` (read-only inspection of exports). The new tools should wrap or replace these, not duplicate them; keep their names working or document the migration.

## Architecture (decided)

Like `git`, but **bash first**. It lives in the vdj-ui repository and is installed system-wide by its own package.

```
/usr/bin/trakcrate                       the one wrapper script (bash)
/usr/lib/trakcrate/                      commands shipped by the package
    trakcrate-import                     a bash script  (trakcrate import ...)
    trakcrate-ls                         a bash script
    trakcrate-export                     a bash script that calls the helper for the heavy part
    ...
    trakcrate-helper                     the compiled C program for work too hard for bash
/usr/local/lib/trakcrate/                commands added by the administrator (any language)
/etc/trakcrate/                          system configuration
```

- **One wrapper**: `trakcrate <command> [options...]` is a single bash script. It handles `--help`, `--version`, `--list-commands`, `help <command>`, finds `trakcrate-<command>` in the directories below, and `exec`s it with the remaining arguments (stdin, stdout, stderr and the exit status pass straight through). Unknown command: "trakcrate: 'foo' is not a trakcrate command" with close matches, exit status 2 (as git). Small and auditable; `set -eu`, quoted expansions, `shellcheck` clean, written to the project's bash conventions (the `c-bash` skill).
- **A command is a script**: every sub command is an executable file named `trakcrate-<command>` (`<command>` is `[a-z0-9][a-z0-9-]*`), preferably **bash**; any other language is allowed (Python, Perl, ...) as long as it has a shebang. Commands are small and single purpose and call each other through the wrapper (`"$TRAKCRATE_BIN" <command> ...`) so they compose.
- **Prefer bash, use C where bash cannot cope**: logic that is simple file and text manipulation stays in bash: listing, metadata get/set, the playlist symlinks (`000_` prefixes), folder import and copying, rsync wrappers, option parsing, reports. Logic that needs binary formats, numerics or speed goes to the C helper, called from the bash command like any other tool:
  - decoding audio and reading tags/artwork from mp3 and wav
  - beat grid, waveform, loudness and vocal analysis
  - reading and **writing** `export.pdb`, ANLZ files, the settings files with their CRC
  - rendering the cuemarks into the waveforms, image conversion for artwork
  - the staged export: build the tree, verify it, produce the checksums
- **The C helper**: one program, `/usr/lib/trakcrate/trakcrate-helper`, with its own subcommands (`trakcrate-helper analyse-beats FILE`, `trakcrate-helper pdb-write ...`). It is **not on `$PATH`** (it is an implementation detail with a changing interface; users call `trakcrate ...`), reads and writes the same plain files as the bash commands, prints results as TSV/JSON on stdout and errors on stderr, and uses the same exit status convention. It is built on `libtrakcrate` (C, no Qt) which only the CLI tools use; the vdj-ui GUI does not link it (the GUI runs the commands instead, `gui.md`). When a bash command grows beyond what is readable, the right move is to move that part into the helper, not to write clever bash.
- **Command search order**, first match wins:
  1. `$TRAKCRATE_PATH` (colon separated; development and tests only, ignored when empty)
  2. `/usr/local/lib/trakcrate/` (commands added by the administrator, so they can override or extend)
  3. `/usr/lib/trakcrate/` (commands from the package)
  The wrapper does **not** search `$PATH` or anything under `$HOME`. `trakcrate --list-commands` shows each command with its directory, and flags a command in (2) that shadows one in (3).
- **User supplied commands**: put an executable `trakcrate-<name>` file in `/usr/local/lib/trakcrate/` (the user is assumed to have root for this; `sudo install -m 755 trakcrate-foo /usr/local/lib/trakcrate/`). It is then available to everyone on the machine as `trakcrate foo`. Nothing is installed in any user's home directory by the packages.
- **Extension contract**: the wrapper exports `TRAKCRATE_BIN` (the wrapper itself), `TRAKCRATE_LIBEXEC` (`/usr/lib/trakcrate`), `TRAKCRATE_HELPER` (the helper), `TRAKCRATE_LIBRARY` (resolved library base, from `--library`, `$TRAKCRATE_HOME`, or the default `~/trakcrate`), `TRAKCRATE_FORMAT`, `TRAKCRATE_API=1`. A command should print its usage for `--help` on stdout and one summary line for `--summary` (used by `help` and `--list-commands`).
- **Where user data lives**: the library is the user's data and so lives in their home by default (`~/trakcrate`) or any path they choose; installing the tools never creates it. Configuration: `/etc/trakcrate/trakcrate.conf` (system defaults), overridden by an optional `$XDG_CONFIG_HOME/trakcrate/` file the user creates themselves.
- **Not to be confused with `scripts.md`**: *commands* (`trakcrate-<name>`) are installed system-wide, extend the `trakcrate` command and are run from a terminal or other scripts; *scripts* are `track-script-*` and `crate-script-*` files in a library's `scripts/` directory, run by hand from the vdj-ui GUI (or `trakcrate scripts run`) on the loaded track or the whole library. A script may call `trakcrate` commands.

### Packaging

- A **separate package**, independent of the GUI: `trakcrate-cli`, built as a `.deb` (version in the file name, e.g. `trakcrate-cli_0.0.1_amd64.deb`; the file name follows the Debian pattern, `trakcrate-cli-0.0.1.deb` is shorthand for it). It does not depend on Qt or the GUI, so a headless machine or a server can install it alone.
- Installs: `/usr/bin/trakcrate` (the wrapper), `/usr/lib/trakcrate/trakcrate-*` (bash commands) and `trakcrate-helper` (C), `/usr/share/man/man1/trakcrate*.1.gz`, `/usr/share/bash-completion/completions/trakcrate` (asks the wrapper for the command list), `/etc/trakcrate/trakcrate.conf` (a conffile, so local changes survive upgrades), `/usr/share/doc/trakcrate-cli/` (including the example scripts from `scripts.md`), and an empty `/usr/local/lib/trakcrate/` created on install (Debian policy: the package may create and leave it, the administrator owns the content; it is not removed on upgrade).
- Dependencies: the shell tools the commands use (`coreutils`, `findutils`, `rsync`, `file`), and the shared libraries of the helper (ffmpeg's `libavformat`, `libavcodec`, `libswresample`, plus whatever the image/tag code needs), declared with `${shlibs:Depends}` or by hand. `Recommends: gimp | krita`-style packages are not relevant here (that is the GUI).
- The **vdj-ui GUI package** (`make-deb.sh`, cpack) is separate and has `Depends: trakcrate-cli (>= same version)`: the GUI runs the installed commands as child processes and links no trakcrate library (decided, `gui.md`). The CLI never depends on the GUI. Version numbers of the two move together.
- Build: a CMake target (or a plain Makefile) in a top-level `trakcrate/` directory of the repository that does not need Qt; `trakcrate/make-deb.sh` builds only `trakcrate-cli` (mirror of the existing `make-deb.sh`, using cpack's DEB generator or `dpkg-deb --build` on a staging tree).

### Layout in the repository

```
trakcrate/
├── bin/trakcrate                the wrapper
├── libexec/trakcrate-*          bash commands
├── src/                         libtrakcrate and trakcrate-helper (C)
├── share/{man,completions,doc}/
├── etc/trakcrate.conf
├── tests/                       wrapper tests, command tests, helper tests
└── make-deb.sh
```

### Testing

- The wrapper with fake commands in a temporary directory (via `$TRAKCRATE_PATH`): search order, shadowing, exit status passthrough, unknown command, arguments with spaces and leading dashes, `--help` and `--list-commands`.
- Each bash command through the wrapper against a throw-away library built from fixtures; `shellcheck` in CI.
- The helper with unit tests in C (the snip framework used elsewhere in the project) and golden-file comparisons with rekordbox data (`beat-grid.md`, `waveforms.md`).
- A packaging test: build the `.deb`, install into a clean container, run `trakcrate --version` and `trakcrate init`, remove it, check nothing remains in any home directory.

## Tool layout

Commands are `trakcrate-<command>` executables dispatched by the front end above (the list below is the command set, not one binary). The invocation:

```
trakcrate [global options] <command> [command options] [args]
```

Global options: `--library DIR` (the `trakcrate/` base directory; default `$TRAKCRATE_HOME`, then `~/trakcrate`), `--config FILE`, `--jobs N` (default: number of CPUs), `--format`, `-q`, `-v`, `-n`, `--yes`, `--version`, `--help`, `--color auto|always|never`.

### Library and import

| Command                         | Purpose                                                                               |
|---------------------------------|---------------------------------------------------------------------------------------|
| `init [DIR]`                    | create an empty library layout                                                        |
| `add FILE|DIR...`               | import .mp3/.wav into `track-master-copy/<folder>/<track>/`; extracts tags and artwork |
| `import DIR`                    | import a folder of bare audio files: master storage **and** an auto playlist (below)  |
| `import-rekordbox PENDRIVE`     | import a real rekordbox export (see `import-from-rekordbox.md`); records provenance   |
| `ls [PATTERN]`                  | list tracks (`--long`, `--sort FIELD`, `--where 'bpm>120 and key=8A'`)                |
| `info TRACK`                    | all metadata, provenance and analysis summary for one track                           |
| `set TRACK FIELD=VALUE...`      | edit metadata (the files are plain text, editing by hand also works)                  |
| `rm TRACK...`                   | remove from the library (`--keep-audio`)                                              |
| `check`                         | verify the library: missing audio, bad metadata, files that no longer parse           |

Options for `add` (examples): `--recursive`, `--copy | --move | --link` (how to take the audio), `--folder NAME`, `--no-artwork`, `--analyse` (run analysis straight after), `--skip-existing`, `--follow-symlinks`, `--include '*.wav'`, `--exclude PATTERN`, `--files-from FILE|-` (NUL separated with `-0`), `--tags-from-filename 'Artist - Title'`.


### Importing a folder of bare audio files

The common first step for a new user: a directory of loose .mp3/.wav files with no database.

```
trakcrate import [options] DIR
```

One command creates two things:

1. **Master storage**: every audio file in `DIR` becomes `track-master-copy/<folder>/<track>/` (audio, `meta.txt` from tags, extracted `artwork.jpg`, `source.txt` with origin `generated`). `<folder>` defaults to the directory name (`--folder NAME` overrides).
2. **A playlist**: `pendrives/<NAME>/symlinks/` filled with symlinks to the new tracks, prefixed `000_`, `001_`, ... so the order is carried by the file names (see `trakcrate-db-format.md`; if playlists later get their own place in the layout, only the location changes, not the command).
   - `NAME` is `--playlist NAME`, defaulting to the **base name of DIR** (`~/Music/Gig 2026` gives playlist `Gig 2026`). `--no-playlist` skips it.
   - Initial order is **alphabetical** by file name (case-insensitive, natural sort so `track2` comes before `track10`, locale independent so the order is reproducible), because bare files have no better ordering. Options: `--order name|mtime|title|bpm|none`, `--reverse`. Re-ordering later is renaming the symlink prefixes (`trakcrate pendrive add --at`).
   - Prefix width is chosen from the track count (at least 3 digits) so sorting stays correct for any size.
   - If the playlist already exists: fail unless `--append` (new tracks go at the end, existing order kept) or `--replace` (asks unless `--yes`).

Details:

- Recursion: `--recursive` includes subfolders; without it only `DIR` itself. In a recursive import each subfolder becomes its own `<folder>` in master storage, and the one playlist lists all files (sorted by relative path) unless `--playlist-per-folder` creates one per subfolder.
- Supported files: `.mp3`, `.wav` (case-insensitive); anything else is skipped and counted in the report (`--include`/`--exclude` refine this).
- Audio handling: `--copy` (default), `--move`, `--link`. Never modifies the source files unless `--move`.
- Duplicates: a file whose content hash is already in the master is not copied again; it is added to the playlist pointing at the existing track (`--no-dedupe` to disable).
- Name collisions: two different files with the same `<track-name>` get a numeric suffix; the original file name is kept in `meta.txt`.
- Unreadable or undecodable files: reported, skipped, exit status 3, the rest are imported.
- `--analyse` runs beat grid and waveform analysis straight afterwards; `--dry-run` prints the tracks, the folder and the playlist order without writing anything.
- Idempotent: running the same import twice changes nothing (with `--skip-existing`, the default for `import`).
- Result on stdout (`--format` as usual): track count, new/skipped/duplicate, playlist name and path.

Example:

```
trakcrate import ~/Music/Acid\ Loops
# master: track-master-copy/Acid Loops/ (12 tracks), playlist: pendrives/Acid Loops/ (alphabetical)
trakcrate import --playlist friday --recursive --analyse -j 8 ~/Music/new
```

### Analysis

Each analyser is a subcommand and is also run by `analyse` (all of them).

| Command              | Purpose                                                                    | Notes                                                                 |
|----------------------|----------------------------------------------------------------------------|-----------------------------------------------------------------------|
| `analyse TRACK...`   | run beat grid, waveforms, (key, loudness) in one pass over the decoded audio | decode once; `--only beatgrid,waveform`; `--force` regenerate; skips fields whose origin is `rekordbox` unless `--force` |
| `beatgrid TRACK...`  | BPM and phase detection (`beat-grid.md`)                                   | `--bpm-min 70 --bpm-max 180`, `--bpm N` fix tempo and only find phase, `--first-beat MS`, `--from-tag` use ID3 TBPM as prior, `--min-confidence F`, `--print` result only |
| `waveform TRACK...`  | render ANLZ waveform data and PNG previews (`waveforms.md`)                | `--png FILE`, `--width N`, `--height N`, `--kinds pwav,pwv3,pwv5`, `--curve FILE` fixed height mapping (never per track normalisation) |
| `loudness TRACK...`  | peak / RMS / LUFS report (to see which tracks are not normalised)          | text/tsv/json; `--sort`                                               |
| `key TRACK...`       | musical key (optional, later)                                              |                                                                       |
| `cues TRACK`         | show or edit hot cues, loops and memory points                             | `--add A=12345ms`, `--import-from FILE`                               |

Common analysis options: `--jobs`, `--timeout SEC` per track, `--continue-on-error` (default) / `--fail-fast`, `--cache DIR`, `--progress` (stderr progress lines, or `--progress-fd N` for wrappers), `--report FILE` (summary of what changed).

Analysis must be deterministic for the same input and options, so repeated runs do not dirty `rsync` diffs.

### Export and pendrives

| Command                   | Purpose                                                                              |
|---------------------------|--------------------------------------------------------------------------------------|
| `pendrive new NAME`       | create `pendrives/NAME/` with its symlink folder                                      |
| `pendrive add NAME TRACK...` | add tracks (creates `000_` ordered symlinks; `--at N` position, `--playlist DIR`)  |
| `pendrive ls NAME`        | show contents and order                                                              |
| `export NAME OUTDIR`      | build a rekordbox-format tree (`PIONEER/` + `Contents/`) in tmpfs, verify it, then write once to the **empty** `OUTDIR` (see the main doc) |
| `export` options          | `--tmpdir DIR`, `--no-verify`, `--exclude-art`, `--settings-from DIR` (copy `*SETTING.DAT`), `--no-waveforms`, `--keep-tmp`, `--sha256sums` write a checksum list |
| `sync NAME TARGET`        | update an existing exported drive with rsync semantics: `--delete-orphans` (prints what will be removed and asks unless `--yes`), `-n` dry run |
| `verify TARGET`           | re-read an exported tree with our parsers and report structural errors (usable on a real rekordbox drive too) |

### Inspection and debugging (replace/absorb the libcdj tools)

| Command                  | Purpose                                                                       |
|--------------------------|-------------------------------------------------------------------------------|
| `pdb dump FILE`          | tables, pages, rows (`--table tracks`, `--raw` hex of rows, `--pages`)         |
| `pdb diff A B`           | compare two PDBs structurally (to find unknown bytes between rekordbox versions) |
| `anlz dump FILE`         | tags and decoded content (`--tag PQTZ`), `--raw`                              |
| `anlz diff A B`          | compare two ANLZ files                                                        |
| `unknown-bytes`          | maintain `UNKNOWN_BYTES.txt`: list fields we invent, compare with imported real values (`--from PENDRIVE`) |

## Scripting examples (target behaviour)

```
# import and analyse a folder, 8 jobs, JSON report
trakcrate add --recursive --analyse --jobs 8 --format jsonl ~/Music/new > report.jsonl

# find tracks that are not normalised (peak below -3 dBFS)
trakcrate loudness --format tsv | awk -F'\t' '$3 < -3 { print $1 }'

# analyse only tracks without a beat grid
trakcrate ls --where 'beatgrid=none' --format paths | xargs -d '\n' trakcrate beatgrid

# build two identical gig drives
trakcrate export gig-2026-11 /media/usb1 && trakcrate export gig-2026-11 /media/usb2

# update a drive, show what would be removed first
trakcrate sync gig-2026-11 /media/usb1 --delete-orphans -n
```

## Implementation notes

- **CLI first**: build the commands and the helper (`libtrakcrate`: decoder, tag reader, analysers, PDB/ANLZ writers, library model) and let the GUI call the commands. The CLI is the test harness for analysis work: tests run it on fixture audio and compare to rekordbox values (`beat-grid.md`, `waveforms.md`).
- **Decoding**: `src/audio/audio_decoder.cpp` already decodes via ffmpeg to 44.1 kHz stereo; put it in `libtrakcrate` as a Qt-free C wrapper over ffmpeg's libraries (libavformat/libavcodec/libswresample). ffmpeg itself is the dependency to document for users.
- **Parallelism**: process tracks in a thread pool, bounded by `--jobs`; output order stable (sorted by path) regardless of completion order unless `--unordered`.
- **Locking**: a library lock file so two `trakcrate` runs (or the GUI) do not write the same track folder at once; read-only commands never lock.
- **Atomic writes**: write files in the master copy via temp file + rename so a killed run leaves no half-written metadata.
- **Config**: `~/.config/trakcrate/config` key=value; every option has an equivalent key, command line overrides.
- **Docs and completion**: man pages (`trakcrate.1`, one per subcommand or a combined page, matching `doc/vdj-ui.1`), `--help` for every command, bash completion script installed by the deb package (`make-deb.sh`).
- **Build**: separate CMake target(s) that do not need Qt Widgets, so a headless server package is possible.
- **Tests**: golden-file tests for `--format` outputs, exit code tests, a `--dry-run` test that proves nothing is written, and the round-trip (`export` then `verify`).
- **Naming conflict check**: `trakcrate` and its subcommands should not clash with existing commands on the user's PATH.

## Open questions

- The JSON output and `--progress-fd` formats become the GUI's API: they need a written, versioned spec before the GUI depends on them.
- Per-user commands without root: not supported by design (system install only); revisit only if a real need appears.
- Query language for `--where`: keep it tiny (field op value, `and`/`or`) or lean on `awk`/`jq` over `--format` output?
- Whether `add --analyse` should be the default (slow for big imports, but the user usually wants it).
