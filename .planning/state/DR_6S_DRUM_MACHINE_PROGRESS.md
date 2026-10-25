# DR-6S drum machine — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Data model: voices, patterns, swing, choke | complete |
| 2 | Mixer timed fade-out (choke) | complete |
| 3 | DrumMachine module and scheduling | complete |
| 4 | Voice and step button images | complete |
| 5 | Voice selector, step chooser, swing slider | complete |
| 6 | DR-6S widget and panel integration | complete |
| 7 | Drum kits | complete |
| 8 | Integration, timing, docs | complete |

## Review notes

### Phase 1 — Data model: voices, patterns, swing, choke

⚠ Swing delays every odd 0-based step (the 2nd and 4th sixteenth of each beat), 127 = a quarter of a beat = one step, as confirmed by the human

⚠ The DrumMachine type and class exist already (settings + variant only, not in the Add module menu yet); drumChokeClamp ties go to the lower value

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/swing.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/drum_pattern.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/sampler_module.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_drum_model.cpp`

### Phase 2 — Mixer timed fade-out (choke)

⚠ The fade is linear in gain and ends at the cut point

⚠ A Trigger with cutAfterFrames but the default fade (4410) clamps the fade to the cut length: set fadeFrames explicitly in tests

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/voice_mixer.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_sampler_engine.cpp`

### Phase 3 — DrumMachine module and scheduling

⚠ tickDrums and tickMidi share windowStart(); a swung hit is planned in the window of its grid position and plays later - tested

⚠ Dembomatik swing tests live in tests/test_drum_machine.cpp (not test_sampler_modules.cpp)

⚠ A choke is computed from the BPM when the hit is scheduled; queued hits keep it if the tempo changes

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/sampler_module.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/sampler_scheduler.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_drum_machine.cpp`

### Phase 4 — Voice and step button images

⚠ PNGs are generated from the SVGs with inkscape (voice buttons 32 px, pink 24 px); regenerate them if the SVGs change

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/resources.qrc`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/sampler_icons.cpp`

### Phase 5 — Voice selector, step chooser, swing slider

⚠ Click cycle: plain click off->on, on->off, pink->red; ctrl+click off/on->pink, pink->off (my choice, as planned). The step button reacts on mouse release (the QToolButton clicked() signal is not used)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/step_chooser.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/drum_voice_selector.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/swing_slider.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_drum_ui.cpp`

### Phase 6 — DR-6S widget and panel integration

⚠ The DR-6S takes a whole row in the module grid (gridCell() now returns a QRect); the module-level offset is not shown (per voice offsets instead)

⚠ Wheel over the DR-6S editor = selected voice volume; over its swing slider and elsewhere = module (master) volume. The Dembomatik also has the swing slider now

⚠ Not looked at on screen: the editor is about 450 px wide, check the layout at 2 and 4 across

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/drum_editor.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/sampler_module_widget.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/sampler_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_drum_ui.cpp`

### Phase 7 — Drum kits

⚠ Kits store absolute sample paths; moving the sample folder breaks them (shown as missing)

⚠ A kit does not hold the module's name, on/off or master volume; the kit row sits in the editor's top line (kit combo, Save kit, Delete)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/drum_kits.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/drum_editor.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_drum_kits.cpp`

### Phase 8 — Integration, timing, docs

⚠ Not heard on real audio: check the swing, the choke fade and the half volume steps by ear

⚠ Not looked at on screen: the DR-6S editor is about 450 px wide and takes a whole row; check it at 2 and 4 across

⚠ No cross-voice choke (HH cutting OH), no step cursor. ASAN run was clean; no ThreadSanitizer run

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/README.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/sampler_scheduler.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/drum_editor.cpp`
