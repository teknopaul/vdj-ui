# trakcrate UI pane (requirements)

A pane in vdj-ui for editing the trakcrate master database (`trakcrate-db-format.md`). It is a front end on the `trakcrate` command line (`cli.md`): the GUI does **not** link any trakcrate library; it runs `trakcrate <command>` for every change and every analysis, so every action here has a CLI equivalent by construction and the GUI never holds logic of its own. See "GUI and CLI are separate" below. Because the database is a plain directory tree, the pane must cope with files changing underneath it (the user editing `meta.txt` in an editor, a CLI run in a terminal).

## Placement

vdj-ui builds its panels as `QDockWidget`s in `MainWindow` (Debug at the bottom, Recording and the Rekordbox PC mode on the right, toggled by checkable actions). The trakcrate pane follows that pattern: a dock created lazily by a `trakcrateDock()` accessor, toggled from a View menu action, with its widget in its own `src/ui/trakcrate_panel.{h,cpp}` (as `rekordbox_mode_panel` does). It will want to be large, so it must also be floatable and openable as a separate window. State (selected library, columns, splitter positions) is saved with the other window settings.

## Layout

```
┌ trakcrate ─────────────────────────────────────────────────────────────┐
│ Library: [~/trakcrate ▾] [Open…] [Import folder…] [Import rekordbox…]  │
├───────────────┬────────────────────────────────────────┬──────────────┤
│ Folders       │ Tracks (filter: [__________])           │ Track        │
│  ▸ Acid Loops │  Title   Artist   BPM  Key  Wave  Orig  │ ┌──────────┐ │
│  ▸ Gig 2026   │  …                                      │ │ artwork  │ │
│ Playlists     │                                         │ └──────────┘ │
│  ▸ friday     │                                         │ title …      │
│  ▸ Gig 2026   │                                         │ artist …     │
│               │                                         │ [waveform]   │
│               │                                         │ cues, grid   │
├───────────────┴────────────────────────────────────────┴──────────────┤
│ Jobs: analysing 3/12 ████░░░░  [Cancel]   Messages                     │
└────────────────────────────────────────────────────────────────────────┘
```

Left: folders of `track-master-copy` and the playlists (`pendrives/*`). Centre: track table. Right: detail/editor for the selection. Bottom: background jobs and a message log.

## Functions

### Library
- Open or create a library (`init`); remember recent ones.
- Import a folder of bare audio files (dialog for folder, playlist name defaulting to the directory name, recursive, analyse after; same options as `trakcrate import`).
- Import a real rekordbox pendrive (`import-rekordbox`) with a report dialog: counts, missing audio, conflicts.
- Rescan when files change on disk (file system watcher, plus a Refresh button; large libraries must not be rescanned on every change event).

### Track table
- Columns: title, artist, album, genre, BPM, key, duration, loudness, a mini waveform (reuse `waveform_delegate`), origin (generated / rekordbox / edited), status flags (missing audio, no beat grid, not analysed).
- Sort and filter by any column; quick filter box; saved filters for chores such as "no beat grid", "origin rekordbox".
- Multi-select; drag to reorder inside a playlist and drag tracks between folders and playlists; the drag changes the `000_` symlink prefixes.
- Context menu: Analyse, Re-analyse, Open folder in file manager, Remove from playlist, Remove from library (confirmation).

### Editing a track
- Metadata form (title, artist, album, genre, label, key, BPM, year, rating, colour, comment) saved to the readable `meta.txt`; fields changed by the user are marked `edited` in `source.txt`.
- **Bulk edit** for several selected tracks (set genre, album, strip " (Original Mix)" from titles, as noted in the earlier prompts).
- **Artwork**: show it, replace by file picker or drag and drop, remove, re-extract from the tag (`artwork.md`).
- **Beat grid**: show the grid on the waveform, change BPM, nudge the first beat, tap tempo, double/halve the tempo, set the downbeat, re-detect (`beat-grid.md`). Warn before overwriting a grid whose origin is `rekordbox`.
- **Cues and loops**: add, move, delete hot cues A-H, memory points and loops, with comments/colours once supported (`track_analysis` and `waveform_window` already draw cues, loops and the grid; here they become editable).
- **Waveform**: preview only, plus "regenerate". Heights are never normalised per track, so the DJ can see an unnormalised track (`waveforms.md`).
- Audition: play the track from the master (the app has an audio player), so the DJ can check a grid by ear.

### Playlists / pendrives
- Create, rename, delete; add tracks; reorder with drag and drop; show the resulting order numbers.
- Per-pendrive summary: track count, total size, estimated export size, and tracks that would fail (missing audio, not analysed).
- **Export** to a folder or mounted drive (the staged tmpfs-then-single-write flow), with progress and a verify step; **Sync** an existing drive with a preview of what will be added and what orphans will be removed, with the mandatory warning before removing.

### Provenance and unknown bytes
- Origin per field group is visible and filterable; a "Regenerate" action changes it to generated after a confirmation.
- A view of `UNKNOWN_BYTES.txt` entries and which are filled from real imports, so contributors see what is still invented.

## Behaviour requirements

- **Never block the UI thread.** Decoding, analysis, import, export and verify run as background jobs (thread pool, as in the CLI `--jobs`), with progress, cancel, and results back as signals. The panel remains usable while jobs run; track rows update as results arrive.
- **GUI and CLI are separate.** The GUI is a separate package that depends on `trakcrate-cli` and runs it as a child process (`QProcess`, no shell, arguments as a list, `TRAKCRATE_LIBRARY` set from the open library). Concurrency: the CLI takes the library lock for writes; if it is held (another terminal, a script) the command fails with a documented exit status and the GUI shows "library busy" instead of failing.
- **Undo.** Metadata and cue edits go on a `QUndoStack` for the session. Destructive actions (remove from library, delete orphans, replace playlist) are confirmed and, where possible, move files to a `.trash/` folder inside the library instead of deleting.
- **Atomic, small writes**: edits write the changed file via temp and rename; no bulk rewrite of the library.
- **External edits**: if a `meta.txt` changes on disk while the user has unsaved edits in the form, show a conflict prompt rather than overwriting.
- **Large libraries**: use a model/view with lazy loading (`sample_tree_model` is the existing pattern for trees); thousands of tracks, thumbnail and waveform images loaded on demand and cached.
- **Accessibility and keyboard**: all actions in menus with shortcuts; the table is fully keyboard driven. Dark theme like the rest of the app (`dark_theme`).
- **No hidden state**: anything the pane changes is visible as a file in the library, which is the point of the format.
- **Safety with real pendrives**: the pane writes to physical drives only through the export/sync flow, never during edits.

## GUI and CLI are separate

Decided: no shared library between the GUI and the command line tools.

- **Writes and analysis go through the CLI.** Editing a rating is `trakcrate set TRACK rating=4`, a cuemark is `trakcrate cuemarks TRACK --add-ms ...`, analysis is `trakcrate analyse` and so on. The commands validate, lock, write atomically and mark the origin, so the GUI cannot drift from them. Machine readable output (`--format json`/`jsonl`) and `--progress-fd` are the API; they are documented as stable (`TRAKCRATE_API`).
- **Reads may go straight to the files.** The library is plain text and images (`trakcrate-db-format.md`), so the C++ side reads `meta.txt`, `cuemarks.txt`, `beatgrid.txt` and artwork directly for display, as it already reads rekordbox ANLZ files, and watches them for changes. Parsing is small; the formats are documented in these docs. Anything needing the helper's binary formats (PDB, ANLZ, audio) is requested from the CLI instead (`trakcrate info --format json`).
- **Undo** is the GUI's own stack of inverse commands (set the old value back), not a library feature; destructive actions use the `.trash/` folder and the script snapshots (`scripts.md`).
- **Extending the GUI.** Because everything is a command, users who add `trakcrate-<name>` commands in `/usr/local/lib/trakcrate/` or `track-script-*` / `crate-script-*` scripts in a library can run them from the GUI's script menus (`scripts.md`); the GUI needs no knowledge of them. This is the reason for the split.
- **Process cost**: one process start per edit is fine for interactive use; bulk operations (import, analysis, export) are single commands with progress, not loops of calls from the GUI.
- **Packaging**: the vdj-ui package `Depends: trakcrate-cli`, with the same version policy (`cli.md`, Packaging). The CLI never depends on the GUI.

## Build and code organisation

- New files under `src/ui/`: `trakcrate_panel`, `trakcrate_track_model`, `trakcrate_track_editor`, `trakcrate_import_dialog`, `trakcrate_export_dialog`, and a `trakcrate_jobs` helper. Business logic stays in the CLI commands (`cli.md`); the C++ side only formats, displays and calls commands.
- Tests: model tests with a temporary library built from fixtures; no GUI-driven tests needed for logic, since it lives in the library.

## Phasing

1. Read-only browser: open a library, folders/playlists/tracks, details pane, waveform and artwork preview.
2. Import dialogs (folder, rekordbox pendrive) with background jobs.
3. Metadata, artwork and playlist editing, undo.
4. Beat grid and cue editing, audition.
5. Export / sync with preview, verify and the orphan warning.

## Open questions

- A dock inside the main window, or a separate top-level "library" window? The deck UI is busy; a separate window may suit editing better and the dock can stay a small status/launcher.
- How much of the existing Rekordbox PC-mode and `track_browser` should be reused? The browser already shows folders and rekordbox DBs from pendrives; the trakcrate pane could be a mode of it so a user can drag from a real pendrive into the master.
- Undo scope across the CLI and GUI (the CLI has none).
- Whether bulk text edits (regex rename) belong in the GUI or only in the CLI.

## Preview player (implemented)

A mini player sits under the track list (`src/ui/trakcrate_preview_player.*`). It plays the shown playlist, a list of a virtual pendrive, or (on the pendrive itself) all its lists one after the other, so a DJ can listen to a whole set at home.

- Play/Pause, Stop, previous and next. "Play the list from here" is in the track context menu. Folders cannot be played.
- **Natural BPM** plays each track at its own tempo. **All at BPM** plays every track at one BPM: the track's BPM comes from `beatgrid.txt` (else the BPM tag), the tempo ratio is clamped to 0.5 to 2, and a track without a BPM plays at its natural tempo. The stretching is done in memory by `AudioPlayer`; no file is changed.
- The playing track is selected in the list as it changes, so its artwork and details show. On a pendrive the tree moves from list to list. If you browse another list meanwhile, the view is left alone.
- One list at a time. Playing is refused while the app is connected to the Pro DJ Link network, Connect stops the player first, and loading a track into the deck stops it. Stop gives the audio output back to the DJ decks.
- The mode and BPM are remembered (`trakcrate/preview_mode`, `trakcrate/preview_bpm`).

## Ordering (implemented)

- A playlist (and a list of a virtual pendrive) is always shown in its own order, which is the DJ's work: the columns cannot be sorted there. Folders can be sorted by a column.
- **Drag and drop:** drag the selected rows to a new place in a playlist or pendrive list. A line shows where they will land. Several rows move as one block.
- **Right click:** Move to start, Move up, Move down, Move to end (on the selected rows).
- **Virtual pendrives:** drag a list within its pendrive in the tree to put the lists in order (dropping on the pendrive itself puts it last).
- The order is written at once (`NNN_` prefixes, `Library::moveEntries`); there is no "clear sorting" because the order is never lost.
