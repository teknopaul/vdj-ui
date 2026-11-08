# Beat grid generation (study)

For each exported track we must produce a beat grid, stored twice in the ANLZ files. Without one, a player shows no BPM, cannot sync and cannot quantize. rekordbox itself writes an empty grid (`len_beats 0`, BPM 0) for tracks it never analysed, so a missing grid is *valid* but useless. This doc covers the encoding (known) and the analysis that finds the beats (not written yet).

## Storage format (known, verified on 2162 real tracks)

Source: `references/libcdj/doc-internal/rekordbox-anlz-format.text`.

| Tag  | File | Content                                                                                           |
|------|------|---------------------------------------------------------------------------------------------------|
| PQTZ | DAT  | per beat 8 bytes: u16 beat number (1..4), u16 tempo (BPM x 100), u32 time in ms. Big endian.      |
| PQT2 | EXT  | header repeats first and last beat, beat count, then one u16 per beat: microseconds after the ms. |

- Both must agree: same count, same first and last beat. Beat numbers cycle 1-4 starting from the first entry's number (the first beat does not have to be a downbeat marked 1).
- **Grid shift (PQTZ header 0x12, s16 ms):** the entries are not the whole story: a player (or rekordbox) that moves the grid start stores the shift in the tag header and the entries stay as analysed, so the real beat time is `entry ms + shift`. Our writer always writes 0. libcdj `anlz_beat_at()` adds it (`shift_ms`) for every reader. PQT2 is not changed, its microsecond fractions belong to the unshifted grid. Details: `doc/deck-edits.md`.
- In the real data PQTZ ms + PQT2 us lie on a straight line within ~0.1 ms, so rekordbox computes an exact constant-tempo grid `t_i = t_0 + i * 60000/bpm` and rounds.
- Header word at PQT2 `0x2c` is different in almost every file (looks like a hash/id). Unknown; try zero, then test on a deck.
- `PVBR` (VBR seek index, 1604 body bytes) is mostly zeros in the samples. Layout unknown.
- Writer: `libcdj/src/anlz/anlz_fake.c` has `anlz_fake_add_pqtz` and `anlz_fake_add_pqt2`, written for tests.
- Variable tempo: PQTZ has a tempo field per beat, PQT2 only one in its header. How rekordbox stores a tempo change is **unverified**, and every real grid has a single tempo. So v1 produces constant-tempo grids only.

## What the exporter needs from analysis

1. **BPM** to two decimals (stored x100). Players show it and sync to it, so accuracy matters: at 128 BPM an error of 0.01 BPM drifts a beat in ~100 minutes, 0.1 BPM in 10.
2. **Phase**: the time of the first beat, in ms plus us. Alignment to the transient to within a few ms.
3. **Downbeat**: which beat is "1". Nice-to-have; the sample data starts with whatever beat comes first.
4. Beats for the *whole* track from the first beat to the end: `n = floor((duration - t0) * bpm / 60000) + 1`. Real grids end within 10 s of the track length.

Real exports also set the first hot cue at the first beat (~4-5 ms), marker 150. That is the cue module's concern, not this one.

## Analysis options

We already decode any file to 16-bit stereo 44.1 kHz (`src/audio/audio_decoder.h`, `decodeAudioFile`), so no new decoder is needed. Nothing in the repo detects tempo yet (`beat_source`/`beat_clock` follow network beats, they do not analyse audio).

| Approach                                           | Notes                                                                                       |
|----------------------------------------------------|---------------------------------------------------------------------------------------------|
| Own code: onset envelope + autocorrelation + comb  | Fits dance music at constant tempo, small (~300 lines), no dependency. Recommended start.   |
| aubio (`aubio_tempo`)                              | Mature, a new dependency, accuracy ~1% unless post-processed. Fine as a cross-check tool.   |
| Essentia / librosa / madmom (offline reference)     | Use to validate our results on the test set, not to ship.                                   |
| Read BPM from file tags (ID3 TBPM)                 | Free when present, often wrong or integer. Use as a prior (octave resolution), not as truth. |

### Proposed algorithm (own code)

1. Mono mix, low-pass (~150-200 Hz emphasises kick) and also a full-band spectral flux; compute an onset strength envelope at ~200 Hz.
2. Autocorrelation of the envelope over lags covering 70-180 BPM. Pick peaks, resolve octave errors (half/double) with a prior: ID3 tag if present, otherwise prefer 90-170, the usual rekordbox range.
3. Refine: fit `t_i = t0 + i*T` by least squares over the detected onset positions near the grid (like the PQT2 straight-line check we already did on real data). Resolution well below one envelope sample.
4. Phase: choose `t0` that maximises the summed onset strength on the grid. Shift to the first beat in the track.
5. Output `bpm_x100`, `t0` in microseconds. Round BPM to 0.01 for the tempo field but keep the unrounded period for computing beat times, as rekordbox appears to.
6. Confidence score. If low (no clear periodicity: ambient, rubato, spoken), emit the **empty grid** the way rekordbox does rather than a wrong one.

Wrong grids are worse than none: a deck will happily sync to a wrong tempo.

## Validation

We have strong ground truth: real tracks with rekordbox grids exist (7-track export, and the ~2200 track corpus when the big pendrive is mounted), though the audio for the 7-track export is missing (the mp3s are listed but not present). On the big pendrive the audio is there.

1. Run our analyser on every track with audio; compare BPM (target: within 0.05, octave errors reported separately) and phase (within 10 ms of rekordbox's first beat).
2. Round trip: write PQTZ+PQT2 from the rekordbox values and check `anlz_track_beatgrid` returns identical beats.
3. Synthetic audio: click tracks at known BPM and phase in the unit tests (snip framework); also the app's own drum patterns (`drum_pattern`) are known-tempo audio.
4. On decks: load an exported track, check the displayed BPM, and that sync/quantize and the beat lights line up by ear.

## Open questions

- Is the first beat before any audio allowed (negative times)? Real files start at ~5 ms.
- Downbeat detection: worth doing, or always label the first beat as 1? Likely a cue/phrase concern.
- Variable tempo and tempo changes: format unverified, need a real sample before any support.
- Whether a different PQT2 `0x2c` value matters to the player.
- Where to put the code: libcdj (C, next to `anlz`) or vdj-ui (C++, next to the decoder). It needs the decoded PCM, so vdj-ui is the natural home; libcdj would keep only the writer.
