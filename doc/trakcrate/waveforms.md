# Waveform generation (study)

Each track's ANLZ files carry several pre-rendered waveforms that players draw while browsing and playing. We have to render them ourselves from audio. The formats are known; the exact look of rekordbox's rendering is not. Earlier requirement (`ai-prompts/generating-waveforms.prompt.md`): similar to rekordbox, not identical, tuned visually later.

## Formats (from `rekordbox-anlz-format.text`)

| Tag  | File | Entries                 | Entry layout                                                                  | Status               |
|------|------|-------------------------|-------------------------------------------------------------------------------|----------------------|
| PWAV | DAT  | 400, whole track         | 1 byte: low 5 bits height 0-31, high 3 bits whiteness 0-7                     | verified             |
| PWV2 | DAT  | 100, whole track         | 1 byte: low 4 bits height 0-15                                                | verified             |
| PWV3 | EXT  | 150 per second           | 1 byte, same as PWAV (the scrolling detail used while a track plays)          | verified             |
| PWV5 | EXT  | 150 per second           | u16 big endian: bits 15-13 red, 12-10 green, 9-7 blue (0-7), 6-2 height (0-31) | verified             |
| PWV4 | EXT  | 1200, whole track        | 6 bytes (0-127 / 0-255 values); only roughly understood                        | **raw bytes only**   |
| PWV6 | 2EX  | 1200, whole track        | 3 bytes mid/high/low                                                          | spec only, no sample |
| PWV7 | 2EX/EXT | 150 per second        | 3 bytes mid/high/low                                                          | spec only, no sample |

Which ones the user's decks (XDJ-700 and similar, NXS era) use: PWAV/PWV2 for the DAT, PWV3 for the monochrome detail and PWV5/PWV4 for colour on later players. The 3-band PWV6/PWV7 are for newer hardware (2EX files) that we cannot test or sample, so skip them.

Sizing: PWV3 and PWV5 grow with duration: a 6 minute track is 54,000 entries (54 KB / 108 KB). Header checks: `len_tag = 0x18 + len_entries * entry_bytes`; PWAV 420 = 12 + 8 + 400; PWV2 120; PWV4 7224 = 0x18 + 1200 * 6.

## What to render

All of them come from one pass over the audio.

1. Decode with `decodeAudioFile` (44.1 kHz stereo 16-bit).
2. Build a base series at **150 columns per second** (294 samples per column). For each column compute:
   - **height**: peak (or RMS blend) of the mono mix, mapped to 0-31 with one **fixed** curve for every track. **Never normalise per track**: rekordbox does not, and a DJ must be able to see from the waveform that a track's audio is quiet or not normalised (levels differ between tracks). A quiet track must render low, a hot track must reach the top. Use the same full-scale reference for all tracks (0 dBFS = 31).
   - **colour / whiteness**: from spectral balance. In real PWV5 data loud sections come out red and quiet ones blue/cyan; rekordbox's own scheme is approximately "low frequency energy = red-ish, high = blue-ish/white". We can compute three band energies (e.g. <200 Hz, 200-2k, >2k) and map to 3-bit RGB.
3. PWV3 = heights of the base series, whiteness bits from the high band.
4. PWV5 = base series with colour bits.
5. PWAV = 400 columns by max (or averaged peak) over equal slices of the base series; PWV2 = 100 columns, 4 bit.
6. PWV4 = 1200 columns; copy the empirical structure (byte 0 height 0-127, byte 1 about 255 minus byte 0, bytes 2-4 colour related). Less certain, so either mimic or omit this tag; we need to check which tags a deck requires.

Exact width: real files have `len_entries / 150` within 1.5 s of the DB duration, so use `ceil(duration * 150)`.

## Using existing code

- `src/ui/waveform_widget.cpp` and `waveform_delegate.cpp` already **read** PWV2/PWV3/PWV5 from ANLZ and draw them. We can use them as the viewer for our own output, which is the quickest visual tuning loop: render ours and a real one side by side.
- No renderer exists. `audio_player`/`audio_recorder` may keep a peak display; check before duplicating.
- The ANLZ writer helper `anlz_fake_add_wave(b, fourcc, entry_bytes, count_field, ...)` can emit these tags.
- Colours match the "PWV5 colour" model the details pane draws, so we get the same interpretation for free.

## Validation

1. Build the tags for tracks that also have a rekordbox waveform (big pendrive, with audio), then compare per-column correlation of heights (target: corr > 0.95 as seen between PWV3 and PWV5 in rekordbox's own data, ~0.98).
2. Compare overall loudness mapping: histogram of heights per track against rekordbox's. This is a hard requirement, not a tuning detail: our quiet/loud tracks must keep the same relative height as in rekordbox's; calibrate the fixed curve (dB to 0-31) against the real corpus.
3. Visual check: the details pane and the pop-up `WaveformWindow` for both, plus the deck screens.
4. Structural: parse our output with `anlz_open` and the existing tests; byte length equals header formulas.
5. Deck: browse the drive (PWAV/PWV2 appear in lists), then load a track (PWV3/PWV5 scroll).

## Open questions

- The exact fixed curve (linear amplitude vs dB, peak vs RMS) to match rekordbox; per-track normalisation is ruled out.
- Meaning of PWAV whiteness bits (3 bits) and of the unused low 2 bits of PWV5.
- PWV4 entry layout; also PVBR/PVB2 (see beat-grid.md).
- Are PWV4/PWV5 optional for decks that do not show colour? Determine by testing without them.
- Speed: a 6 minute stereo track is ~16M samples; one pass is quick (< 1 s), the decode dominates. Batch exports of hundreds of tracks should use a thread pool.
