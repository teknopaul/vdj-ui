# Sync BPM precision check (sync-improvements phase 1)

## BeatTempo (src/network/beat_tempo.cpp)

Reviewed and tested; no bug found, no change made.

- Arrival times are int64 ns, the least squares fit is done on doubles relative to the first beat, so there is no ms or ns integer rounding in the fit.
- New tests: arrivals stamped to whole milliseconds (120, 127.93, 133.37, 98.01 BPM) read back within 0.02 BPM; a 200 beat run at 128.128 BPM with the 32 beat sliding window never moves more than 0.02 BPM between consecutive beats and ends within 1e-4 BPM.

## Reported BPM path (libcdj)

- `cdj_beat_calculated_bpm` = `cdj_calculated_bpm(cdj_beat_bpm, cdj_beat_pitch)` = `(float)(track_bpm * pitch_multiplier)` (references/libcdj/src/c/cdj.c ~350, ~790).
- `track_bpm` is a uint16 in 0.01 BPM units, so the reported BPM is quantised to the deck's own resolution before the pitch multiplier; the result is a `float` (about 7 significant digits, 0.00001 BPM at 128). That is not a rounding problem at the 0.01 BPM level the decks themselves use, but it means the *reported* value only changes in steps of the deck's pitch resolution. The calculated (measured) BPM is the only continuous source.
- No libcdj change proposed.

## Telemetry

View menu > "Log Sync Telemetry (CSV)" writes `sync-telemetry-<timestamp>.csv` into the application cache directory (QStandardPaths CacheLocation). Columns (append-only):
steady_ms,kind,pid,bar_pos,reported_bpm,measured_bpm,alsa_bpm,diff_ms,avg_diff_ms,target_ms,nudge_ms,master_pid,sync_state,note,track_bpm_raw,pitch_raw,pitch_pct,next_beat_ms,beat8_ms

The last five columns (raw beat packet fields) were appended later; older logs end at `note`.

`kind` is `beat` (one per remote beat; measured_bpm is the BeatTempo fit including that beat) or `event` (telemetry on/off, lock, difflock off, manual nudge, master change, VDJ BPM change, sync-to-master press; the text is in `note`).
