# Hardware test results

The Phase 9 gate: does a real player accept a drive exported by trakcrate?

## Result

**Passed.** The user exported a drive from the trakcrate pane and tested it on real hardware; it works fine (reported 2026-10-27).

Not recorded, because the user has not given them: the player model(s), the file system of the stick, and which compatibility switches (if any) were left on.
Add them here when known, because they decide which format guesses in `UNKNOWN_BYTES.md` are now proven.

## Library used

`~/Music/trakcrate` holds three folders: `2013_08_House` (Beatport, loudness normalised) and two folders of the user's own tracks (not normalised).
The Beatport tracks show clearly taller waveforms than the user's own, which is the fixed (never per-track normalised) scale doing its job.

## BPM detection (aubio), same session

- Works, but is not precise (close to, not exactly, the true value).
- The user believes their music was written with integer BPMs, so rounding the detected value should recover it. To be verified once the beat grid can be viewed against the audio in a player view.

## Still open

- Which of the guessed header words / row bytes in `UNKNOWN_BYTES.md` the players actually care about: needs the bisect tests only if a future change breaks a player.
- Waveform height calibration against rekordbox's own output for the same tracks.
- Artwork sizes (240 / 80) and the `_m` copy.

## Cuemark checks (Phase 11, not yet run on a player)

Export one track with a few cuemarks (`Edit cuemarks` in the track editor; `cuemarks.md` has the design). Export it twice, once with the spikes
(the default) and once without (`ExportOptions::cuemarks.enabled = false`), and compare on the deck.

1. A plain cuemark shows as a bright white spike in the detail waveform (about 13 ms wide) and as a thin line in the overview. Note which decks show it.
2. The same drive without spikes shows the true waveform (the master is never modified; the spike is added only in the exported copy).
3. Hot cues A to C: a cuemark with a hot letter appears as a real hot cue pad on the deck (the deck draws it; trakcrate adds no spike for it).
   Check the pads jump to the right place.
4. Quantize: set a cue by hand at a spike with quantize on and off and write down the offset from the cuemark.
5. Memory cue limit: not exported yet (PCOB list type 0 is untested); nothing to check until it is written.
6. The colour preview (PWV4) is deliberately left without spikes. If the overview looks wrong, report it.
7. Optional: green spikes on the colour detail waveform (`CuemarkRenderOptions::green`) to see whether it reads better than white.
