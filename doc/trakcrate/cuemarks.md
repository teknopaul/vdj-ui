# cuemarks (study)

A **cuemark** is a trakcrate marker: a named position in a track, unlimited in number, kept in the master database. The players only store a few real cue points, so on export the cuemarks are *drawn into the waveform* as vertical green lines. The DJ sees them on the deck, and can turn any of them into a real cue point on the spot.

## Decision: a white spike is enough

The user has confirmed that a bright white full-height spike is acceptable. So the baseline does not depend on a deck showing colour: every waveform tag gets the white spike (maximum height, maximum whiteness), which works on mono and colour decks alike. Green on the colour tags (`PWV5`, `PWV4`) is an optional extra (`--cuemark-colour green`) to be enabled only if the deck test shows it is visible and looks better. Where this document says "green line", read "white spike, green where supported".

## Is it possible?

Probably yes, with caveats that only a deck test can settle.

1. **Real cues on the drive: the 3 hot cues.** The ANLZ cue tags (`PCOB`/`PCO2`) are already writable (`anlz_fake_add_pcob`, `anlz_fake_add_pco2`) and every real export we have contains hot cues A, B, C. On export, three chosen cuemarks (or none) become those hot cues; the DJ jumps to them and loops from them as usual.
2. **The other cuemarks are only pixels.** The waveform tags are plain per-column bytes (`PWAV`, `PWV2`, `PWV3`, `PWV5`, `PWV4`; see `waveforms.md`). A player draws what the bytes say and has no way to know a column is "special". So a green full-height column at the cuemark position will appear on the deck's waveform exactly where we put it. This follows from the format; it is not yet tried on a player.
3. **Turning a mark into a cue live.** The DJ sees the line coming, and positions the deck on it (pause, search, jog, beat jump, or just let it play and press the hot cue pad as it passes), then stores a hot cue or memory cue there. If **quantize** is on, the player snaps a newly set cue to the nearest beat of the grid, so a cuemark that sits on a beat gives an exact cue. That is why optional beat alignment matters. Without quantize, accuracy is the DJ's timing, within the width of the line.

What we do not know yet:
- **Which waveform the user's decks show.** No longer blocking: the white spike works on every tag. It still decides whether the optional green is worth enabling (colour players only).
- **Whether the preview (overview) waveform keeps the line visible.** `PWAV` has only 400 columns for the whole track; one column is 0.25% of the track (about 0.9 s on a 6 minute track), so the mark there is a coarse hint, but visible.
- **Whether memory cues are an alternative.** The cue tags have a list for memory points (`PCOB` list type 0) which the real software uses, and players step through them with the memory/cue-search buttons. How many the user's decks accept and whether they show on the waveform is untested (no sample on disk has one). If they work, they are a free extra layer for the *most important* marks; cuemarks remain the unlimited layer.
- **Whether a deck reads the cue/waveform data from the drive more strictly than we assume** (checksums are not known to exist in the ANLZ files, but we have not tried modified ones on hardware).

## Data model

One file per track, own file like `vocals.md`:

```
track-master-copy/<folder>/<track>/cuemarks.txt
```

```
# trakcrate cuemarks v1
# beatgrid: 128.00 bpm, first beat 51.972 ms
# beat      ms        label        hot    colour
17.0        7551.6    drop-1       A
33.0        15051.8   breakdown
65.0        30051.4   build        B
97.0        45051.0   drop-2       C
129.0       60051.2   outro-start
```

- `beat`: beat number from the first beat of the grid (1.0 = first beat), decimals allowed. `-` when there is no grid.
- `ms`: the time in ms, 0.1 ms precision. If beat and ms disagree because the grid changed, ms wins and the beat is recomputed.
- `label`: free text, no tabs. Not shown on the deck (no text channel in the waveform), shown in trakcrate and in the CLI.
- `hot`: optionally `A`, `B` or `C`: this cuemark becomes that real hot cue on export. At most three; more is an error at export (`trakcrate check`). With none assigned, the exporter may choose automatically (first, one third and two thirds, or the first three by order; option `--auto-hot`).
- `colour`: reserved (the user's decks do not show cue colours).
- **Beat aligned (optional):** tools offer `--snap beat|bar|phrase|none` when creating a cuemark; stored values are already snapped, a mark that is not on a beat is legal.
- Unlimited count; sorted by position; two marks within 1 column of each other (6.7 ms) are merged in the render.
- Provenance in the header like `vocals.md`: `origin: generated|edited|imported`. Imported rekordbox memory points and hot cues can be converted to cuemarks (`import-from-rekordbox.md`), keeping their `hot` assignment.

## Loops

A cuemark can carry a loop: an extra column `loop_end_ms=<ms>` after the colour column (the rekordbox import and "copy back" write the same column). Other extra columns are kept.

- Allowed lengths are 1/2, 1, 2, 4, 8, 16 and 32 beats. The end is `start + beats x 60000 / bpm` of the constant grid, so the start may be anywhere but the end is a fixed number of beats later.
- Creating a loop, and doubling or halving one, needs a beat grid. Removing a loop and moving or deleting the mark do not. A mark that moves takes its loop along (same length).
- A loop imported from a deck can have another length (say 3 beats). It is kept as it is until it is doubled or halved: the length is first snapped to the nearest allowed one (by ratio, a tie goes to the shorter), then moved one step.
- A loop that would end after the end of the track is refused.
- **Export:** a mark that holds a hot cue letter A-C and has a loop is written as a **loop** hot cue (`PCPT`/`PCP2` loop flag, end time, and for the lengths we make the beat count in `PCP2`); the end is clamped to the track length. A memory point with a loop is not exported (memory points are not exported at all). **Not yet seen on a deck**: the first hardware check is that the pad shows and plays the loop, and that copying it back gives the same `loop_end_ms=`. **VBR mp3, deck test 1 (2026-11-08):** the first export played the loop correctly when called on the deck but did not draw it on the waveform: we had left the loop end's frame and byte offset at 0. After loops were made on the deck for the same file, its ANLZ files (kept in `tests/fixtures/deck-vbr-loops/`) show the layout: the last four words of the `PCPT` entry are pairs, start and end in 1/75 s (`0x772`/`0x8cd` for 25425/30041 ms, exactly `ms x 75 / 1000`), then start and end as byte offsets (`0xd4160`/`0xfc480`). The exporter now fills the end half (`loopBytePos`) and `test_trakcrate_anlz_roundtrip` checks the deck's bytes. The deck's byte offsets are one to three frames away from `Mp3Index::offsetAtMs` (and not from the Xing TOC), so the deck has its own rule; ours is still a real frame boundary. The `PCP2` entry has no byte offsets for a loop (44 byte form, `0008 0001` = 8 beats). Still to be confirmed on the deck: that the re-exported loop is now drawn.
- Code: `cuemark_edit.{h,cpp}` (`setCuemarkLoop`, `scaleCuemarkLoop`, `clearCuemarkLoop`, `cuemarkLoopEnd`, `precedingCuemark`).

## Rendering into the waveforms (export only)

The master copy keeps the **true** waveform. The exporter applies the marks on a copy when it writes the ANLZ files, so changing or removing a cuemark never needs a re-analysis and the master is never "polluted".

For each cuemark at time `t`:

| Tag                               | Columns                                                          | What is written                                                                                                                                                                                              |
|-----------------------------------|------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| PWV5 (colour detail, 150/s)       | column `round(t * 150)`, 2 columns wide by default (about 13 ms) | default: height 31, red 7, green 7, blue 7 (white); optional green: red 0, green 7, blue 0                                                                                                                   |
| PWV4 (colour preview, 1200 total) | column `t / duration * 1200`                                     | max height; the layout of this tag is only partly understood, so copy the byte pattern of the loudest white-looking real column, or leave PWV4 untouched until tested (the PWV3/PWV5/PWAV spikes still show) |
| PWV3 (mono detail, 150/s)         | same columns as PWV5                                             | height 31, whiteness 7 (bright), the mono equivalent of the line                                                                                                                                             |
| PWAV (mono preview, 400)          | column `t / duration * 400`                                      | height 31, whiteness 7                                                                                                                                                                                       |
| PWV2 (tiny preview, 100)          | column `t / duration * 100`                                      | height 15                                                                                                                                                                                                    |

Design points:
- **A line replaces the waveform in that column.** Making it 1 column wide on the detail wave hides almost nothing, and the DJ is not trying to read a transient 6.7 ms wide. 2 columns is easier to see. Option `--cuemark-width N` and a `--cuemark-style` (replace, or only tint the column and keep its height, an alternative that hides nothing but is less visible).
- **Beat grid alignment** matters for the visual too: the player draws beat markers from the grid; a mark exactly on a beat sits on that tick.
- **Different mark kinds** could use different colours later; v1 has one kind of spike (white, optional green on colour tags). Keep a `kind` out of v1.
- **Hot cue marks are not drawn** as green lines (they are real cues, which the deck draws itself), unless `--draw-hot` is set; the default avoids a double marker.
- **Clipping**: marks within the first or last column are clamped.
- The rendering must be deterministic so `rsync` updates only touch changed ANLZ files.

## CLI and GUI

CLI (add to `cli.md`):

```
trakcrate cuemarks TRACK                     list
trakcrate cuemarks TRACK --add 33 --label breakdown --snap bar
trakcrate cuemarks TRACK --add-ms 15051.8
trakcrate cuemarks TRACK --rm 33
trakcrate cuemarks TRACK --hot A=17 B=65 C=97
trakcrate cuemarks --import-from-grid TRACK --every 32      # a mark every 32 beats, a quick start
trakcrate cuemarks --from-vocals TRACK       # a mark at each vocal region start (uses vocals.md)
trakcrate cuemarks --check TRACK...
trakcrate export ... --cuemark-width 2 --cuemark-style replace --cuemark-colour white|green --draw-hot --no-cuemarks
```

GUI (add to `gui.md`): add, drag (snap to beat by default), label and delete marks on the waveform; choose the three hot cues; a preview of how the line will look in the exported mono and colour waveforms; bulk "marks at section changes", using the vocals data or imported phrases.

## Test plan (needs the decks)

1. Export one track with a single white-spike mark; load on each target deck; note whether it shows in the detail wave and the overview. Then try green on PWV5.
2. Same with PWV3/PWAV only (mono).
3. With the three hot cues: confirm A-C jump and loop; test with quantize on and off, set a hot cue by hand at a mark, and measure the offset from the mark.
4. Test memory cues (`PCOB` list type 0) with 3, 10 and 20 entries to learn the player's limit and whether they show on the waveform.
5. Check the deck does not reject, crash on or silently ignore modified waveforms.

## Open questions

- Which tags the user's decks display (only decides whether the optional green is worth it).
- Whether the player has its own limit on cue count in `PCOB`/`PCO2` that makes "unlimited" real cues possible (not assumed here: the user states the limit; the real number is a test item above).
- Per-mark colours/kinds in a later version.
- Whether to also write the cuemarks into a human readable sidecar on the exported drive (a `.txt` the DJ can read on a laptop), harmless to players.
