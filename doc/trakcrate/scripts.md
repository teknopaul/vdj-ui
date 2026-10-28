# trakcrate scripts (requirements)

Users can drop their own scripts and binaries into the library and run them from vdj-ui (and the CLI) to apply "hacks" to a loaded track or to the whole database. It is the user's responsibility that a script does something sane; trakcrate's job is to find scripts, run them predictably and safely, show the result, and make a mistake recoverable.

## Layout

A `scripts/` directory in the base of the library, next to the others in `trakcrate-db-format.md` (that file's tree should gain this entry):

```
trakcrate/
├── scripts/
│   ├── track-script-fix-titles       operates on the loaded track
│   ├── track-script-double-bpm.py
│   ├── crate-script-strip-original-mix    operates on the whole database
│   └── crate-script-backup.sh
├── user-settings/
├── track-master-copy/
└── pendrives/
```

- **`track-script-*`**: runs on one track (the one loaded in the selected deck or selected in the trakcrate pane).
- **`crate-script-*`**: runs on the whole database.
- Only regular files with the **executable bit** set (for the current user) and the matching prefix count. Everything else in `scripts/` (a README, `.orig` files, libraries a script imports) is ignored. Subdirectories are ignored, so a script may keep helpers in `scripts/lib/`.
- Any language: shell, Python, Perl, a compiled binary. A script needs a shebang line.
- The part after the prefix is the script's name (`fix-titles`), shown in the UI. An optional description comes from the file's second line if it is a comment of the form `# description: ...`, or from a companion file `<script>.desc`; no execution is needed to show it.
- Ordering: alphabetical. A numeric prefix after `script-` (`track-script-10-...`) is just part of the name and gives the user control over the order.

## Calling convention

Plain, so scripts work with any tool and are testable from a terminal. Scripts are executed **directly** (`execve`), never through a shell string.

**Working directory**: the track folder for a track script, the library base for a crate script.

**Arguments**:
- track script: `track-script-NAME TRACK_DIR` (absolute path of the track folder in `track-master-copy`)
- crate script: `crate-script-NAME LIBRARY_DIR`

**Environment** (in addition to the user's normal environment; unset variables are simply absent):

| Variable                  | Meaning                                                                          |
|---------------------------|----------------------------------------------------------------------------------|
| `TRAKCRATE_LIBRARY`       | absolute path of the library base                                                |
| `TRAKCRATE_SCRIPT_KIND`     | `track` or `crate`                                                               |
| `TRAKCRATE_SCRIPT_NAME`     | the script's name                                                                  |
| `TRAKCRATE_TRACK_DIR`     | track script: the track folder                                                     |
| `TRAKCRATE_TRACK_AUDIO`   | track script: the audio file in the folder                                         |
| `TRAKCRATE_TRACK_POSITION_MS` | track script: the deck playhead if the track is loaded in a deck, else absent  |
| `TRAKCRATE_DECK`          | track script: the deck number when launched from a deck                            |
| `TRAKCRATE_SELECTION`     | crate script: path of a file listing the selected track folders (one per line, NUL separated if `TRAKCRATE_SELECTION_NUL=1`), absent if nothing is selected |
| `TRAKCRATE_DRY_RUN`       | `1` when run with dry run, a script that supports it should only print what it would do |
| `TRAKCRATE_BIN`           | path of the `trakcrate` CLI, so scripts can call back (`$TRAKCRATE_BIN set ...`)   |
| `TRAKCRATE_API`           | version of this convention (`1`)                                                 |

**stdin**: closed (/dev/null). **stdout** and **stderr** are captured and shown. **Exit status**: 0 = success; non-zero = failure, shown to the user in red, with the captured output. Scripts that want to report counts can print lines `trakcrate: ...` which the UI highlights; nothing else is parsed.

Scripts should edit files through the `trakcrate` CLI where they can (it validates, locks and marks `origin: edited`), or directly in the track folder (the files are plain text, which is the point of the format).

## Safety rules (what trakcrate does so scripts are survivable)

1. **Only ever run manually.** A script runs only when the user starts it from the UI or the CLI. Nothing triggers a script: not an import, not an export, not loading a track, not a timer. That is why they are called scripts and not hooks. There is no event mechanism and none is planned.
2. **Backup before running**: for a track script, snapshot the track folder's small files (everything but the audio) to `.trakcrate-history/<time>-<script>/` inside the track folder; for a crate script, a snapshot of all metadata files (tar of everything except audio and images, which is small) in `.trakcrate-history/`. Offered to restore via "Undo last script". Disable per script with a `# no-snapshot` header (scripts that are themselves backups). The audio files are never snapshotted (large), which is the one thing a script can break for good; the confirmation dialog says so.
3. **Confirmation**: crate scripts always ask first ("Run crate-script-X on all N tracks?"), with a "don't ask again for this script" that the user must consciously select; track scripts run on one click.
4. **Library lock** is held during the run so the GUI, CLI and other scripts do not write at the same time (`cli.md`); read-only operation of the app continues.
5. **Timeout** (default 5 minutes for track scripts, none for crate scripts but with a Cancel button that sends SIGTERM, then SIGKILL after 5 s); the whole process group is signalled, so child processes die as well.
6. **Run as the user**, nothing else: no privilege escalation, no sudo prompts in the GUI (stdin is closed so a script cannot wait on input; one that needs `sudo` fails and says so).
7. **Resource limits**: nice 10 (so audio playback is not starved while a script runs), output captured up to a limit (1 MiB, then truncated with a notice).
8. **Warnings in the list**: a script that is world-writable, or in a directory writable by others, is shown with a warning and not runnable until fixed; a script owned by another user is shown disabled. (The library is the user's own folder; this protects against a shared or cloned library.)
9. After a run: reload the affected track(s) (changed files are detected, and a loaded deck refreshes its grid, cuemarks and artwork), and show the result (exit status, duration, the captured output) in a Scripts log.
10. **A script that fails does not roll back automatically**; the output is shown and the snapshot is offered for restore.

Trust model: scripts are arbitrary code with the user's permissions, exactly as a script the user runs by hand. Importing a library or a shared crate (`import-rekordbox`, a library cloned from somebody) must **not** bring scripts along silently: `scripts/` is never created by an import, and the first time a script the library did not "know" appears, the UI shows it as new and asks before it can be run ("new script, review it first"; it can be opened in the editor from that prompt).

## UI

In the trakcrate pane and the track editor (`gui.md`, `track-editor.md`):

- A **Scripts** menu/button in the track editor lists the `track-script-*` entries, in order, with name, description and an icon showing state (ready, warning, new/unreviewed). Choosing one runs it on the loaded track.
- A **Scripts** menu in the library toolbar lists the `crate-script-*` entries and runs them on the whole database (or the current selection, when the script declares `# selection: yes` and the selection exists).
- Context menu per script (right click, or a "..." button):
  - **Run**
  - **Run (dry run)** (sets `TRAKCRATE_DRY_RUN=1`; shown only if the script has `# dry-run: yes`)
  - **Open with editor...** (below)
  - **Show in file manager**
  - **Copy path**
  - **Show last output**
  - **Disable** (stored in user settings as a list of disabled names; the file is untouched)
- A **Scripts log** panel (dock tab or in the message area): time, script, target, status, duration, expandable output. Kept in memory for the session and also appended to `scripts/.log` (plain text).
- A **New script** action: asks for a kind (track/crate) and name, then writes a template script with the calling convention in comments, sets the executable bit and opens it in the editor, so a user starts from something working.
- The list refreshes when the directory changes (`QFileSystemWatcher`), and has a Refresh button.
- Progress: while a script runs, the button shows a spinner and a Cancel; the deck stays playable. Running more than one script on the same library at once is refused (lock).

### Open with editor (Ubuntu)

- "Open with editor" opens the script source in the user's graphical text editor. Use the same desktop-database mechanism as the artwork editor (`track-editor.md`): list the applications registered for `text/plain` and `text/x-shellscript` / `text/x-python` (from the `.desktop` files, e.g. gedit, Geany, Kate, VS Code, Sublime), a default one (`xdg-mime query default text/plain`), the choice remembered per user (`script_editor`), and a custom command executed without a shell.
- **Not** `xdg-open` for executable scripts without care: a file manager or `xdg-open` on an executable shell script may *run* it (or ask), which is the opposite of the intent. Always launch the chosen editor application explicitly with the file as its argument (`gtk-launch <desktop-id> FILE`, `gio launch`, or the `Exec=` line with `%f` filled in and run without a shell).
- A terminal editor (vim, nano) is allowed: configured as a command prefixed with the terminal emulator (`x-terminal-emulator -e vim FILE`), set in the settings.
- Binary scripts (compiled executables) are not opened in an editor; the entry is "Show in file manager" only (detected by MIME type: not text).
- After the editor closes, or the file changes on disk, the list refreshes; a script modified since it was reviewed shows as "changed" and, in the unreviewed-scripts rule above, needs a confirmation before it can be run again (the review state is a SHA-256 of the file stored in user settings, never in the script).

## CLI equivalents

```
trakcrate scripts ls [--kind track|crate]            list with status (ready / warn / new)
trakcrate scripts run NAME [TRACK] [--dry-run] [--yes] [--no-snapshot]
trakcrate scripts new track|crate NAME [--editor]    template
trakcrate scripts review NAME                        mark reviewed (records the hash)
trakcrate scripts undo [TRACK]                       restore the last snapshot
```

The GUI runs these same commands as child processes, so a script behaves identically from both.

## Example scripts (shipped as documentation, not enabled)

Installed under `/usr/share/doc/vdj-ui/script-examples/`, copied by "New script" if the user wishes:

- `track-script-title-from-filename`: sets the title from the audio file name.
- `track-script-halve-bpm`: halves the BPM in `beatgrid.txt` and rewrites the beats (via `$TRAKCRATE_BIN`).
- `crate-script-strip-original-mix`: removes " (Original Mix)" from all titles (an earlier request in the development prompts).
- `crate-script-backup`: `rsync -a` of the library to a backup drive.
- `crate-script-lowercase-genres`.

## Open questions

- How a script returns structured changes to the UI (nothing now; the UI just reloads files).
- Whether scripts should run in a sandbox (bubblewrap/firejail) as an option; not by default, since a script's purpose is to change the user's files and may legitimately need a lot of the system.
- Windows or macOS: out of scope (Ubuntu only; the paths, `.desktop` files and executable bits assume Linux).

## Implemented (phase 14)

- **Core** (`src/trakcrate/scripts.*`, no Qt): discovery, states (ready / new / changed / warning / foreign / disabled), headers (`# description:`, `# dry-run: yes`, `# selection: yes`, `# no-snapshot`, `.desc` companion), arguments, the `TRAKCRATE_*` environment, snapshots and restore.
- **Runner** (`src/ui/trakcrate_scripts.*`): `execve` through `QProcess`, no shell; own process group (timeout and Cancel signal the whole group: SIGTERM, SIGKILL after 5 s); stdin closed; nice 10; output capped at 1 MiB; 5 minute timeout for track scripts, none for crate scripts; library lock file `.trakcrate-lock` (a lock left by a dead process is taken over); appends to `scripts/.log`.
- **Review and disabled state** are stored per user in the app config (sections `trakcrate-script-reviewed` and `trakcrate-script-disabled`), not in the library, so a cloned library cannot arrive pre-approved. A new or changed script asks for review before it runs.
- **UI:** a Scripts button in the trakcrate pane (crate scripts) and a Scripts button in the track editor (track scripts). Each script has Run, Run (dry run), Open with editor, Show in file manager, Copy path, Show last output, Disable. Also New script, Undo last script, Scripts log. Crate scripts ask first. After a run the pane, editor and deck reload.
- Open with editor uses the remembered `trakcrate/script_editor` or the first `text/plain` application in the `.desktop` files; never `xdg-open`.
- **Not done:** the CLI equivalents, the `TRAKCRATE_DECK` / `TRAKCRATE_TRACK_POSITION_MS` values from a deck (the environment code supports them, the menu does not pass them yet), selection files for crate scripts, `# selection: yes` handling in the UI, a spinner while running, watching `scripts/` for changes (the menu is rebuilt each time it opens), the example scripts, and an optional terminal editor command.
