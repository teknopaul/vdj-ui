# Deck edits: copy back to trakcrate

Beat grid and cue point edits made on a deck can be copied back into the trakcrate library from the rekordbox viewer.
Plan: `.planning/plans/deck-edits-copy-back_PLAN.md` (see its REVISION section).

## Using it (human test in vdj-ui)

1. Export a playlist with trakcrate, play it on a deck, adjust the grid / set cues on the deck.
2. Open the deck's database in the rekordbox viewer (or the pendrive folder).
3. Deck over the network: the viewer mirrors files into `~/.cache/vdj-ui/xdj/` and only re-reads a file when its size changed, so a moved cue is
   not seen. Right click the **top item** of the tree and choose **Refresh from deck** (only shown for a deck). It re-reads `export.pdb` and drops the
   cached analysis files (`PIONEER/USBANLZ`); the audio is kept.
4. Right click a **playlist** (all its tracks) or a **track** (the selected tracks): **Copy back beatgrid** / **Copy back cue points**.
5. A preview lists, per track, what changes (BPM and first beat shift, or cues added / moved / removed), tracks that are not in the library, and
   ambiguous ones (skipped). Ok writes.

## What is written

Per matched library track, only `beatgrid.txt` and/or `cuemarks.txt`, plus `source.txt` (`origin.beatgrid` / `origin.cues` = `edited`,
`beatgrid_from` / `cues_from` = deck and time). Before that the track folder is snapshotted to `<track>/.trakcrate-history/<time>-deck-copy-back/`
(restore with the scripts' restore). Nothing is written when there is no difference. Metadata, artwork, audio and waveforms are never touched.

- Beat grid: trakcrate keeps one constant tempo, so a variable-tempo deck grid keeps its first tempo. The `beat` column of the cue marks is recomputed.
- Cue points: the library's cue list is **replaced** by the deck's (hot cues A-C become hot A-C, other hot cues and memory cues are marks with a
  label, loops keep `loop_end_ms`). 
- Matching a deck track to a library track: title + artist + file name, then title + artist, then file name, then title alone when the deck row has no
  artist. More than one hit = ambiguous, skipped. There is no audio hash match: the deck's audio is not mirrored. Tracks not exported by trakcrate may
  not match.

## Where a deck saves a moved beat grid (found with a before/after copy of a whole stick)

The deck does not touch the beat entries. Moving the grid start writes a **signed 16 bit shift in milliseconds into the PQTZ tag header of the `.DAT`,
at offset 0x12** (0 in an export). Test track: grid start 29 ms -> moved onto hot cue A at 443 ms gave `+414` (0x019e) and the cue moved 453 -> 443 in
PCOB/PCO2; nothing else changed on the stick except the two pdb sequence bytes. Effective beat time = entry time + shift. The PQT2 tag of the `.EXT`
is not changed, and its microsecond fractions belong to the unshifted grid (they are dropped for a shifted grid). The sign for a move to an earlier
position is assumed (two's complement), not yet seen on a deck.

libcdj's `anlz_beat_at()` does not know the shift: `src/trakcrate/pqtz_offset.h` reads it, and the viewer's waveform (`track_analysis.cpp`), the copy
back (`deck_edits.cpp`) and the drive import apply it. When libcdj learns it, drop the helper or the shift is applied twice.
Fixture: `tests/fixtures/deck-grid-move/`.

## Not done / unknown

- Whether and where each deck model saves its edits on the stick is only known from the first test (the database the viewer shows changes).
  Record models and findings here after more human tests.
- No "export map" yet (exporter remembering which pdb id is which library track).
