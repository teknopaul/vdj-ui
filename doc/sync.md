# Sync

How the VDJ's ALSA sequencer (and the DJ Tools) follow a deck. The measurements behind it are in `doc/sync-timing.md`.

## Modes and the Sync button

Sync is a *mode*, not an action. It stays on until you press the button again.

- **Sync on a deck row** follows that deck, whoever is master.
- **Sync on the VDJ row** follows whichever deck is master, and follows the next one when the master changes.
- Pressing the button of the thing being followed, or the VDJ button while any Sync is on, turns Sync off.
- If the VDJ becomes master, following is suspended (the button shows **Master**) so the BPM is yours: ramp it, move the slider.
  When another deck becomes master the VDJ follows it again (the chosen deck again, in deck mode).
- A master change never moves the BPM or the position. The offset to the new reference is measured while the nudges are held
  ("holding, measuring the new offset...") and becomes the new target. The BPM then glides to the new reference's at 0.1 BPM per beat.

| Button    | Meaning                                                              |
|-----------|----------------------------------------------------------------------|
| Sync      | off                                                                  |
| Waiting   | on, following the master, but there is none yet                      |
| Syncing...| bringing the VDJ in time (aligning, or measuring after a master change) |
| Locked    | in time; the difflock trims small errors                             |
| Master    | on, but the VDJ is master so it follows nobody                       |

**Copy BPM** copies the BPM only. Sync copies the BPM and the beat.

## Beat alignment (first press)

Measures the deck's offset over 4 beats, then slews the sequencer onto the beat plus your target in steps of at most 40 ms,
one per deck beat (`AlsaClock::phaseShiftMs`), always the short way round (at most half a beat). The nudges wait until the
offset has settled for 5 beats. `SyncAligner::kStepMs` is the step size.

## Offsets

- **Target (difflock)**: ms between the deck's beat and the sequencer's, set in the spinner, by Capture, or by the nudge buttons.
  This is where kick-drum attack versus sample start is compensated by ear. It moves the MIDI clock and the DJ Tools together.
- **Audio latency** (DJ Tools panel, `dj/djtoolz_latency_ms`, saved): the audio stack's delay. Positive = slow, the sounds are played
  earlier. Only the DJ Tools sound moves; the MIDI clock does not.

## Correction

- Steady deck: every beat is measured; on the last beat of the bar the mean of the last 4 is compared with the target and the
  sequencer nudged by at most 20 ms. Nudges of 1 to 2 ms every 10 to 15 s are normal; they absorb the deck's real tempo error (about 20 to 200 ppm).
- Deck BPM changing (the reported BPM moved in the last 8 beats, `BpmChangeDetector`): corrected on every beat, half of the mean of the last two
  beats, at most 5 ms. The numbers come from the fader runs of 2026-11-01: without it the offset sat 3 to 4 ms high for several beats during a rise.

## Options

- **Calculated BPM** (`dj/sync_calculated_bpm`, off): follow the BPM measured from the beat packets instead of the reported one.
  Telemetry of the same tracks (143.9 and 140 BPM) showed no benefit: more tempo retunes (14 to 21 in 190 s on a steady deck) and a larger
  offset spread on the steady 140 BPM deck (sd 2.0 ms and 28 nudges in 58 s, against sd about 1 ms and 3 to 7 nudges per 20 s reported). It falls back to the reported BPM
  until 8 beats are measured and while the two differ by more than 0.5 BPM, which is what protects a fader move.
- **Sync: use Quantised Beat Phase (experimental)** (`dj/sync_quantised_phase`, off): feeds the difflock the `BeatPhase` estimate instead of the raw packet arrival.

## Not built

- **Drift trim** (a tiny ALSA tempo trim from the slope of the offset) and a **"micro-BPM on nudge"** heuristic. The telemetry does not need them: no slow drift in
  3 minutes at steady BPM, nudges of 1 to 2 ms, and both modes sounded right. A trim needs a tempo estimate good to 10 ppm, which is 300 beats at an unchanged BPM
  (`doc/sync-timing.md` section 5). Revisit if a long steady track shows the nudges becoming frequent or audible.

## Telemetry (View > Log Sync Telemetry (CSV), `~/.cache/vdj-ui/sync-telemetry-*.csv`)

Columns: `steady_ms, kind, pid, bar_pos, reported_bpm, measured_bpm, alsa_bpm, diff_ms, avg_diff_ms, target_ms, nudge_ms, master_pid, sync_state,
note, track_bpm_raw, pitch_raw, pitch_pct, next_beat_ms, beat8_ms, diff_tick_ms, est_period_ms, est_phase_unc_ms, est_resonant, diff_used_ms`.
`kind` is `beat` or `event` (sync on/off, vdj bpm changes, manual nudges, alignment, master changes, BPM source). `sync_state` is one of
off, waiting, aligning, rebasing, suspended, locked, follow-master. Column details are in `doc/sync-timing.md` section 6.
