# VDJ UI - Qt GUI for Virtual DJ

A Qt-based GUI for VDJ that renders real-time beat positions from Pioneer CDJ/XDJ
ProLink networks. Designed for nightclub use: dark mode, keyboard operation,
real-time beat grid rendering for up to 5 decks.

## Architecture

- **Build**: CMake + Qt6 (Widgets)
- **Language**: C++17 with C wrappers for `libcdj`/`libvdj`
- **Design**: Single-page application with menu bar, icon toolbar, status bar
- **Config**: Reuse vdj config + flat files in `/etc/` (user 1000 read-write)
- **Platform**: Linux (Raspberry Pi primary target)

---

## Phase 1: Project Scaffold & CMake Build

Set up the CMake project structure, Qt6 dependency, and a minimal running window.

### Deliverables

- `CMakeLists.txt` with Qt6 Widgets, C++17, and test support
- `src/main.cpp` — QApplication + empty main window
- `src/CMakeLists.txt` — src target definition
- Build instructions in README
- Unit test skeleton with GoogleTest or snip

### Verification

```sh
cmake -B build && cmake --build build
./build/vdj-ui   # empty window appears
```

```sh
geany-progress done 1 -r CMakeLists.txt -r src/main.cpp
```

---

## Phase 2: C++ Wrappers for libcdj / libvdj

Wrap the C libraries in RAII C++ classes with Qt-friendly interfaces.

### Deliverables

- `src/cdj/CdjWrapper` — wraps `cdj.h` functions (packet parsing, BPM/pitch conversion)
- `src/vdj/VdjWrapper` — wraps `vdj_t` lifecycle (init, open sockets, threads, destroy)
- `src/vdj/BacklineModel` — wraps `vdj_backline_t`, provides Qt model data
- CMake target `vdj-cdj` as a static library linked to libcdj/libvdj
- Unit tests for wrapper classes

### Key files

- `src/cdj/cdj_wrapper.h/.cpp`
- `src/vdj/vdj_wrapper.h/.cpp`
- `src/vdj/backline_model.h/.cpp`

### Verification

```sh
cmake --build build && ctest --test-dir build
```

```sh
geany-progress done 2 -r src/cdj/cdj_wrapper.h -r src/vdj/vdj_wrapper.h -r src/vdj/backline_model.h
```

---

## Phase 3: Application Shell & Layout

Build the main window with menu bar, toolbar, status bar, and deck area layout.

### Deliverables

- `MainWindow` class with:
  - Menu bar (File, View, Network, Help)
  - Icon toolbar (play/pause, sync, BPM controls)
  - Central widget with scrollable deck area
  - Status bar showing network info
- Dark palette stylesheet (nightclub theme)
- Keyboard shortcut bindings
- Deck area as a `QVBoxLayout` holding up to 5 `DeckWidget` placeholders

### Key files

- `src/ui/main_window.h/.cpp`
- `src/ui/deck_area.h/.cpp`
- `src/ui/dark_theme.h/.cpp`
- `src/ui/toolbar.h/.cpp`

### Verification

```sh
# Launch, verify dark theme, menus, toolbar render correctly
./build/vdj-ui
```

```sh
geany-progress done 3 -r src/ui/main_window.h -r src/ui/dark_theme.h
```

---

## Phase 4: Beat Grid Renderer

Implement the real-time beat grid display — the core visual feature.

### Deliverables

- `BeatGridWidget` — custom `QWidget` painting a 4/4 beat grid
  - Vertical moving line showing current playhead position
  - Grid lines for bar/beat subdivision
  - Aligned horizontally across all deck rows
- `DeckWidget` — per-deck container with:
  - Beat grid at top
  - BPM display (`???.??`)
  - Player ID, master badge, play state icon
- 60fps refresh timer tied to `QTimer` (or `QBasicTimer` for precision)
- Beat position calculated from `vdj_link_member_t.last_beat` + BPM

### Key files

- `src/ui/beat_grid_widget.h/.cpp`
- `src/ui/deck_widget.h/.cpp`

### Verification

```sh
# Run with mock data, verify grid scrolls smoothly
./build/vdj-ui --mock-beat
```

```sh
geany-progress done 4 -r src/ui/beat_grid_widget.h -r src/ui/deck_widget.h -w "Timer precision may vary on RPi — consider clock_nanosleep fallback"
```

---

## Phase 5: Network Integration & ProLink

Connect the real `libvdj` managed threads to the UI via Qt signals/slots.

### Deliverables

- `NetworkManager` — owns `vdj_t*`, starts managed threads, emits Qt signals:
  - `deckJoined(player_id, ip)`
  - `deckLeft(player_id)`
  - `beatReceived(player_id, bpm, bar_pos)`
  - `statusUpdated(player_id, status)`
  - `masterChanged(player_id)`
- Network config dialog (interface selector, start/stop autoip)
- Backline display in status bar (connected deck count, master indicator)
- Thread-safe signal bridging (Qt::QueuedConnection from worker thread)

### Key files

- `src/network/network_manager.h/.cpp`
- `src/ui/network_config_dialog.h/.cpp`

### Verification

```sh
# Connect to real ProLink network, verify deck detection
./build/vdj-ui
```

```sh
geany-progress done 5 -r src/network/network_manager.h -r src/ui/network_config_dialog.h -w "Ensure thread safety — vdj managed threads call into Qt via QueuedConnection"
```

---

## Phase 6: Deck Controls & Interaction

Implement user controls for deck interaction.

### Deliverables

- Deck control panel per deck:
  - Play / Pause button
  - Sync to downbeat
  - Copy BPM from master / specific deck
  - Set BPM manually (spinbox)
  - Become master button
- Pitch slider (tempo adjust)
- Keyboard shortcuts for all controls
- VDJ-specific controls (broadcast beat, set bar position)

### Key files

- `src/ui/deck_controls.h/.cpp`
- `src/ui/pitch_slider.h/.cpp`
- `src/ui/keyboard_shortcuts.h/.cpp`

### Verification

```sh
# Test all controls with mock VDJ, verify signals fire correctly
./build/vdj-ui --mock-vdj
```

```sh
geany-progress done 6 -r src/ui/deck_controls.h -r src/ui/pitch_slider.h
```

---

## Phase 7: USB / MIDI Configuration

Expose USB device and MIDI routing configuration.

### Deliverables

- USB config dialog:
  - List detected USB MIDI devices
  - Route local USB devices (drum machines) to VDJ's ALSA MIDI output
  - Define XDJ as MIDI source
- Joystick configuration:
  - List detected joysticks
  - Auto-learn mapping tool
  - Write device config files to `/etc/vdj-ui/`
- Config persistence (flat files for user 1000)

### Key files

- `src/ui/usb_config_dialog.h/.cpp`
- `src/ui/joystick_config_dialog.h/.cpp`
- `src/config/config_manager.h/.cpp`

### Verification

```sh
# Verify config files written correctly, USB devices detected
./build/vdj-ui
ls /etc/vdj-ui/
```

```sh
geany-progress done 7 -r src/config/config_manager.h -r src/ui/usb_config_dialog.h -w "Config files in /etc/ require correct ownership (user 1000)"
```

---

## Phase 8: Rekordbox NFS & Track Rendering

Render rekordbox data and now-playing information from NFS.

### Deliverables

- NFS mount manager (reuse `libcdj` NFS client)
- Track browser panel:
  - Directory tree of rekordbox tracks on each deck's USB
  - Track metadata display (title, artist, BPM, key)
- Now-playing display per deck
- Integration with `BacklineModel` for per-deck NFS status

### Key files

- `src/nfs/nfs_mount_manager.h/.cpp`
- `src/ui/track_browser.h/.cpp`
- `src/ui/now_playing.h/.cpp`

### Verification

```sh
# Mount NFS from connected CDJ, verify track listing renders
./build/vdj-ui
```

```sh
geany-progress done 8 -r src/nfs/nfs_mount_manager.h -r src/ui/track_browser.h -w "NFS mount may need root — handle permission errors gracefully"
```

---

## Phase 9: Debug Mode & Logging

Add packet logging and debug overlay for protocol development.

### Deliverables

- Debug mode toggle (menu or keyboard shortcut)
- Packet log panel:
  - Scrollable list of ProLink messages with timestamps
  - Filter by message type
  - Correlate with user operations
- Debug overlay on deck widgets showing raw protocol state
- Log to file for post-analysis

### Key files

- `src/debug/packet_logger.h/.cpp`
- `src/ui/debug_panel.h/.cpp`
- `src/ui/debug_overlay.h/.cpp`

### Verification

```sh
# Enable debug mode, verify packets logged alongside UI operations
./build/vdj-ui --debug
```

```sh
geany-progress done 9 -r src/debug/packet_logger.h -w "Debug logging may impact performance on RPi — disable by default"
```

---

## Phase 10: Polish, Testing & Packaging

Final integration testing, edge cases, and packaging.

### Deliverables

- End-to-end integration tests (mock VDJ + UI)
- Performance profiling on RPi
- `make deb` packaging via CMake
- Man page and usage documentation
- Error handling audit (network drops, USB disconnects, NFS failures)
- Graceful shutdown of all threads

### Key files

- `tests/integration/` — end-to-end tests
- `CMakeLists.txt` — packaging targets
- `doc/vdj-ui.1` — man page

### Verification

```sh
cmake --build build && ctest --test-dir build
sudo make deb
```

```sh
geany-progress done 10 -r CMakeLists.txt -w "Review all error paths before release"
```
