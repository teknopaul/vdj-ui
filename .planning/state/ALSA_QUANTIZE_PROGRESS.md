# ALSA quantize — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Bar-position model and light restart in AlsaClock | complete |
| 2 | Show and send the new bar position | complete |
| 3 | Shared Q button and glyph buttons | complete |
| 4 | DeckPhase: other decks' beat and bar | complete |
| 5 | Quantized restart and beat jumps in MainWindow | complete |
| 6 | Quantized Play for the sequencer | complete |
| 7 | MIDI map and joystick use quantized restart | complete |
| 8 | Experimental: tell the drum machines | complete |
| 9 | Docs and full run | complete |

## Review notes

### Phase 1 — Bar-position model and light restart in AlsaClock

⚠ restart() reuses the running clock thread; check by ear/with a drum machine that START arrives right after STOP and the pattern restarts

⚠ A beat jump changes only the bar label, never the clock timing

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/seq_bar.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/alsa_clock.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_seq_bar.cpp`

### Phase 2 — Show and send the new bar position

⚠ Only the label moves on a beat jump: beat packets and MIDI clock timing are unchanged; the sampler (AlsaClockBeatSource) keeps reading raw beats; DiffLock only looks at the deck's barPos (==4) so it needed no change

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_beat_grid.cpp`

### Phase 3 — Shared Q button and glyph buttons

⚠ '<< Beat' (broadcast) was renamed 'Send Beat' to avoid clashing with the new back-one-beat button

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/deck_controls.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/quantize_button.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/player_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_controls.cpp`

### Phase 4 — DeckPhase: other decks' beat and bar

⚠ Extrapolated from the last beat packet (steady arrival time from beatTimed); accuracy is the packet arrival jitter (a few ms)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/deck_phase.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_deck_phase.cpp`

### Phase 5 — Quantized restart and beat jumps in MainWindow

⚠ Restart is scheduled with a precise QTimer (~1 ms jitter) and then restart() on the live clock thread; not tried against real decks or drum machines

⚠ Without a sync deck (we are the master) a quantized restart waits for our own next whole beat

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/deck_phase.cpp`

### Phase 6 — Quantized Play for the sequencer

⚠ Start is scheduled with a precise QTimer, not on the ALSA queue; not tried against real decks

⚠ MIDI-map start/toggle stay immediate

⚠ While Play is queued the button already shows pause; pressing it again cancels the wait (it does not start immediately)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/deck_phase.cpp`

### Phase 7 — MIDI map and joystick use quantized restart

⚠ Joysticks have never been tested on real hardware: only the pure event->action mapping is unit tested

⚠ MIDI-map q_restart is now really quantized (it was an immediate stop/start before)

⚠ The joystick bridge now skips the initial state events sent when the device is opened

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/joystick_seq_bridge.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_joystick_map.cpp`

### Phase 8 — Experimental: tell the drum machines

⚠ SKIPPED on purpose: the optional Song Position Pointer experiment is only worth building after trying the beat jumps with the real drum machines; nothing was implemented

### Phase 9 — Docs and full run

⚠ Run the whole ctest and listen to it on real hardware (decks, drum machines, joystick) before relying on it

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/quantization.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_integration.cpp`
