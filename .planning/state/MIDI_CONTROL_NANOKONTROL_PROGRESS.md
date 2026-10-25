# MIDI control (nanoKONTROL) — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Sampler ops in .adjmm, mapping maths | complete |
| 2 | Raw controller events and SamplerMidiControl | complete |
| 3 | Modules mode: strips drive modules | complete |
| 4 | DR-6S per-voice mute | complete |
| 5 | DR-6S mode: voices and kits | complete |
| 6 | Wiring, settings dialog, MIDI learn | complete |
| 7 | adj project: adj_midilearn -s (optional) | pending |
| 8 | Integration, review, docs | complete |

## Review notes

### Phase 1 — Sampler ops in .adjmm, mapping maths

⚠ The CC numbers in the example file are a GUESS (nanoKONTROL2 layout), not verified on the Studio; select is commented out

⚠ Sampler ops are extra names in the normal .adjmm format; adj itself logs them as unknown names until Phase 7

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/midi_map_input.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/sampler_ctl_map.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/pickup.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/nanokontrol-studio-sampler.adjmm`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_sampler_ctl_map.cpp`

### Phase 2 — Raw controller events and SamplerMidiControl

⚠ Events come from the reader thread: all consumers must be connected with the default queued connection

⚠ The ALSA reader itself was not run against a real controller; start("") (empty map) is untested end to end, MainWindow still needs to use it (Phase 6)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/midi_map_input.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/sampler_midi_control.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_sampler_midi_control.cpp`

### Phase 3 — Modules mode: strips drive modules

⚠ Rec = forward, select = back (literal reading of the prompt) - one "#! rec_is_forward" line swaps them

⚠ Pickup is on by default: a fader that is far from the on-screen value does nothing until it reaches it

⚠ The controller mode is saved in sampler-current.json and in sampler presets (loading a preset also sets the mode)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/sampler_controller.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/sampler_module_widget.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/sample_chooser.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_sampler_controller.cpp`

### Phase 4 — DR-6S per-voice mute

⚠ Voice mute is saved with the module / sampler presets but deliberately not in drum kits

⚠ Alt+click or middle click on a voice button toggles its mute; a voice that is already ringing finishes

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/drum_pattern.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/sampler_scheduler.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/drum_voice_selector.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/drum_editor.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_drum_machine.cpp`

### Phase 5 — DR-6S mode: voices and kits

⚠ With several DR-6S modules the controller drives the one touched last (else the first)

⚠ Strips 7 and 8, and solo / rec / select, do nothing in the DR-6S mode

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/sampler_controller.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/drum_editor.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_sampler_controller.cpp`

### Phase 6 — Wiring, settings dialog, MIDI learn

⚠ Not tried with the real nanoKONTROL Studio: the example CC numbers are a guess, use Controller... > Learn to capture the real ones

⚠ No LED feedback to the controller; a second controller is wired only via midi_input_sampler in the config file (no dialog)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/controller_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/README.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_controller_dialog.cpp`

### Phase 8 — Integration, review, docs

⚠ Real nanoKONTROL Studio not tested; the ALSA reader was tested with a software sender only

⚠ ASAN clean on the new tests; no ThreadSanitizer run (the reader thread is new code)

⚠ Phase 7 (adj project edits) NOT done: it needs the human's OK; adj logs the sampler op names as unknown until then

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/README.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/midi_map_input.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/sampler_controller.cpp`
