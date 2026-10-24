# VDJ-UI Future Features — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Phase 1: Quick UI Polish | complete |
| 2 | Phase 2: VDJ Master BPM Control | complete |
| 3 | Phase 3: Auto-IP Script & Status UI | complete |
| 4 | Phase 4: Distribution & .deb Packaging | complete |
| 5 | Phase 5: Crash Stability (double free) | complete |
| 6 | Phase 6: Passive Monitor Mode | complete |
| 7 | Phase 7: Rekordbox Track Browser | complete |
| 8 | Phase 8: Rekordbox Metadata Viewer | complete |
| 9 | Phase 9: Rekordbox Metadata Edit | complete |
| 10 | Phase 10: Pendrive Cloning & Batch Fix | complete |
| 11 | Phase 11: DJ HUD Window | complete |
| 12 | Phase 12: Playlist History & Export | complete |
| 13 | Phase 13: MIDI Routing | complete |
| 14 | Phase 14: Session Audio Recording | complete |

## Review notes

### Phase 1 — Phase 1: Quick UI Polish

⚠ About dialog shows plain text link — logo not embedded (no network fetch at build time)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/deck_widget.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/network/network_config_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/CMakeLists.txt`

### Phase 2 — Phase 2: VDJ Master BPM Control

⚠ Ramp timer fires on beatReceived (barPos==1); if no beats arriving the ramp stalls — add demo clock fallback if needed

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/bpm_ramp_widget.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/bpm_ramp_widget.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`

### Phase 3 — Phase 3: Auto-IP Script & Status UI

⚠ Installed script must be executable; cmake install sets this via PROGRAMS

⚠ pkexec path differs across distros — /usr/bin/pkexec assumed

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/scripts/vdj-autoip.sh`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/network/auto_ip_manager.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/autoip_status_widget.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/autoip_status_widget.cpp`

### Phase 4 — Phase 4: Distribution & .deb Packaging

⚠ Icon data/vdj-ui.png must be provided manually (copy from adj-logo-full.png)

⚠ t64 package names are Ubuntu 24.04+; older distros need libqt5widgets5 etc. without t64 suffix

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/CMakeLists.txt`
- `/home/teknopaul/bzr_workspace/vdj-ui/data/vdj-ui.desktop`
- `/home/teknopaul/bzr_workspace/vdj-ui/make-deb.sh`

### Phase 5 — Phase 5: Crash Stability (double free)

⚠ Root cause confirmed: v_->client must be nulled before vdj_stop_threads to prevent callback/teardown race

⚠ Run with ASAN (cmake -B build-asan -DCMAKE_CXX_FLAGS=-fsanitize=address) for full validation on a live ProLink network

⚠ requestMaster() backline null-check added; full heap analysis needs live hardware

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/network/network_manager.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_wrappers.cpp`

### Phase 6 — Phase 6: Passive Monitor Mode

⚠ Passive mode uses vdj_open_broadcast_sockets — no status packets received (no unicast handshake)

⚠ In passive mode local VDJ deck is set to remote=true; it resets when disconnecting

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/network/network_manager.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/network/network_manager.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`

### Phase 7 — Phase 7: Rekordbox Track Browser

⚠ PDB3 format: artist/key name tables parsed with name at row+8 — verified against reference format spec

⚠ Artwork decode deferred to Phase 8 (AnlzReader); artwork() returns null QPixmap

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/nfs/rekordbox_db.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/nfs/rekordbox_db.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/track_browser.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/track_browser.cpp`

### Phase 8 — Phase 8: Rekordbox Metadata Viewer

⚠ ANLZ path derived from track analyze_path field; format is rekordbox-version-dependent — test with CDJ-2000NXS2 pendrives

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/nfs/anlz_reader.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/nfs/anlz_reader.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/track_meta_dialog.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/track_meta_dialog.cpp`

### Phase 9 — Phase 9: Rekordbox Metadata Edit

⚠ PDB3 page layout: each table row spans one or more 4096-byte pages — must parse row offset correctly before patching bytes

⚠ Do not edit live NFS mount; copy to tmpfs, edit, rsync back

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/nfs/rekordbox_db_writer.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/nfs/rekordbox_db_writer.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/track_edit_dialog.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/track_edit_dialog.cpp`

### Phase 10 — Phase 10: Pendrive Cloning & Batch Fix

⚠ Cloning a live NFS pendrive is theoretically safe but risky if the deck writes during copy — warn user

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/pendrive_clone_dialog.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/pendrive_clone_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/nfs/rekordbox_db_writer.cpp`

### Phase 11 — Phase 11: DJ HUD Window

⚠ Spectrum bars are synthetic (beat-phase driven), not audio-derived — label clearly in UI

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/hud_window.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/hud_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`

### Phase 12 — Phase 12: Playlist History & Export

⚠ Track change detection relies on title string comparison — NFS must be working for track names to be available

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/session_log.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/session_log.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/session_panel.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/session_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`

### Phase 13 — Phase 13: MIDI Routing

⚠ ALSA sequencer requires libasound2-dev; add to CMake find_package and deb Depends

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/midi_router.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/midi/midi_router.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/midi_routing_dialog.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/midi_routing_dialog.cpp`

### Phase 14 — Phase 14: Session Audio Recording

⚠ lame subprocess approach avoids GPL linking concerns — ensure lame is in PATH or add to deb Depends

⚠ PulseAudio monitor source can capture mixer output without a cable — mention in UI tooltip

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/audio_recorder.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/audio/audio_recorder.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/recording_panel.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/recording_panel.cpp`
