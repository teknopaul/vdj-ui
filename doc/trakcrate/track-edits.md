# DJ edits and acid loops

Make a new version of a track with parts cut out and parts looped, the way a DJ would do it live with the loop buttons, but prepared in advance so that the joins are clean. The original audio file is never changed: an edit is always a new track.

Code: `src/trakcrate/edit_plan.*` (regions, planning), `edit_render.*` (the audio), `edit_output.*` (files, tags, names), `edit_library.*` (import into the library), `edit_job.*` (the whole job); UI: `src/ui/trakcrate_edit_panel.*`, `trakcrate_edit_preview.*`, `trakcrate_edit_render_dialog.*`, `trakcrate_edit_loop_dialog.*`. Plan: `.planning/plans/track-edits_PLAN.md`.

## Using it

1. Load a track with a beat grid in the track editor and press **Edit track**. The track editor pops out of its dock as a window across the whole screen width, and goes back into the dock when the mode ends. (Without a grid the button refuses; fix the grid first, see `beat-grid.md`.) The edit modes (cuemarks, grid, edit track) exclude each other.
2. Drag on the waveform. The range snaps to the beats, always. Press **Cut** or **Loop**:
   - **Cut** removes the range, rounded to whole bars from a bar line (the beat 1 of the grid). Drawn dark grey.
   - **Loop** repeats the range, rounded to 1, 2, 4, 8 or 16 bars, starting on the nearest beat. Drawn yellow. It is played 4 times by default (2 to 16).
   - Both are drawn as 25% bands with their number. Bars are either cut or looped, never both; overlapping regions and touching cuts are refused with a message (make touching cuts one cut).
3. Select a region (click its band or the list) and set its **click avoidance** below. Every change is undoable (Ctrl+Z); Delete removes the selected region. The regions are kept only while the track stays loaded.
4. The line under the list shows the new length ("Edit: 5:42 -> 4:51") or why the edit cannot be rendered.
5. A click on the waveform (not a drag) sets the blue **play marker**, snapped to the nearest beat; it is a position in the original track. **Space** (while Edit track is on and the focus is anywhere in the editor except a text field; it takes the key from the sequencer's play / pause) or **Preview...** plays the edit from the marker, in the editor itself, no window; Space again stops. A time line (the green playhead) moves across the waveform showing where in the original the edit is, so it jumps back at every loop repeat and over every cut. The edit is rendered the first time (a message says so) and again only after you change it; changing a region while it plays stops the preview. A marker inside a cut starts just after the cut. **Preview region** plays just the selected cut or loop, from 2 bars before it to 2 bars after it. Nothing is saved.
6. **Render...** saves it (below).

## Click avoidance

A cut of `[a,b)` joins the audio before `a` to the audio from `b`; each repeat of a loop joins its end back to its start. Raw sample edits click, because the audio is not at a zero crossing there, so every join has an algorithm:

| algo | parameter | what it does | cuts | loops |
|------|-----------|--------------|------|-------|
| `cut` | fade 5, 10, 20 or 50 ms (default 10) | raw join with just enough fade out before and fade in after | yes | yes |
| `cross` | 1/2, 1 or 2 beats | the beats before the join crossfade (equal power) from the cut audio into the audio before the rejoin point; length and bar alignment are unchanged | yes | yes |
| `fadeout` | 1/2, 1 or 2 beats | the last beats before the join fade to silence, the next part starts hard (5 ms fade in), so the beat drops in with emphasis | yes | yes |
| `tailoverlap` | 0, 1, 2 or 4 beats | like `cut` (5 ms), and the first beats of the removed range (a reverb tail) are played over the start of the beats that come afterwards. 0 is the same as `cut`. It is the DJ's job to choose places where this does not double the kicks | yes | no |
| `earlycut` | 1, 2 or 3 beats, before or after the bar, and `cut` or `cross` for the join | the cut starts a few beats before (or after) the bar line, for example because a vocal starts early. The joined part moves by the same amount, so the track stays in time and the bars stay 4 beats | yes | no |

## Rendering

**Render...** opens the save dialog. Settings are remembered: format, MP3 bitrate, author name and folder.

- **Add to the library, next to the original** (default): the edit becomes a new track in the same master folder as the original. It is analysed (waveform and loudness) so it can be exported to a pendrive; its beat grid is the exact grid of the edit (origin `edited`, not detected). If a playlist exists with exactly the name of that folder and contains the original, the edit is inserted into it just before the original. No such playlist: nothing is created or added, the dialog says so. The info file is kept in the track folder as `edit.txt`; `source.txt` has `edit_of` (the original's track folder).
- **Save a file only**: the audio and a `.txt` go to a folder. It defaults to the folder the original was imported from (`imported_from` in `source.txt`), else Music, else home.
- **Format**: WAV (16 bit) or MP3 at a constant bitrate (128, 192, 256 or 320 kbps). Nothing is ever overwritten; clashes get `_2`, `_3`.
- **Name**: `<original file name>_<user>_edit.<ext>`, with `<user>` lower case.
- **Tags**: copied from the original. A trailing "(Original Mix)" is removed from the title and "(<User> Edit)" appended; the artist is not changed. MP3 gets ID3v2.3 with `TBPM`; WAV gets the tempo in the RIFF `acid` chunk and a LIST/INFO chunk. Artwork is not copied into the audio file (a library edit has no artwork of its own).
- **Info file** (`.txt` / `edit.txt`): output name, source file, author, ISO time stamp, source and output tempo and first beat, duration, and one line per region ("#1 cut bars 9-12 algo=cross 1 beat").

The whole original is decoded into memory and kept (with the render) while the track stays loaded in the editor, so a render after a preview does not decode again. Cuemarks of the original are not carried over to the edit.

## Acid loops

Cut a loop out of the track as a sample for Acid. Select a loop region, or drag a range, and press **Export loop...** (a range gives 4 beats from its nearest beat; the length can be 4, 8, 16, 32 or 64 beats in the dialog).

- Asks for a **name**; the file is `<original file name>_<name>.wav|mp3`, the title `<title> (<name> <User> Edit)`.
- Click avoidance `cut` (default 10 ms), `cross` or `fadeout`, applied inside the sample so that it repeats cleanly end to end. Its length is exactly the loop's.
- The tempo is in the file (acid chunk / `TBPM`). Tags come from the original.
- **Preview** plays the loop repeating 8 times.
- The folder you choose is remembered and is the default from then on (first default: where the original was imported from). Loops are not imported into the library and not put in playlists.

## Not done

Variable tempo grids, non 4/4 bars, saving the region list (it can only be rebuilt by hand from the `.txt`), carrying cuemarks into an edit, and artwork in the output audio file.

## Fade lengths worth knowing

The fades that are not configurable are all 5 ms: the fade in after a `fadeout`, both fades of `tailoverlap` (its fade out only when the tail is 0 beats), and the fallback of `cross` when there is no audio before the join (a loop at the very start). The configurable `cut` fade is applied from the join onwards, so a long one softens the first kick after a cut: use 5 ms there if the weight of that kick matters. Moving the fade so that it ends on the beat is a possible later change.
