# Live sampler — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | BeatSource, sample cache, WAV info | complete |
| 2 | SamplerEngine voices and mixing | complete |
| 3 | Bombomatic, stab player, scheduler | complete |
| 4 | Acid loops, one shots, time-stretch | complete |
| 5 | MidiMatic | complete |
| 6 | Presets and sample folder model | complete |
| 7 | Sampler panel UI | complete |
| 8 | Integration, timing, docs | complete |

## Review notes

### Phase 1 — BeatSource, sample cache, WAV info

⚠ The beat map is re-derived on every call from AlsaClock::positionBeats(), which takes the clock's mutex and counts whole ticks (~5 ms at 120 BPM): check it is cheap enough and not held by the GUI when called from the audio thread

⚠ cmake/make here is in-source: running cmake regenerated the tracked Makefile/CPack/CTestTestfile files

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/sample_cache.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/wav_info.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/beat_source.h`

### Phase 2 — SamplerEngine voices and mixing

⚠ Pulse latency is a constant (kOutputLatencyMs = 40 ms, setLatencyMs) that must be tuned by ear; the engine opens its stream only after start()

⚠ Not run against a real sound server: only the VoiceMixer is unit tested

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/sampler_engine.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/voice_mixer.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_sampler_engine.cpp`

### Phase 3 — Bombomatic, stab player, scheduler

⚠ Lookahead is 100 ms (offsets clamped to +-80 ms); reverse sync uses 100 ms + the sample length

⚠ Triggers already posted keep the tempo they were computed at: a tempo change during a long reverse-sync lookahead is not corrected

⚠ One-bar: pressing again while armed OR playing cancels (remaining stabs of that bar are dropped, voices already playing finish)

⚠ Bombomatic/StabPlayer only: AcidLoop/OneShot/MidiMatic types are created in later phases (create() returns null for them)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/sampler_module.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/sampler_scheduler.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_sampler_modules.cpp`

### Phase 4 — Acid loops, one shots, time-stretch

⚠ WSOLA artefacts on percussive loops at large ratios (clamped to 0.5..2.0); listen to a loop at +-10 %

⚠ Acid loop restarts by choking the previous pass (VoiceMixer::stopTagAt) and switches off on the next beat; a loop whose stretched copy is not ready yet skips that bar

⚠ Reverse sync is not offered for acid loops (UI must hide it)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/sample_stretch.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/sampler_module.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/sampler_scheduler.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_sampler_stretch.cpp`

### Phase 5 — MidiMatic

⚠ Tick resolution is ~5 ms at 120 BPM, so MIDI offsets are quantised to whole ticks

⚠ AlsaClock now has a second port 'Sampler MIDI'; notes go straight to the chosen ALSA client:port (no subscription), or to subscribers when no device is set

⚠ AlsaClock::start()/stop() remove queued events, so the scheduler sends All Notes Off (CC 123) when the sequencer stops; sendSamplerMidi was only tested closed (not against a live ALSA sequencer)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/midi_out.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/alsa_clock.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/sampler_scheduler.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_midi_matic.cpp`

### Phase 6 — Presets and sample folder model

⚠ Preset files are plain JSON in <configDir>/sampler-presets/, the working state in <configDir>/sampler-current.json; no migration code yet (kVersion = 1, unknown keys ignored)

⚠ The sample folder is stored inside each preset; ConfigManager sampler/folder is not used (the panel in phase 7 decides)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/sampler_presets.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/sample_tree_model.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_sampler_presets.cpp`

### Phase 7 — Sampler panel UI

⚠ Not exercised against real audio hardware or a running sequencer: check preview, up/down and timing by ear

⚠ The 'View > Sampler' toggle and MainWindow wiring are not covered by a unit test (SamplerPanel is); the app starts offscreen without crashing

⚠ Armed one-bar button blinks green/grey, playing is red (my choice)

⚠ Sampler panel is created at startup (hidden unless View > Sampler was on) so enabled modules restored from sampler-current.json run even when hidden

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/sampler_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/sampler_module_widget.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/resources.qrc`

### Phase 8 — Integration, timing, docs

⚠ Timing was only verified by the logged rendered-vs-scheduled difference in the mixer (VoiceMixer timing, shown in the debug panel), not with an external measurement; output latency is a setting (sampler.conf latency_ms, default 40 ms) to tune by ear

⚠ Sampler output follows the Local Track Player's device combo; there is no separate device chooser

⚠ ASAN build ran the sampler tests clean; no ThreadSanitizer run was done (scheduler/engine/mixer are threaded)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/README.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/sampler_engine.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/sampler_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
