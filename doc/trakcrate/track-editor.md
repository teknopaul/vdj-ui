# trakcrate per-track editor (requirements)

Edit one track while it is loaded in the player. The library-wide pane (`gui.md`) manages many tracks; this editor is about the **single loaded track**, with the audio and the waveform in front of you so changes can be checked by ear. It edits the same files in the trakcrate master (`trakcrate-db-format.md`) and changes it by running `trakcrate` commands (`cli.md`, `gui.md`: GUI and CLI are separate); no trakcrate library is linked.

## Loading a trakcrate track into the player

- Drag a track from the trakcrate pane (or double click, or a context menu "Load into deck N") onto a player; `PlayerPanel::loadTrack(const TrackInfo&)` is the existing entry point and should accept a trakcrate track by path (the master copy's audio file) plus its folder.
- The player gets the master's analysis, not the rekordbox ANLZ: beat grid from `beatgrid.txt`, cuemarks from `cuemarks.txt`, waveform from the stored waveform data (`waveforms.md`), artwork from `artwork.jpg`. Tracks imported from a real rekordbox drive load their kept ANLZ data via the existing `track_analysis` path.
- The deck knows it holds a trakcrate track (its folder), so the editor widgets are enabled only then; tracks from pendrives or the network stay read-only.
- Several decks can hold tracks from the library at once; each deck has its own editor state.
- Loading must never modify the track. Writes happen only on explicit edits (below) and are saved immediately to the files, atomic via temp and rename, so a crash loses at most the edit in progress.

## First version: what is editable

| Function                | How                                                                                                                      |
|-------------------------|--------------------------------------------------------------------------------------------------------------------------|
| Nudge the beat grid     | buttons/keys and mouse drag on the waveform (see below)                                                                  |
| Insert a cuemark        | mouse on the waveform                                                                                                    |
| Delete a cuemark        | mouse on the waveform                                                                                                    |
| Rating 0-5 stars        | click on a star row in the track view                                                                                    |
| Comment                 | text field in the track view                                                                                             |
| Artwork                 | shown in the track view; "Edit in..." opens a graphical editor                                                            |

Not in the first version: editing hot cues and loops, BPM entry, key, full metadata form, vocals regions. They follow in later phases (`gui.md` lists them).

## Beat grid nudging

The grid is a constant-tempo line `t_i = t0 + i * 60000/bpm` (`beat-grid.md`). Nudging changes `t0` (and later the BPM).

- **Offset nudge**: buttons `<` and `>` shift the whole grid by a step; keys left/right do the same when the waveform has focus. Step sizes: coarse 10 ms, normal 1 ms, fine 0.1 ms (shift/ctrl modifiers), and a user-set step. Held key repeats.
- **Drag nudge**: dragging the grid on the waveform (an explicit "grid" drag mode, because plain drag already pans in `WaveformWidget`) moves it by the pixel delta converted to time at the current zoom.
- **Snap to a transient**: "set beat 1 here": the user clicks a position (or presses a key at the playhead) and the grid is shifted so the nearest beat lands there; optionally refine to the nearest audio onset.
- **Tempo adjustments** (second step, same controls): BPM +/- 0.01, double/halve, tap tempo; the grid is anchored at a beat the user picks so the far end moves, not the start.
- Visual feedback: the existing overlay draws beats white and bars red (`WaveformWidget`); during a nudge show the numeric offset (`+3.4 ms`) and keep playing, so the DJ hears the click/metronome against the beat (an optional click track mode helps check by ear).
- A "revert" restores the stored grid from before the session; undo/redo for every nudge step (`QUndoStack`, merging consecutive nudges into one command).
- Writing: `beatgrid.txt` is rewritten with `origin: edited`, and downstream data that depends on the grid is marked stale or recomputed (cuemark beat columns, vocal regions' beat columns; both store ms as the truth, `vocals.md`, `cuemarks.md`). A grid whose origin is `rekordbox` shows a confirmation the first time ("this grid came from rekordbox, edit anyway?").

## Cuemarks with the mouse

On the waveform (details view and the popup `WaveformWindow`):

- **Insert**: double click, or right click "Add cuemark here", or key `M` at the playhead. The position **snaps to the nearest beat by default** (hold Alt for free time, setting `snap beat|bar|none` in the toolbar), and a snap guide line shows where it will land before the click.
- **Move**: drag a mark; shift snaps to bars; the time is displayed while dragging.
- **Delete**: right click "Delete", or click a mark and press Delete; confirmed only for undo-less situations (undo is always available, so no prompt).
- **Label**: double click a mark to type a label (optional).
- **Hot cue assignment** (A, B, C) via context menu on a mark (the three the decks can really store; at most one mark per letter).
- Marks are drawn in the editor as green vertical lines (the exported rendering is a white spike, `cuemarks.md`; the editor can show both: line, plus a hint of "export look" in a preview toggle).
- Hit testing: a mark is grabbed within a few pixels; marks too close at the current zoom are listed in a small popup menu on click.
- The existing interactions (wheel zoom, drag pan, hover readout, double click fits track) are kept; cuemark editing uses an edit-mode toggle in the toolbar so a double click means one thing at a time.
- Audition: clicking a mark in the list jumps the deck's playhead to it.
- Every change saves to `cuemarks.txt` (origin `edited`), through the undo stack.

## Track view: rating and comment

- **Rating**: five stars in the track view, click star N to set N, click the same star again to clear to 0; hover shows a preview; keys `0`-`5` when focused. Stored in `meta.txt` (`rating: 0..5`). This matches the rating field of the rekordbox track row (`pdb_track_t.rating`, 0-5), so export maps directly.
- **Comment**: a single-line field (multi-line allowed, shown collapsed), saved on Enter/focus loss to `meta.txt` (`comment:`), and exported to the rekordbox track row comment string. The user's decks do not show cue comments, but track comments are shown by players; verify.
- Both write immediately, mark the field group `edited` (`source.txt`), and are undoable.
- Also shown read-only in the track view: title, artist, album, BPM, key, duration, loudness, origin of each field group.

## Artwork

- Shown in the track view at a moderate size (about 160 px, click to enlarge into a popup), from `artwork.jpg`; a placeholder when there is none.
- Buttons: **Open in editor**, **Replace...** (file picker, also drag and drop of an image file onto the artwork), **Remove**, **Re-extract from tag**.
- **Open in a native graphical editor** (Ubuntu / freedesktop integration):
  - The default is "the image editor the desktop knows": use `xdg-open`-style handling for `image/jpeg` via `QDesktopServices::openUrl(QUrl::fromLocalFile(...))`, which launches the user's default for the MIME type (often an image *viewer*, not an editor).
  - Because viewers are not editors, the menu is built from the desktop database: list installed applications that handle `image/jpeg` / `image/png` (parse `/usr/share/applications/*.desktop` and `~/.local/share/applications/`, using `MimeType=` and `Exec=`; Qt does not expose this list, `gio` does: `gio mime image/jpeg` or the `QMimeDatabase` plus a `.desktop` scan). Typical entries: GIMP, Krita, Inkscape, Pinta, Shotwell, gThumb, darktable. Not limited to GIMP; the user's list simply shows what is installed.
  - Remember the chosen editor per user (config key `artwork_editor`, a `.desktop` file id) and allow a custom command (`gimp %f` style, executed without a shell, with the file path as an argument, never via string concatenation into a shell).
  - Launch with the file path of `artwork.jpg` (an editor that saves in place changes the master file directly; editors like GIMP export, not "save", to JPEG: "Overwrite artwork.jpg" in File menu; mention this in the UI hint).
  - **Detect saved changes**: watch the file with `QFileSystemWatcher` (plus a modification time poll, since some editors save via rename), then reload the artwork in the view and mark artwork `edited`. Editors that write a temporary file and rename it replace the watched inode; re-add the watch after each event.
  - Work on a **copy**? Editing the master in place is the simplest and what the user asked for ("editable on the file system", `artwork.md`). Keep the previous image in `.trakcrate-history/` (one level of backup) so a bad save is recoverable.
  - If flatpak or snap editors are used, the file must be reachable by the sandbox (home directory is normally accessible; warn if the library lives elsewhere).
  - Failure cases: no editor installed (show how to install: `sudo apt install gimp`), the launch fails (show the command output in the message line).
- Image rules at export are in `artwork.md` (the exporter re-encodes, so editors can save as PNG or large JPEG).

## UI placement

- A **track editor panel** attached to each deck (collapsible area under the deck's waveform in `DeckWidget`/`PlayerPanel`), or a dock "Track" showing the track of the currently selected deck; start with one dock that follows the selected deck, to avoid crowding the busy deck area.
- Toolbar on top of the waveform: edit mode toggle, snap selector, nudge step, grid nudge buttons, undo/redo.
- Contents: artwork, rating stars, comment, read-only facts, cuemark list (position, label, hot cue letter).
- New files under `src/ui/`: `trakcrate_track_editor` (the panel), `star_rating_widget`, `artwork_view`, `artwork_launcher` (desktop file scan and launch, no UI), plus changes to `waveform_widget` for the edit mode (grid drag, mark hit testing, signals `gridNudged(double ms)`, `cuemarkAdded(double ms)`, `cuemarkMoved`, `cuemarkRemoved`).
- Writes go through `trakcrate` commands (`set`, `cuemarks`, `beatgrid`, `artwork`), reads of the plain files are done directly for display. The panel only calls commands and shows results.

## Concurrency and safety

- The track files are plain files: if the file changes on disk (the user edits `meta.txt`, or the CLI re-analyses) while loaded, reload with a "changed on disk" notice; if the editor has unsaved text (comment being typed), ask before overwriting.
- Use the library lock for writes (`cli.md`); if busy show "library busy, retry".
- Edits never touch the audio file or the real pendrive.

## Phasing

1. Load a trakcrate track into the deck with its grid, cuemarks and artwork, read-only.
2. Rating and comment (smallest write path; proves the files, atomic writes and undo).
3. Cuemark insert/move/delete with the mouse.
4. Beat grid nudge (buttons and keys, then drag, then snap-to-transient).
5. Artwork view and "open in editor" with file watching.
6. Later: hot cues and loops, BPM editing, tap tempo, metadata form, vocal regions.

## Open questions

- Should a grid nudge keep playing audio aligned (the deck's quantize and sync use the grid of the loaded track)? The deck should switch to the new grid immediately; check how `beat_source`/`deck_phase` hold the grid.
- Where the player gets the waveform from when the track has no stored one (compute on load in a background job, `waveforms.md`).
- Whether comment is single line only (the decks show one line).
- Keyboard shortcuts conflicting with the deck controls and MIDI mapping (`keyboard_shortcuts`); the editor should use shortcuts only when it has focus.
- Rating scale mapping to the rekordbox field and to ID3 POPM if we ever write tags back to the audio (not planned: the master audio file is not modified).

## Implemented (phases 11 and 12)

- **Cuemarks:** `Edit cuemarks` in the track editor. Double click adds (snapped to a beat or bar, Alt = free), drag moves, right click offers Delete and Make hot cue A/B/C, `M` adds at the play position, Delete removes the selected mark, double click a row in the list to name it. Writes `cuemarks.txt` through the undo stack. Code: `src/trakcrate/cuemark_edit.*`, `src/ui/waveform_widget.*`.
- **Export:** `src/trakcrate/cuemark_render.*` draws the white spike into PWAV/PWV2/PWV3/PWV5 of the exported copy only (PWV4 is left alone; hot cue marks are real cues and get no spike). `ExportOptions::cuemarks` has the width, the green option and an off switch; there is no GUI control for them yet.
- **Beat grid:** `Edit grid` mode: drag the waveform, Left/Right (1 ms, Shift 10 ms, Ctrl 0.1 ms), `B` or right click "Set beat 1 here", buttons -10/-1/-0.1/+0.1/+1/+10, a readout of the offset, Revert. Consecutive nudges merge into one undo step. A grid with origin `rekordbox` asks once per session. Cuemark and vocal beat columns are recomputed from their ms. A deck that has the track loaded reloads the new grid at once (`PlayerPanel::reloadTrakcrateAnalysis`). Code: `src/trakcrate/grid_edit.*`.
- Not done: tempo (BPM) nudging, tap tempo, snap to an audio onset, a click track for checking by ear, the preview of the exported look.

## Implemented (phase 13)

- **Artwork:** the editor shows `ArtworkView` (`src/ui/artwork_view.*`): click to enlarge, Replace... (picker or drop a file), Remove, Re-extract (from the audio tag), Restore (the one level backup in `.trakcrate-history/`), Open in editor (menu of the editors found in the `.desktop` files for `image/jpeg`, plus a custom command with `%f`, run without a shell; the last choice is remembered under `trakcrate/artwork_editor`).
- A change made by an external editor (in place or by renaming a temporary file over it) is noticed by a file watch plus a 1.5 s poll; the picture reloads and the artwork group is marked `edited`.
- Qt-free operations are in `src/trakcrate/artwork_edit.*`; `.desktop` parsing and command building are in `src/ui/artwork_launcher.*`.
- Not tried: a real editor, and flatpak/snap editors (the library must be reachable by the sandbox).
