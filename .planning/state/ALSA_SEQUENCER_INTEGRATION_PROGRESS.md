# ALSA Sequencer Integration — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | AlsaSequencer engine class | complete |
| 2 | MidiClockPanel widget | complete |
| 3 | MainWindow integration + CMake | complete |
| 4 | Keyboard shortcuts for sequencer | complete |
| 5 | Joystick sequencer support | complete |

## Review notes

### Phase 1 — AlsaSequencer engine class

⚠ Clock accuracy depends on nanosleep() + queue lookahead; jitter < 5ms is acceptable for MIDI clock

⚠ AlsaSequencer uses std::atomic not _Atomic — confirm C++17 or later in CMake

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/alsa_sequencer.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/alsa_sequencer.cpp`

### Phase 2 — MidiClockPanel widget

⚠ BeatGridBar does not use QTimer — it is driven by tickAdvanced signal from clock thread; verify queued connection to avoid cross-thread painter issues

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/midi_clock_panel.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/midi_clock_panel.cpp`

### Phase 3 — MainWindow integration + CMake

⚠ Dock starts visible; user can hide it from the View menu

⚠ ALSA init failure is soft (warning only) so the app still starts without an ALSA sequencer

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/CMakeLists.txt`

### Phase 4 — Keyboard shortcuts for sequencer

⚠ Ctrl+Space conflicts if Qt uses it for something — verify no grab by OS/WM

⚠ Existing plain-Arrow pitch nudge shortcuts are preserved; Ctrl+Arrow is the sequencer layer

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/keyboard_shortcuts.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/keyboard_shortcuts.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`

### Phase 5 — Joystick sequencer support

⚠ Joystick device path must be configured; bridge skips silently if device is absent

⚠ adj_js.c axis values are +-32767; map to +-1 multipliers, not raw values

⚠ Logitech axis mapping is hardcoded by default; full remapping via JoystickConfigDialog is stretch goal

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/joystick_seq_bridge.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/joystick_seq_bridge.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/joystick_config_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
