# Recording Bar — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Fix capture crash | complete |
| 2 | lame dependency | complete |
| 3 | LADSPA compressor | complete |
| 4 | Recording bar UI | complete |
| 5 | Close warning, docs | complete |

## Review notes

### Phase 1 — Fix capture crash

⚠ Root cause: blocking snd_pcm_readi + deleting a running QThread after wait(3000) timed out; the thread now polls (snd_pcm_wait 100 ms) and is never deleted while running

⚠ Not verified on the Xone:43C itself: ask the human to try it

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/audio_recorder.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_audio_recorder.cpp`

### Phase 2 — lame dependency

⚠ Snap: check that lame exists in the base's archive; if not the snap needs a part that builds it

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/CMakeLists.txt`
- `/home/teknopaul/bzr_workspace/vdj-ui/snap/snapcraft.yaml`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/audio_recorder.cpp`

### Phase 3 — LADSPA compressor

⚠ File names changed from the plan: src/audio/ladspa_host.* already exists (LadspaHost for the DJ toolz) and was overwritten by mistake, then restored from .sst/truth: please check it with your own copy

⚠ Control values of caps CompressX2 are the plugin defaults: confirm by ear on a real recording

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/ladspa_effect.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/ladspa_min.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_ladspa_host.cpp`

### Phase 4 — Recording bar UI

⚠ RecordingPanel and its dock are gone; settings in config section 'recording'

⚠ Look at it at 1280 px wide: the meter is the part that shrinks

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/recording_bar.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp:2561`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/level_meter_widget.cpp`

### Phase 5 — Close warning, docs

⚠ Human retest: pick every capture device in the list (incl. the Xone) with the bar visible, record 10 s with and without the compressor, then close the window while recording

⚠ Full ctest: only vdj-ui-trakcrate-panel-test fails, as before this plan

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp:2326`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/recording.md`
