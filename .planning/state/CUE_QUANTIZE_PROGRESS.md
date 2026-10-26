# Cue quantize — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | BeatClock grid lookup | complete |
| 2 | Track beat phase while playing | complete |
| 3 | Scheduled seek in AudioPlayer | complete |
| 4 | Quantize button and cue-jump logic | complete |

## Review notes

### Phase 1 — BeatClock grid lookup

⚠ Before the first beat and after the last beat the grid is extrapolated from the neighbouring interval

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/beat_clock.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/beat_clock.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_beat_clock.cpp`

### Phase 2 — Track beat phase while playing

⚠ phase_ is up to ~33 ms stale (positionChanged is ~30 Hz); the jump code re-reads the position instead

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/player_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_player_quantize.cpp`

### Phase 3 — Scheduled seek in AudioPlayer

⚠ The jump is split inside a render chunk; output latency is intentionally not compensated (DJ adjusts by ear)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/audio_player.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/audio_player.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_audio.cpp`

### Phase 4 — Quantize button and cue-jump logic

⚠ Not tried against real audio output: check by ear that the landing is on the beat

⚠ Q on/off is not persisted; the resolution is (dj/quantize)

⚠ Resolution is a Config > Quantize submenu, not a combo; play|pause is one toggle (pauseButton() removed)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/player_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/beat_clock.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/quantize_settings.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_player_quantize.cpp`
