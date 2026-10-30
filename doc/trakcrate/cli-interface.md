# trakcrate CLI: machine interface (API version 1)

What a script may rely on: exit statuses, `--format` output, progress lines, environment. Everything not listed here (the `text` format, stderr wording, `--help` layout) can change.
Requirements and command list: `cli.md`. Library format: `TRAKCRATE-FORMAT.md`. The wrapper sets `TRAKCRATE_API=1`.

Stability rules:
- Field names, exit statuses and event names are **additive only** within an API version: new fields and new events may appear, existing ones keep their meaning. Consumers MUST ignore fields and events they do not know.
- Breaking changes bump `TRAKCRATE_API`, and `trakcrate --version` prints the API version.

## Exit status

| status | meaning                                                                                  |
|--------|------------------------------------------------------------------------------------------|
| 0      | success                                                                                  |
| 1      | runtime failure (I/O error, library invalid, nothing could be done)                      |
| 2      | usage error (bad option, unknown command, missing argument)                              |
| 3      | partial success: the command ran but some items failed (listed on stderr and in the output as `status: failed`) |
| 4      | library busy: `.trakcrate-lock` is held by a live process (`TRAKCRATE-FORMAT.md` section 16); nothing was changed |

`--dry-run` returns the status the real run would return as far as can be known, and never writes.

## Output formats

Selected with `--format` (or `$TRAKCRATE_FORMAT`). Results go to stdout; stderr is never parsed.

| format  | shape                                                                                                                    |
|---------|--------------------------------------------------------------------------------------------------------------------------|
| `text`  | for people, not stable                                                                                                   |
| `tsv`   | first line: the field names, TAB separated; then one record per line. Empty field = empty string, no value = empty. TAB, newline and backslash inside a value are written `\t`, `\n`, `\\`. |
| `jsonl` | one JSON object per line, UTF-8, keys as the field names; absent values are omitted (not `null`)                          |
| `json`  | one JSON array of the same objects                                                                                      |
| `paths` | only for commands that list tracks: one track directory per line (`-0`: NUL separated), for `xargs`                       |

Conventions for all fields: paths are absolute, UTF-8; times are integers with the unit in the name (`_ms`, `_us`, `_s`); `bpm` is a decimal number; booleans are `true`/`false` in JSON and `1`/`0` in TSV; timestamps are ISO 8601 UTC.
The record of an item that went through a processing command always has `status` (`ok`, `skipped`, `failed`) and, when not `ok`, `message`.

## Records per command

Field names are the keys of `meta.txt` (`TRAKCRATE-FORMAT.md` section 4) wherever they exist there.

| command                         | fields                                                                                                           |
|---------------------------------|------------------------------------------------------------------------------------------------------------------|
| `ls`, `info`                    | `path`, `folder`, `track`, `title`, `artist`, `album`, `genre`, `key`, `rating`, `bpm` (from the beat grid, 0 = none), `duration_ms`, `container`, `audio_sha256`, `analysed` (beatgrid, waveform present), `origin_meta`, `origin_beatgrid`, `origin_cues`, `origin_waveform`, `origin_artwork` |
| `add`, `import`                 | `path`, `source`, `status` (`ok`, `skipped`, `failed`), `reason` (`duplicate`, `exists`, `unsupported`, ...), `message`, `playlist`, `position` |
| `analyse`, `beatgrid`           | `path`, `status`, `bpm`, `first_beat_us`, `confidence`, `analyser`, `written` (list of groups changed), `skipped_groups` (with the origin that protected them), `message` |
| `waveform`                      | `path`, `status`, `columns`, `png`                                                                               |
| `loudness`                      | `path`, `peak_dbfs`, `rms_dbfs`, `status`                                                                        |
| `cues`                          | one record per mark: `path`, `beat`, `ms`, `label`, `hot`, `colour`                                              |
| `pendrive ls`                   | `playlist`, `position`, `path`, `link`, `dangling`                                                               |
| `export`                        | one summary record: `dest`, `playlists`, `tracks`, `files`, `bytes`, `verified`, `status` (`ok`, `dry-run`)      |
| `sync`                          | one summary record: `dest`, `playlists`, `tracks`, `files` (copied), `bytes` (copied), `removed`, `status`        |
| `verify`                        | one record per finding: `where` (path inside the target), `severity` (`error`, `warning`), `code`, `message`; exit 1 if any error |
| `check`                         | the same shape, for the library                                                                                  |
| `pdb dump`, `anlz dump`         | dump formats are described in their own man pages and follow libcdj's structures; always TSV or JSON lines per row/tag |

## Progress

`--progress` writes human readable lines to stderr. `--progress-fd N` writes **JSON lines** to the open file descriptor N, one event per line, flushed per line:

| event      | fields                                                                                  |
|------------|-----------------------------------------------------------------------------------------|
| `start`    | `command`, `total` (items, 0 when unknown)                                              |
| `item`     | `index` (1-based), `path`, `status`, `message`, `elapsed_ms`                            |
| `phase`    | `name` (for long single steps, e.g. `export`: `stage`, `copy`, `verify`), `done`, `total` |
| `done`     | `ok`, `skipped`, `failed`, `elapsed_ms`                                                 |

Events for one command are `start`, then `item`/`phase` events, then `done`. A process killed before `done` has failed. A cancelled run (SIGINT/SIGTERM) finishes the item in progress, writes `done` and exits with status 1 (3 if it had already processed some items).

## Environment seen by commands and scripts

`TRAKCRATE_BIN`, `TRAKCRATE_LIBEXEC`, `TRAKCRATE_HELPER`, `TRAKCRATE_LIBRARY`, `TRAKCRATE_FORMAT`, `TRAKCRATE_API` as in `cli.md`; the script variables (`TRAKCRATE_TRACK_DIR`, ...) are in `scripts.md`.
`NO_COLOR` is honoured; `--color` overrides it.
