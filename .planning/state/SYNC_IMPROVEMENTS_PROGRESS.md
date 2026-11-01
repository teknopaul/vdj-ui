# Sync improvements — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Telemetry and BeatTempo check | complete |
| 2 | Sequencer sub-tick phase model | complete |
| 3 | Quantised beat phase estimator | complete |
| 4 | SyncController state machine | complete |
| 5 | UI wiring: toggle and master changes | complete |
| 6 | Beat alignment on Sync | complete |
| 7 | DJ Tools latency | complete |
| 8 | Sync to calculated BPM | complete |
| 9 | Cadence for changing BPM (gated) | pending |
| 10 | Drift trim, nudge heuristic, docs (gated) | pending |

## Review notes

### Phase 1 — Telemetry and BeatTempo check

⚠ phase 1 was done before the plan was renumbered; telemetry has the raw packet columns

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/sync_telemetry.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/sync-timing.md`

### Phase 2 — Sequencer sub-tick phase model

⚠ every queue tempo change must go through the model: a missed anchor shows up as a resync, check the resync counter in the test

⚠ needs a real-deck look: compare diff_ms and diff_tick_ms in the telemetry

⚠ deviation from plan: the steady/queue clock offset is not kept; positionBeatsExact(&steadyNs) takes the steady time next to the queue read

⚠ found: nudgeMillis(n) shifts by n*base/(base+n), about 2% short (documented)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/seq_phase_model.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/alsa_clock.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/beat_source.cpp`

### Phase 3 — Quantised beat phase estimator

⚠ the quantum is learned (snapped to 0.5 ms), not hard coded: only one deck model has been measured

⚠ unit tests on real CSVs and synthetic data; judge the estimator on a live deck by watching est_* in the telemetry

⚠ not used by DiffLock yet: phase 5 adds it behind dj/sync_quantised_phase

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/network/beat_phase.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/network/beat_phase.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_beat_phase.cpp`

### Phase 4 — SyncController state machine

⚠ DiffLock public behaviour unchanged (test_diff_lock passes); new: rebase, suspend, setHold

⚠ deviation: a rebase sets the target to the measured offset (nothing relative to keep: the old target was against the old reference)

⚠ kGlideBpmPerBeat = 0.1 is provisional

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/sync_controller.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/diff_lock.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_sync_controller.cpp`

### Phase 5 — UI wiring: toggle and master changes

⚠ needs a real-deck test: Sync on a deck and on the VDJ row, press again to stop, change master on the decks, become master

⚠ behaviour change: a remote deck's BPM packets no longer set the VDJ tempo while Sync is off (DeckControls::setBpm blocks the spinner signal); see doc/sync-timing.md section 7

⚠ beats now feed the difflock from beatTimed (arrival time available); dj/sync_quantised_phase is off by default

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/deck_controls.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/sync_controller.cpp`

### Phase 6 — Beat alignment on Sync

⚠ audible behaviour: the user must listen to a sync on a real deck before the step cap (kAlignStepMs) is trusted

⚠ quantize grid wait is computed but not applied yet: the first step is at the next reference beat

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/sync_aligner.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/alsa_clock.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`

### Phase 7 — DJ Tools latency

⚠ sign convention: positive = audio stack is slow, schedule earlier; make sure the tooltip says so

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/djtoolz_scheduler.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/djtoolz_panel.cpp`

### Phase 8 — Sync to calculated BPM

⚠ STOP after this phase: the user tests calculated vs reported BPM and the telemetry before phases 9 and 10 are started

⚠ build-dev had been deleted; it was recreated with cmake -S/-B

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/diff_lock_bar.cpp`
