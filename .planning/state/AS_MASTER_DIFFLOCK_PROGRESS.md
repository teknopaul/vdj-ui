# As Master Difflock — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Pure echo/offset maths + tests | complete |
| 2 | Echo events in AlsaClock + MainWindow wiring | complete |
| 3 | UI: replace Send Beat with spinner | complete |
| 4 | Telemetry, docs, end-to-end check | complete |

## Review notes

### Phase 1 — Pure echo/offset maths + tests

⚠ Positive offset = packet sent AFTER the MIDI clock beat (same meaning as difflock: sequencer early)

⚠ Design is ALSA echo events, not a timing thread

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/master_beat_math.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/master_beat_math.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_master_beat_math.cpp`

### Phase 2 — Echo events in AlsaClock + MainWindow wiring

⚠ Beat packets now sent automatically whenever the ALSA clock runs, even when not master

⚠ Clock thread now polls the sequencer fd instead of sleeping 2 ms: review for starvation of the clock-pulse queueing

⚠ Verify with tcpdump on the beat port before trusting phase 3

⚠ Pre-existing ctest failures unrelated to this work: vdj-ui-audio-test (deckUnreachable), vdj-ui-trakcrate-panel-test (menu list)

⚠ Echo event has only 3 free 32-bit words: word0 = kind | generation<<8, words 1-2 = packed beat+bar

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/alsa_clock.cpp:399`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp:115`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/network/network_manager.cpp:306`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_alsa_clock_beats.cpp`

### Phase 3 — UI: replace Send Beat with spinner

⚠ Send Beat button, broadcastFromActiveDeck and the broadcast_beat joystick action (it was only listed in the config dialog, never dispatched) are removed

⚠ NetworkManager::broadcastBeatQuiet was renamed broadcastBeat (the old logging one had no callers left)

⚠ Spinner range is +/-500 ms; the clock clamps it to 0.45 of a beat period at the current BPM

⚠ Pre-existing failures: audio-test, trakcrate-panel-test; artwork-edit-test failed once under ctest -j4 and passes alone

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/deck_controls.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp:530`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/joystick_config_dialog.cpp:22`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/network/network_manager.cpp:314`

### Phase 4 — Telemetry, docs, end-to-end check

⚠ Telemetry is a debug-overlay line on the VDJ row only (SyncTelemetry has no natural slot)

⚠ Offset effect verified against the real ALSA sequencer at +/-60 ms (tests/test_alsa_clock_beats.cpp, within 8 ms); NOT verified on the wire: tcpdump / a real deck following the VDJ was not run

⚠ asan build (build-asan) was not run

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/README.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/sync.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
