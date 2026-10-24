# VDJ UI - Qt GUI for VDJ — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Phase 1: Project Scaffold & CMake Build | complete |
| 2 | Phase 2: C++ Wrappers for libcdj / libvdj | complete |
| 3 | Phase 3: Application Shell & Layout | complete |
| 4 | Phase 4: Beat Grid Renderer | complete |
| 5 | Phase 5: Network Integration & ProLink | complete |
| 6 | Phase 6: Deck Controls & Interaction | complete |
| 7 | Phase 7: USB / MIDI Configuration | complete |
| 8 | Phase 8: Rekordbox NFS & Track Rendering | complete |
| 9 | Phase 9: Debug Mode & Logging | complete |
| 10 | Phase 10: Polish, Testing & Packaging | complete |

## Review notes

### Phase 1 — Phase 1: Project Scaffold & CMake Build

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/CMakeLists.txt`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/main.cpp`

### Phase 2 — Phase 2: C++ Wrappers for libcdj / libvdj

⚠ C headers lack extern C guards — wrappers wrap includes in extern "C"; if libcdj headers later add guards this becomes redundant

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/cdj/cdj_wrapper.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/vdj/vdj_wrapper.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/vdj/backline_model.h`

### Phase 3 — Phase 3: Application Shell & Layout

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/dark_theme.h`

### Phase 4 — Phase 4: Beat Grid Renderer

⚠ Timer precision may vary on RPi — consider clock_nanosleep fallback

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/beat_grid_widget.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/deck_widget.h`

### Phase 5 — Phase 5: Network Integration & ProLink

⚠ Ensure thread safety — vdj managed threads call into Qt via QueuedConnection (default AutoConnection handles cross-thread)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/network/network_manager.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/network_config_dialog.h`

### Phase 6 — Phase 6: Deck Controls & Interaction

⚠ Watch the null-sender connect crash: always create deck widgets before connecting their signals

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/deck_controls.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/pitch_slider.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/keyboard_shortcuts.h`

### Phase 7 — Phase 7: USB / MIDI Configuration

⚠ Config files default to ~/.config/vdj-ui when /etc/vdj-ui is not writable by uid 1000

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/config/config_manager.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/usb_config_dialog.h`

### Phase 8 — Phase 8: Rekordbox NFS & Track Rendering

⚠ NFS mount shells out to 'mount' and may need root — errors surfaced in status bar; track listing falls back to ~/Music in demo mode

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/nfs/nfs_mount_manager.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/track_browser.h`

### Phase 9 — Phase 9: Debug Mode & Logging

⚠ Debug logging may impact performance on RPi — disable by default

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/debug/packet_logger.h`

### Phase 10 — Phase 10: Polish, Testing & Packaging

⚠ Review all error paths before release

⚠ deckLeft has a UI handler but leave-detection is not yet driven by libvdj backline scanning - review for production

⚠ Performance profiling on RPi still pending hardware - see profiling notes

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/CMakeLists.txt`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/nfs/nfs_mount_manager.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_integration.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/vdj-ui.1`
