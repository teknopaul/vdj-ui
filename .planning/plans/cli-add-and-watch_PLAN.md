# trakcrate CLI: `add`, replace-by-name, `probe`, and the two import scripts

Source: `ai-prompts/cli-usb-exporter.prompt.md` and the answers of 2026-11-01 to the feasibility review.
Everything lives under `src-cli/` (bash commands in `script/usr/lib/trakcrate/`, tests in `test/`). The C helper already has what is needed
(`probe`, `analyse`, `artwork`, `sha256`, `export`, `sync`); **no C code is planned.**

## Decisions (user, 2026-11-01)

1. **`trakcrate add FILE...`** imports single files (documented in `cli.md` as "not implemented yet"; there is no `trakcrate-add` script).
2. **Artwork is edited directly on the filesystem** (`track-master-copy/<folder>/<track>/artwork.jpg|png`) with ImageMagick. No `trakcrate artwork` command.
   The exporter crops and scales to the rekordbox size itself, so scripts only add the watermark. They set `origin.artwork: edited` in `source.txt`
   (`tc_kv_set`, or any text tool) so a later `analyse` or re-add does not discard it.
3. **No dedupe by audio hash.** A track is identified by its place: `<folder>/<track name>`, where the track name is the sanitised file name without extension
   (two tracks with one name are not usable on the decks anyway). The hash `audio_sha256` is used only to decide *whether the audio changed*:
   - same name, same hash: audio and analysis stay; the tags in `meta.txt` are re-read (except fields whose `origin.meta` is `edited`/`rekordbox`);
   - same name, different hash: the audio is replaced in place, the generated analysis (beat grid, cues, waveform) is dropped and redone, protected groups
     (`origin.* = edited|rekordbox`) are kept; playlist symlinks keep working because the directory does not move;
   - no such name: a new track.
   The user updates tracks often, so replacing is the normal case, not an error. `import DIR` gets the same behaviour (see Phase 2).
4. **Reading tags for the watcher**: `trakcrate probe FILE...` becomes a public command (today it is only `trakcrate-helper probe`).
5. **`inotify-tools` becomes a `Depends:` of the `trakcrate-cli` .deb.** ImageMagick (`convert`) is used by the example scripts only: `Recommends:`.
6. **No USB handling.** A mount point is just a directory: the scripts call `trakcrate sync PLAYLIST... DIR` and `trakcrate verify DIR`, nothing more
   (no detection, no eject). No special USB workflow is documented.

## Facts the phases rely on (checked 2026-11-01)

- `trakcrate-import` (330 lines) holds the whole per-file import inline in one loop: probe, free-name search ("name 2"), temp dir `.import.$$.N`, `meta.txt`
  fields (`original_filename`, `audio_sha256`, `file_size`, `date_added`), `source.txt` with `origin.*: generated`, `imported_from`, `imported_at`; then the playlist
  block (`tc_playlist_load`, `tc_playlist_write`, `tc_track_link`); the lock is `tc_lock` / `tc_unlock`.
- `common.sh` has `tc_track`, `tc_kv_get/set/unset`, `tc_sanitize_name`, `tc_safe_name`, `tc_playlist_*`, `tc_lock`, `tc_confirm`, `tc_common_opt`.
- `trakcrate-analyse` redoes generated groups every time and skips protected groups unless `--force`; it needs the library lock free.
- `trakcrate-helper probe FILE [--artwork-dir DIR]` prints `meta.txt` lines (`title artist album genre label year track_number comment bpm_tag` ...).
- Tests: `src-cli/test/test_*.sh`, helpers in `test/lib.sh` (`run`, `tc`, `assert_*`, `make_track`), run with `make test` in `src-cli/`.
  `test_import.sh` currently asserts duplicates (`imported 0, duplicates 2`, `"reason":"duplicate"`, `--no-dedupe`).
- Never use git. Do not touch the GUI code (`src/`); the GUI is an independent implementation of the format.
- The tests need `script/usr/lib/trakcrate/trakcrate-helper` to be reachable (the oracle test fails in a tree where it is not installed). Run `make test` after
  `make -C c`, and read failures before assuming your change caused them.

---

## Phase 1: extract the per-file import into `import-lib.sh` (no behaviour change)

Goal: `trakcrate import` and the new `trakcrate add` share one function. Pure refactor, all existing tests stay green.

1. Create `script/usr/lib/trakcrate/import-lib.sh` (sourced by commands, not an executable command: make sure the wrapper's command listing, `--list-commands`,
   ignores it; check how `common.sh` is skipped and do the same).
2. Move the body of the loop in `trakcrate-import` (from the hash through `mv -- "$tmp" "$final"`) into
   `tc_import_one SRC FOLDER STEM MODE` which sets result variables (`tc_imp_dir`, `tc_imp_status` = `ok|failed`, `tc_imp_reason`, `tc_imp_msg`, `tc_imp_hash`)
   and appends nothing to the caller's arrays. Keep the temp-dir-then-`mv` atomic pattern.
3. `trakcrate-import` sources it and calls the function. Output records, summary line and exit codes must be byte identical.
4. `deploy/build-deb.sh` and `test_package.sh`: confirm the new file is packaged (`script/` is copied wholesale; check the file mode is 644, not 755).

Verify: `cd src-cli && make test` gives the same results as before the change (the oracle test may fail for the unrelated missing-helper reason; compare the list
of failing tests with a run before you start and write both lists in the progress note).

```sh
geany-progress done 1 -r src-cli/script/usr/lib/trakcrate/import-lib.sh -r src-cli/script/usr/lib/trakcrate/trakcrate-import -w "refactor only: compare make test before and after"
```

## Phase 2: replace-by-name semantics, drop hash dedupe

Goal: decision 3, in `tc_import_one`, so both `import` and `add` get it.

1. In `tc_import_one`, after computing the track name `stem`: if `$tc_master/$folder/$stem` exists (no more "name 2"), compare `sha256` of the new file with
   `audio_sha256` in its `meta.txt`:
   - **same hash**: re-run `probe`, merge into `meta.txt`: take the new tag values for the keys in the probe output unless `source.txt` has `origin.meta: edited|rekordbox`
     (then leave `meta.txt` alone); keep `date_added`, update `file_size`; keep the audio file (do not copy again); keep the analysis. Status `unchanged` if nothing differed, `updated` otherwise.
   - **different hash**: build the new directory in the temp dir as for a new track, but start from the existing directory: carry over files of protected groups
     (`beatgrid.txt`, `cuemarks.txt`, `waveform.bin`, `anlz/`, `artwork.*`, `vocals.txt`, according to `origin.<group>` in `source.txt`, format doc section 7);
     groups with `origin: generated` are not carried over (they are stale). The artwork is the exception: a fresh embedded cover replaces a `generated` artwork,
     an `edited` one stays. Swap with `mv` of the old directory to `.trash`-style temp name then the new one into place, so there is no moment without the track
     (same rename dance as `Library::renameDrive` / the temp-dir pattern already used). Status `replaced`. Remove the `.import.*` temp dir on any failure.
   - Status values added to the records: `unchanged`, `updated`, `replaced` (with `ok` for new). Add them to the `rec_status`/`rec_reason` handling and to the summary
     line: `imported N, replaced N, updated N, unchanged N, unsupported files N, failed N` (keep `imported`/`failed` wording where tests rely on it, update tests).
2. Remove the audio-hash dedupe: delete the `index` file building (the `grep -r ... audio_sha256` over the whole master copy is also slow on a big library), the
   `existing` lookup and the `duplicate` skip. Keep `--no-dedupe` accepted and ignored (documented as obsolete) so old scripts do not break.
3. A track that already exists must still go into the playlist if it is not there yet (current behaviour for duplicates), and keeps its position if it is.
4. Update `test_import.sh`: replace the duplicate assertions (lines ~33, 71, 90-120, 148) with: re-import unchanged => `unchanged`, retouched tags => `updated`,
   changed audio (use `gen_beats.py` with another tempo) => `replaced` and the generated `beatgrid.txt`/`waveform.bin` gone, `origin.beatgrid: edited` beat grid kept,
   playlist symlink still resolves, no "name 2" directories appear. Update `test_import_rekordbox.sh` only if it depends on the old summary text.

Verify: `make test` in `src-cli/`; also run the new assertions on a copy of a real library (`cp -a --reflink=auto`), never on `/var/vdj/trakcrate`.

```sh
geany-progress done 2 -r src-cli/script/usr/lib/trakcrate/import-lib.sh -r src-cli/test/test_import.sh -w "behaviour change: import no longer dedupes by hash; --no-dedupe is a no-op" -w "never test against the real library at /var/vdj/trakcrate"
```

## Phase 3: `trakcrate add`

Goal: decision 1.

Create `script/usr/lib/trakcrate/trakcrate-add` (mode 755, same header, `tc_help_or_summary`, `tc_common_opt` as `trakcrate-import`):

```
Usage: trakcrate add [options] FILE...

  --folder NAME      master folder (default: the name of the directory the file is in)
  --playlist NAME    also put the track into this playlist; repeatable. A playlist that does not exist is created.
                     A track already in it keeps its position; a new entry goes at the end.
  --copy (default) | --move | --link
  --analyse          analyse the track unless the audio is unchanged and already analysed (beatgrid.txt and waveform.bin present)
  --files-from FILE|-  (-0 for NUL separated) the files to add, one per line
  -n/--dry-run, --yes, --format FORMAT, --library DIR, -q, -v
Output: as "import": one record per file (path source status reason message playlist position); with --format paths the track directory per line.
Exit status: 0, 3 some failed, 1 all failed.
```

1. Only `.mp3` and `.wav` (case-insensitive); anything else is a failed record `unsupported`, not a hard error. Resolve with `readlink -f`.
2. Take the lock once, call `tc_import_one` per file, release the lock, then (if `--analyse`) run `trakcrate analyse` on the directories that need it (the lock must
   be free, as `import --analyse` already does; reuse that code path rather than copying it, move it into `import-lib.sh` if needed).
3. Playlist handling for several `--playlist` names: reuse the block from `trakcrate-import` (`tc_playlist_load`, `tc_track_link`, `tc_playlist_write`); move it into
   `import-lib.sh` as `tc_playlist_add_track PLAYLIST TRACKDIR` and let `import` use it too if that stays simple.
4. `--files-from` and bare arguments can be mixed. At least one file is required (usage error otherwise).
5. Tests: new `test/test_add.sh` (copy the structure of `test_import.sh`): new track, two playlists, second add of the same file => `unchanged`, new tags => `updated`,
   new audio => `replaced` + analysis redone with `--analyse`, wrong extension, missing file, `--move` removes the source, `--link`, `--dry-run` writes nothing,
   `--format paths`, playlist order after several adds.

```sh
geany-progress done 3 -r src-cli/script/usr/lib/trakcrate/trakcrate-add -r src-cli/test/test_add.sh -w "add --analyse reuses the lock-release logic of import"
```

## Phase 4: `trakcrate probe`

Goal: decision 4. Make the tag reader available to scripts.

Create `script/usr/lib/trakcrate/trakcrate-probe`:

```
Usage: trakcrate probe [options] FILE...
  --field NAME       print only this field's value (one file)   e.g.  trakcrate probe --field bpm_tag song.mp3
  --artwork-dir DIR  write the embedded cover to DIR/artwork.jpg|png (one file)
  --format FORMAT    text (default: key: value lines, as meta.txt), tsv (file + all fields as columns), json, jsonl
```

1. Calls `"$TRAKCRATE_HELPER" probe` (see `c/helper/cmd_probe.c` for the fields); for several files, one block or record each, with the file path as the first field.
2. Exit status 1 for a file that is not usable audio (message on stderr); with several files, 3 if some failed.
3. `tc_help_or_summary` summary line, entry in the wrapper's command list if it keeps one, and a test `test/test_probe.sh` (generate files with `gen_beats.py`; tags
   can be added with `ffmpeg -metadata` if ffmpeg is on the machine, otherwise skip those assertions like other tests skip missing tools).

```sh
geany-progress done 4 -r src-cli/script/usr/lib/trakcrate/trakcrate-probe -r src-cli/test/test_probe.sh
```

## Phase 5: the two example scripts, dependency, docs

Goal: decisions 2, 5, 6. Scripts use only `trakcrate`, ImageMagick (`convert`) and `inotifywait`.

1. Install under `script/usr/share/doc/trakcrate-cli/examples/` (mode 755), with `set -eu`, a usage text and `shellcheck`-clean (the project has a `c-bash` skill for
   conventions; follow it):
   - `import-own-track.sh LIBRARY MP3 PLAYLIST IMAGE WATERMARK [MOUNTPOINT]`
     1. `trakcrate --library LIBRARY add --playlist PLAYLIST --format paths MP3` gives the track directory.
     2. Artwork: `convert IMAGE -gravity center -crop` to a square (the exporter makes the final 80x80, so the master only needs to be square and of reasonable size,
        e.g. 600x600), then `composite -gravity southwest WATERMARK` for the bottom left corner, write `artwork.jpg` into the track directory (remove an old `artwork.png`),
        and set `origin.artwork: edited` in `source.txt`.
     3. `trakcrate analyse DIR` only when the audio is new or changed (the `add` record status is `ok`/`replaced`, or use `add --analyse`).
     4. If MOUNTPOINT is given (any directory, USB or not): `trakcrate sync PLAYLIST MOUNTPOINT` then `trakcrate verify MOUNTPOINT`, then `sync` (coreutils, flush). Nothing else.
   - `watch-folder.sh LIBRARY WATCHDIR PLAYLIST... [-w WATERMARK]`
     1. `inotifywait -m -e close_write -e moved_to --format '%w%f' WATCHDIR`; ignore names that are not `.mp3`/`.wav` and hidden or `*.part`/`*.tmp`.
     2. Debounce: wait until the file size is unchanged for 2 seconds (copy still running), and coalesce repeated events for one file.
     3. `trakcrate probe` shows the artist, title and BPM to the log; `trakcrate add --copy --folder <WATCHDIR name> --playlist P... --analyse FILE`
        does the import or the replace (the tags come from the file, the BPM goes into `bpm_tag` and is the analyser's tempo hint).
     4. The embedded cover is extracted by `add` already; if a watermark was given, process `artwork.*` as in the first script and mark it `edited`
        (only when `add` said `ok` or `replaced`; for `unchanged`/`updated` leave the artwork alone so the watermark is not applied twice).
     5. Exits cleanly on SIGINT/SIGTERM; handles `inotifywait` ending; one library operation at a time (a simple queue, the library has a lock anyway).
2. `deploy/DEBIAN/control.in`: `Depends:` add `inotify-tools`; `Recommends: imagemagick`. Check `deploy/build-deb.sh` and `test_package.sh` for anything that lists the
   Depends line or the files, and that the examples are packaged with the right mode.
3. Docs (`doc/trakcrate/cli.md`): move `add` out of "Not implemented yet" (line ~116) and describe it, the replace-by-name rules (section on `import` too: remove the
   "same audio is not stored again" text, line ~124 and the `--no-dedupe` mentions), add `probe` to the command tables, add a short "Scripting" section pointing to the two
   examples and saying that artwork is changed directly in the track directory (name `artwork.jpg|png`, square, `origin.artwork: edited`). **No USB section.** Update
   `TRAKCRATE-FORMAT.md` only if it describes duplicate handling on import (grep `duplicate`, `dedup`, `sha256`).
4. Tests: `test/test_examples.sh`: `bash -n` and, if `shellcheck` is installed, `shellcheck` on both scripts; run `import-own-track.sh` end to end on a generated wav and
   image when `convert` exists (skip otherwise), checking the artwork size and `origin.artwork: edited`. The watcher is checked by `bash -n` and a short run: start it with
   a 3 second `timeout`, copy a wav into the watched folder, and assert the track exists (skip when `inotifywait` is missing).

```sh
geany-progress done 5 -r src-cli/script/usr/share/doc/trakcrate-cli/examples -r src-cli/deploy/DEBIAN/control.in -r doc/trakcrate/cli.md -w "inotify-tools is now a hard Depends of trakcrate-cli" -w "examples need ImageMagick (Recommends only)"
```

## Out of scope

- A `trakcrate artwork` command, USB detection, mounting, ejecting, free-space checks beyond the exporter's own.
- Changes to the C helper or to the GUI (`src/`): the GUI importer keeps its own rules; if the GUI should also replace-by-name, that is a separate plan.
- `cues`, `vocals`, `key` commands.
