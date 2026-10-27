# Vocal analysis (study)

Goal: for each track, find where vocals are present, and store the result in its own file so it is easy to inspect, correct by hand and replace with a better analyser later. Useful to a DJ for mixing: don't overlay two vocals, find instrumental intros and outros to blend into, and see vocal sections on the waveform.

This is **a trakcrate feature only**. Players do not read it. See "Relationship to rekordbox data" for the one place the real format may help.

## Separate file, own format

The analysis is not mixed into `meta.txt` or the ANLZ data. One file per track in the track folder (`trakcrate-db-format.md`):

```
track-master-copy/<folder>/<track>/
├── track.mp3
├── meta.txt
├── beatgrid.txt
└── vocals.txt            this feature
```

Reasons for a separate file: users can replace it with their own or a better tool's output; it is optional (no file means "not analysed", an empty list means "analysed, no vocals"); it is not part of the rekordbox export; and its provenance is its own (a hand-edited file must never be overwritten by a re-run).

## File format

Plain text, one region per line, whitespace separated, `#` comments, so it works with `awk`, `sort` and any editor. Positions are given **both in beats and in time**, because the beat grid is the DJ's frame of reference but time is what is audible if the grid is later corrected.

```
# trakcrate vocals v1
# analyser: trakcrate-vocals 0.1 (energy-band heuristic)
# origin: generated            # generated | edited | imported
# beatgrid: 128.00 bpm, first beat 51.972 ms
# confidence: 0.71             # whole-track confidence, 0..1
#
# start_beat  end_beat  start_ms  end_ms  kind    confidence
33.0          65.0      15210.5   30211.2 vocal   0.84
97.0          129.0     45212.0   60213.0 vocal   0.77
129.0         145.5     60213.0   68000.0 hook    0.40
```

Fields:
- `start_beat`, `end_beat`: beat numbers counted **from the first beat of the grid, starting at 1.0**, as decimals, so a vocal that enters off the beat is representable (e.g. `33.5`). The region is `[start, end)`.
- `start_ms`, `end_ms`: the same points as time from the start of the audio, to 0.1 ms. These are the truth if the two disagree after the grid changes; tools recompute beats from times when the grid's origin or BPM changed (the header line `beatgrid:` records the grid the beats were computed against).
- `kind`: `vocal` (default), optional finer labels the analyser or user may use: `hook`, `spoken`, `adlib`, `chop`. Unknown kinds must be preserved by readers.
- `confidence`: 0-1 per region; hand-written lines may use `1`.
- Regions are sorted by start and must not overlap (merge instead). Gaps between regions are the instrumental sections.
- UTF-8, LF line endings, readers ignore unknown comment headers and tolerate extra trailing columns.

Alignment: analysers detect vocal presence in time, then **snap start and end to the nearest beat (optionally bar or phrase boundary) of the beat grid** when within a tolerance (default half a beat), keeping the unsnapped time in the `_ms` columns. Both are stored so users can see the snapping. A `--no-snap` option keeps raw times.

If a track has no usable grid (not analysed), the beat columns are written as `-` and only times are stored.

## Analysis approaches

Nothing exists in the repo. The decoder (`src/audio/audio_decoder.h`, 44.1 kHz stereo) provides the audio.

| Approach                                           | Accuracy                                  | Cost / dependency                                    |
|----------------------------------------------------|-------------------------------------------|------------------------------------------------------|
| Heuristics: mid-side centre-channel energy, 200 Hz-4 kHz band energy, spectral flux / harmonicity | Poor to fair; fooled by leads, synths, snares; works for clean acapella-style vocals | Small, no dependency; good first version |
| Source separation then energy gate (Demucs, Open-Unmix, Spleeter-class models) | Good; the vocal stem makes presence detection easy | Large models, ONNX runtime or similar; slow on CPU; licence of model weights to check |
| Trained voice-activity / singing-voice detector (small CNN on mel spectrograms, e.g. published SVD models) | Good, fast | Needs a model file and runtime; check licences |

Recommendation: ship the heuristic detector first (as the baseline, flagged with its low confidence), define the file format now, and let better analysers plug in through a single interface: `analyser(audio, beatgrid) -> list of regions`. External tools can be used via the CLI: `trakcrate vocals TRACK --analyser CMD` runs an external command that prints the region lines and imports the output, so users can script their own (a Python separation script, for example) without touching our code.

Post-processing (all analysers): smooth the per-frame decision (hysteresis, minimum region length of about 1 bar, minimum gap of about 1 bar), merge near regions, drop weak ones below `--min-confidence`.

## CLI (additions to `cli.md`)

```
trakcrate vocals TRACK...            analyse (skips edited files unless --force)
    --analyser NAME|CMD              builtin heuristic (default) or an external command
    --snap beat|bar|phrase|none      default beat
    --min-confidence F               default 0.5
    --min-length BARS                default 1
    --print                          write to stdout, not to vocals.txt
trakcrate vocals --show TRACK        human readable table
trakcrate vocals --check FILE        validate a vocals.txt (sorted, no overlap, within track length)
trakcrate ls --where 'vocals=none'   tracks without a vocals file
```

`trakcrate analyse` includes it with `--only vocals` selectable. Exit status as in the other commands.

## GUI (additions to `gui.md`)

- Draw the vocal regions as a coloured band over the waveform in the track details and the waveform window, next to the beat grid.
- Edit regions by dragging the edges (snapping to beats by default, Alt for free), add and delete regions, mark a region's kind.
- A "re-analyse" action that keeps manual regions (merge by marking edited regions as locked).
- Table column and filter: "has vocals", "instrumental intro" (first vocal start beyond bar 8, say), "instrumental outro".
- An "intro/outro without vocals" summary in the details pane: bars of vocal-free intro and outro, the info a DJ wants for a transition.

## Validation

- Ground truth: hand-labelled tracks. Start with a small set (10-20) of the user's own tracks plus tracks with published stems or acapellas, labelled to the nearest beat. Metric: frame-level precision/recall and mean start/end error in beats.
- Self-consistency tests on synthetic audio: a click track plus a sung/spoken sample inserted at known beats.
- Format tests: parse, round trip, tolerate comments and extra columns, overlap/sort rejection.

## Relationship to rekordbox data

Newer rekordbox analysis (phrase data, the `PSSI` tag in the ANLZ files, for the players that support phrase analysis) classifies sections as intro/verse/chorus/outro and, in its "high/mid/low" modes, hints at how vocal/energetic a section is. The repo has the spec but **no real sample** (see the anlz notes), so we cannot use it yet. When a rekordbox pendrive with PSSI arrives, `import-from-rekordbox.md` should import the phrases into a separate file (not into `vocals.txt`; phrases are not vocal detection), and the vocal analyser may use them as a prior.

## Open questions

- Do we want per-stem output later (store a separate vocal stem audio file)? Out of scope for v1.
- Should `end_beat` be allowed to exceed the grid (a vocal running past the last grid beat)? Allow it, extrapolating the grid.
- Variable-tempo tracks: beats can't be derived from a constant BPM; for such tracks use time columns only until the beat grid format supports tempo changes (`beat-grid.md`).
- Licence and size of any bundled model weights (needs a decision before adding a dependency).
