# VDJ-UI Future Features Plan

All 10 original build phases are complete. This plan covers every feature item from
`ai-prompts/development-state.prompt.md`, grouped into phases that each fit within a
single Claude Sonnet 4.6 context window.

---

## Phase 1: Quick UI Polish

Three small independent fixes: About dialog, VDJ deck visual distinction, and NIC
persistence across sessions.

### Deliverables

**About dialog** (`src/ui/main_window.cpp`)
- Wire up the currently no-op `Menu > Help > About` action to a `QMessageBox` (or
  small custom `QDialog`) showing:
  - App name: "VDJ-UI"
  - Author: teknopaul
  - Version from `CMAKE_PROJECT_VERSION` (pass via compile-time define)
  - Logo: fetch `https://github.com/teknopaul/adj/raw/master/doc/adj-logo-full.png`
    at build time and embed as a Qt resource (`resources.qrc`), display in dialog

**VDJ deck background tint** (`src/ui/deck_widget.cpp`)
- `DeckWidget::paintEvent` currently uses `QColor(0x1c, 0x1c, 0x1c)` for non-master
  decks.
- Add a `local_` bool to `DeckWidget` (mirroring existing `remote_` bool).
- When `local_ == true` (software VDJ), use a very slight green tint:
  `QColor(0x1a, 0x1e, 0x1a)` instead of `0x1c,0x1c,0x1c`.
- `setLocal(bool)` / `isLocal()` accessors; `MainWindow` calls `setLocal(true)` on
  the VDJ deck after `isLocalDeck()` resolves.

**NIC persistence** (`src/ui/main_window.cpp`, `src/config/config_manager.h`)
- After a successful connect, call
  `config_->saveValue("network", "last_interface", currentInterface_)`.
- In `NetworkConfigDialog`, pre-fill the interface combo with the saved value via
  `config_->loadValue("network", "last_interface")` before showing.
- `NetworkConfigDialog` ctor needs a `ConfigManager*` param (or pass the saved iface
  as a `QString`).

### Key files
- `src/ui/main_window.cpp` / `.h`
- `src/ui/deck_widget.cpp` / `.h`
- `src/network/network_config_dialog.cpp` / `.h`
- `resources/resources.qrc` (new)
- `resources/adj-logo-full.png` (fetched at build time or bundled)

### Verification
```sh
cmake --build build
./build/vdj-ui
# Help > About shows logo, name, author
# VDJ deck row has subtly different background
# Connect, disconnect, re-connect — dialog pre-fills last NIC
```

```sh
geany-progress done 1 \
  -r src/ui/main_window.cpp \
  -r src/ui/deck_widget.cpp \
  -r src/network/network_config_dialog.cpp \
  -w "Logo PNG is fetched at build time; add CMake download step or bundle manually"
```

---

## Phase 2: VDJ Master BPM Control UI

Wire up the software VDJ becoming ProLink master and expose gradual BPM ramping.

### Background

`VdjWrapper::requestMaster()` exists and `DeckControls` already has `becomeMaster_`
button + `setBpmClicked` signal. What is missing:
- A gradual-ramp UI: target BPM, bars-to-ramp, apply button
- Greying out ramp controls when VDJ is not master
- The `applyGradualBpm` logic: step BPM each bar until target reached

### Deliverables

**`BpmRampWidget`** (new `src/ui/bpm_ramp_widget.h/.cpp`)
- `QDoubleSpinBox targetBpm_` — desired end BPM
- `QSpinBox rampBars_` — number of bars over which to ramp (default 32)
- `QPushButton* applyBtn_` — "Apply BPM"
- `QPushButton* plusOne_` / `minusOne_` — ±1 BPM increment
- `setEnabled(bool)` override: greyed out when VDJ is not master
- Emits `rampRequested(double targetBpm, int bars)`

**`MainWindow` wiring**
- Add `BpmRampWidget*` as a dock widget or toolbar expansion
- On `network_->masterChanged`: if local deck is now master, enable `BpmRampWidget`
- On `rampRequested(target, bars)`: start a `QTimer` that fires each beat; each tick
  computes the next BPM step = `(target - current) / remainingBars` and calls
  `network_->setLocalBpm(nextBpm)` — stop when delta < 0.01 BPM

**DeckControls tweak**
- `±1 BPM` buttons already exist as `becomeMaster_` trigger; add dedicated `+1`/`-1`
  shortcut buttons that emit `bpmDeltaRequested(int)` (+1 or -1), wired to
  `nudgeLocalBpm()` in MainWindow

### Key files
- `src/ui/bpm_ramp_widget.h/.cpp` (new)
- `src/ui/main_window.cpp` / `.h`
- `src/ui/deck_controls.cpp`

### Verification
```sh
# Start vdj-ui, click "Become Master" on VDJ deck
# BpmRampWidget becomes enabled
# Set target to current+5, bars=16, click Apply
# BPM increments gradually over 16 bars of beats
# ±1 buttons nudge BPM immediately
# BpmRampWidget greyed when not master
```

```sh
geany-progress done 2 \
  -r src/ui/bpm_ramp_widget.h \
  -r src/ui/bpm_ramp_widget.cpp \
  -r src/ui/main_window.cpp \
  -w "Ramp timer fires on beat received; if no beats arriving (no decks) ramp stalls — add wall-clock fallback"
```

---

## Phase 3: Auto-IP Script & Status UI

Replace the inline bash command with a proper script and expose clear status.

### Background

`AutoIpManager` embeds a bash heredoc script that shows up in `ps` as a long command.
The feature request is: a named script so `ps aux | grep vdj` shows something sensible,
plus a clear status indicator in the UI.

### Deliverables

**`/usr/local/bin/vdj-autoip` install script**
- Extract the embedded bash script from `AutoIpManager` into
  `scripts/vdj-autoip.sh` (tracked in repo).
- CMake `install(PROGRAMS scripts/vdj-autoip.sh DESTINATION bin RENAME vdj-autoip)`.
- `AutoIpManager::start()` invokes the installed `/usr/local/bin/vdj-autoip` (or falls
  back to the in-tree path for dev builds) via `QProcess` with `pkexec`.
- `ps aux` will now show `vdj-autoip eth0` — recognisable.

**Auto-IP status widget** (new `src/ui/autoip_status_widget.h/.cpp`)
- Small widget placed in the status bar (right side): shows claimed IP or "No IP" or
  an error icon.
- Listens to `AutoIpManager::ipChanged(QString)` and `AutoIpManager::stopped()`.
- Tooltip shows the full last-error string on hover.

**`NetworkConfigDialog` auto-ip feedback**
- After starting auto-ip show "AutoIP starting…" in a `QLabel` inside the dialog.
- On `AutoIpManager::ipChanged`, update label to "AutoIP: 169.254.x.x ✓".
- On error, show error text in red.

### Key files
- `scripts/vdj-autoip.sh` (new, extracted from `auto_ip_manager.cpp`)
- `src/network/auto_ip_manager.h/.cpp` (update to invoke named script)
- `src/ui/autoip_status_widget.h/.cpp` (new)
- `src/network/network_config_dialog.h/.cpp`
- `CMakeLists.txt` (install target for script)

### Verification
```sh
cmake --build build && sudo cmake --install build --prefix /usr/local
# Check /usr/local/bin/vdj-autoip exists
./build/vdj-ui
# Start AutoIP — ps aux shows "vdj-autoip <iface>"
# Status bar shows claimed IP once avahi claims it
# Disconnecting shows "No IP" in status bar
```

```sh
geany-progress done 3 \
  -r scripts/vdj-autoip.sh \
  -r src/network/auto_ip_manager.cpp \
  -r src/ui/autoip_status_widget.h \
  -w "pkexec path differs across distros — use which(pkexec) fallback"
```

---

## Phase 4: Distribution & .deb Packaging

Produce a proper installable `.deb` using the existing `../deb-template` and fix
runtime dependency declarations.

### Background

`CMakeLists.txt` already has `CPACK_GENERATOR "DEB"` but `CPACK_DEBIAN_PACKAGE_DEPENDS`
lists `qtbase5-dev` (a dev package) instead of runtime Qt libs, and does not cover
`avahi-autoipd`, `pkexec`, or the `libcdj`/`libvdj` SOs.

### Deliverables

**Runtime dependency audit**
- Run `ldd build/vdj-ui` to enumerate required SOs.
- Map SOs to Debian package names with `dpkg -S`.
- Fix `CPACK_DEBIAN_PACKAGE_DEPENDS` to list runtime packages:
  - Qt6: `libqt6widgets6, libqt6network6` (or Qt5 equivalents)
  - ProLink libs: `libcdj (>= 0.1), libvdj (>= 0.1)`
  - Auto-IP: `avahi-autoipd, policykit-1`

**App icon**
- Add `resources/vdj-ui.png` (64×64, derived from adj-logo or custom).
- CMake installs it to `share/pixmaps/vdj-ui.png`.
- Add `share/applications/vdj-ui.desktop` (new file in `data/`):
  ```ini
  [Desktop Entry]
  Name=VDJ-UI
  Exec=vdj-ui
  Icon=vdj-ui
  Type=Application
  Categories=AudioVideo;Audio;
  ```

**deb-template integration**
- Check `../deb-template` for its expected directory layout.
- Add a `make-deb.sh` script in the repo root that invokes CPack with the right
  variables, then optionally applies the deb-template fixups (postinst, etc.).

**Post-install script** (`debian/postinst` via CPack)
- Sets `setuid` bit on `/usr/local/bin/vdj-autoip` so it can run with root without
  pkexec if preferred, OR installs a polkit rule.

### Key files
- `CMakeLists.txt`
- `data/vdj-ui.desktop` (new)
- `resources/vdj-ui.png` (new or symlink)
- `make-deb.sh` (new helper script)
- `debian/postinst` (if using CPack custom scripts)

### Verification
```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && cpack -G DEB
# .deb appears; install on clean Ubuntu 22.04
sudo dpkg -i vdj-ui-*.deb
# apt-get install -f satisfies deps automatically
# App appears in app launcher with icon
```

```sh
geany-progress done 4 \
  -r CMakeLists.txt \
  -r data/vdj-ui.desktop \
  -r make-deb.sh \
  -w "libcdj/libvdj must already be installed on the target; add them to deb Depends or bundle their SOs"
```

---

## Phase 5: Crash Stability (double free)

Reproduce and fix the `double free or corruption (!prev)` abort under ASAN.

### Background

The crash is a heap corruption — likely a double-delete of a C-side struct, a
use-after-free in the Qt/C bridge, or a missing ownership guard in `VdjWrapper` or
`BacklineModel`. The `build-asan` directory suggests ASAN builds are already used.

### Deliverables

**Reproduce with ASAN**
- Build with `-DCMAKE_BUILD_TYPE=AddressSanitizer` (or existing `build-asan`).
- Run until crash; capture full ASAN output showing stack trace.
- Document the crash site in a comment block at the fix location.

**Fix candidates to investigate**
1. `VdjWrapper::destroy()` — is `vdj_destroy(v_)` safe to call when threads are still
   running? Does it free something that Qt still holds a pointer to?
2. `BacklineModel` — does it cache raw pointers into `vdj_backline_t` that become
   stale after `vdj_destroy`?
3. `NetworkManager` thread callback — if the callback fires after `VdjWrapper` is
   destroyed, it may dereference freed memory.
4. Any `QTimer` still ticking during shutdown that accesses `network_` after
   `MainWindow::closeEvent` has started teardown.

**Fix**
- Add lifecycle guards: check `isActive()` before any callback-driven access.
- Ensure `stopThreads()` fully joins before `destroy()` is called.
- Nullify raw pointers after free; replace any bare `delete` with `QPointer` or
  `std::unique_ptr` where possible.

**Regression test**
- Add a `tests/` test that constructs `VdjWrapper`, calls `init()` / `openSockets()`
  on a loopback, then calls `destroy()` twice to confirm it doesn't crash (guard
  idempotency).

### Key files
- `src/vdj/vdj_wrapper.cpp` / `.h`
- `src/vdj/backline_model.cpp`
- `src/network/network_manager.cpp`
- `src/ui/main_window.cpp` (`closeEvent`, `shutdown`)
- `tests/` (new regression test)

### Verification
```sh
cmake -B build-asan -DCMAKE_CXX_FLAGS="-fsanitize=address" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan
./build-asan/vdj-ui  # run for 30+ minutes without crash
# ASAN output: no heap errors reported
```

```sh
geany-progress done 5 \
  -r src/vdj/vdj_wrapper.cpp \
  -r src/network/network_manager.cpp \
  -r src/ui/main_window.cpp \
  -w "Root cause may differ from the candidates above — run ASAN first, then fix the actual trace"
```

---

## Phase 6: Passive Monitor Mode

Add a lightweight monitoring mode that reads ProLink broadcast packets without
performing the full CDJ/VDJ handshake.

### Background

The full connection requires device discovery, keepalive, and status broadcasts.
Passive mode listens only to the broadcast channel, exposing BPM, master state, and
beat clocks — useful for monitoring without disturbing an active ProLink network.
`references/libcdj` likely has a `cdj_mon`-style packet reader.

### Deliverables

**`PassiveMonitor`** (new `src/network/passive_monitor.h/.cpp`)
- Opens a raw/multicast socket on the ProLink broadcast port (UDP 50001/50002).
- Parses `cdj_packet_t` for beat and status packets (reuse `CdjWrapper` helpers).
- Emits Qt signals: `beatReceived(int pid, double bpm, int barPos)`,
  `statusReceived(int pid, double bpm, int pitch, int flags)`.
- No outbound packets — purely reads from the wire.

**UI integration**
- `Menu > Network > Monitor (Passive)` — new action in `setupMenus()`.
- When chosen, skip `NetworkConfigDialog`'s handshake path and instead start
  `PassiveMonitor` on the selected interface.
- The deck area updates identically to connected mode (drives `DeckWidget` via same
  beat/status signals).
- Toolbar shows "MONITOR" badge instead of "CONNECTED".
- Controls that require two-way communication are disabled (play/pause, sync, BPM set,
  become master).

**Status bar indicator**
- Add "PASSIVE" mode label in status bar when active.

### Key files
- `src/network/passive_monitor.h/.cpp` (new)
- `src/ui/main_window.cpp` (menu action, mode switching)
- `src/ui/toolbar.cpp` (MONITOR badge)

### Verification
```sh
# On a ProLink network, start vdj-ui in passive mode
# Decks light up with BPM and beat positions from the network
# No keepalive packets visible in Wireshark from the vdj-ui host
# Control buttons greyed out
```

```sh
geany-progress done 6 \
  -r src/network/passive_monitor.h \
  -r src/network/passive_monitor.cpp \
  -r src/ui/main_window.cpp \
  -w "Some ProLink versions require being on the same subnet for broadcast — test on real hardware"
```

---

## Phase 7: Rekordbox Track Browser

Extend `TrackBrowser` to parse rekordbox PDB databases from NFS mounts and local
pendrives, replacing the current generic filesystem browser.

### Background

`TrackBrowser` currently walks the filesystem with `QDir`. Rekordbox stores its
database in `/Contents/rekordbox.pdb` (PDB3 format). The `references/libcdj` library
may already have PDB parsing; if not, a minimal reader for the `Playlist` and
`Track` tables is needed.

### Deliverables

**`RekordboxDb`** (new `src/nfs/rekordbox_db.h/.cpp`)
- Opens and parses `rekordbox.pdb` using libcdj PDB API (or a minimal hand-rolled
  reader for the `Track`, `Playlist`, `PlaylistEntry`, `Artwork` tables).
- Exposes:
  ```cpp
  struct RbTrack { uint32_t id; QString title, artist, album; double bpm;
                   QString key; uint32_t artworkId; QString filePath; };
  QList<RbTrack> tracks();
  QList<QString>  playlists();
  QList<RbTrack>  tracksInPlaylist(const QString& name);
  QPixmap         artwork(uint32_t artworkId);  // cached
  ```

**`TrackBrowser` rewrite**
- Add a `QComboBox` at top: `[Filesystem | Rekordbox DB]`
- In Rekordbox mode, populate with `Playlists` view and `All Tracks` folder.
- Columns: Title, Artist, BPM, Key — a small artwork thumbnail in a 4th column
  (drawn as a 24×24 `QPixmap` in `QTreeWidget` via `QIcon`).
- Clicking a track emits `trackSelected` with metadata.
- Pendrive detection: watch `/media/$USER/` for new mounts via `QFileSystemWatcher`;
  auto-populate the source dropdown when a rekordbox pendrive is inserted.

**Source selector**
- Dropdown of: NFS-mounted deck paths, local pendrive mounts, manual filesystem path.
- Populated from `NfsMountManager::mountedPlayerIds()` + pendrive watch.

### Key files
- `src/nfs/rekordbox_db.h/.cpp` (new)
- `src/ui/track_browser.h/.cpp` (extend)
- `src/ui/main_window.cpp` (connect pendrive events)

### Verification
```sh
# Insert rekordbox pendrive
# TrackBrowser source dropdown shows pendrive
# Switching to Rekordbox DB mode shows playlists and tracks
# Track art thumbnails visible
# Selecting a track emits trackSelected with correct metadata
```

```sh
geany-progress done 7 \
  -r src/nfs/rekordbox_db.h \
  -r src/nfs/rekordbox_db.cpp \
  -r src/ui/track_browser.h \
  -r src/ui/track_browser.cpp \
  -w "PDB3 format: use libcdj parser if available; otherwise implement minimal table walker (pages are 4096 bytes)" \
  -w "Artwork decode requires reading the BLOB out of the Artwork table — can be JPEG or PNG"
```

---

## Phase 8: Rekordbox Metadata Viewer

Show waveform preview, beat grid overlay, and full-size artwork for a selected track.

### Background

Rekordbox stores pre-analysed data per track: a waveform preview (a small PNG/GIF
showing the audio energy), a beat grid (list of beat positions in milliseconds), and
high-res artwork. These are stored in the ANLZ files (`/PIONEER/USBANLZ/Pnn/` with
`.DAT` extension).

### Deliverables

**`AnlzReader`** (new `src/nfs/anlz_reader.h/.cpp`)
- Parses a `.DAT` ANLZ file for a given track.
- Extracts:
  - `PWAV` section: waveform preview (palette-indexed bitmap — convert to `QImage`)
  - `PQTZ` section: beat grid (array of beat offsets in ms, with bar markers)
  - `PCO2` section: cue point list (memory cues, hot cues)
- Exposes: `QImage waveformPreview()`, `QList<int> beatGrid()`,
  `QList<CuePoint> cuePoints()`.

**`TrackMetaDialog`** (new `src/ui/track_meta_dialog.h/.cpp`)
- `QDialog` opened by double-clicking a track in `TrackBrowser`.
- Layout:
  - Top: 128×128 artwork (from `RekordboxDb::artwork()`)
  - Middle: waveform image scaled to dialog width (`QLabel` with `QPixmap`)
  - Below waveform: cue point markers overlaid (draw small triangles at beat offsets)
  - Bottom: metadata table (title, artist, album, BPM, key, comment)

**Integration**
- `TrackBrowser` emits `trackDoubleClicked(int playerId, uint32_t trackId)`.
- `MainWindow` opens `TrackMetaDialog(db, trackId, anlzPath, this)`.

### Key files
- `src/nfs/anlz_reader.h/.cpp` (new)
- `src/ui/track_meta_dialog.h/.cpp` (new)
- `src/ui/track_browser.h/.cpp` (add double-click signal)

### Verification
```sh
# Double-click a track in Rekordbox browser
# Dialog opens: artwork top-left, waveform strip, beat grid overlay
# Cue points shown as coloured triangles on waveform
# Metadata fields correct
```

```sh
geany-progress done 8 \
  -r src/nfs/anlz_reader.h \
  -r src/nfs/anlz_reader.cpp \
  -r src/ui/track_meta_dialog.h \
  -r src/ui/track_meta_dialog.cpp \
  -w "ANLZ path derived from track file path; format is rekordbox-version-dependent — test with CDJ-2000NXS2 pendrives"
```

---

## Phase 9: Rekordbox Metadata Edit

Allow in-memory editing of rekordbox database fields with write-back optimised to
minimise pendrive rewrites.

### Background

Writing to a pendrive is destructive if done naively (rekordbox rewrites 16 kB pages).
Strategy: load the PDB into memory, apply edits, then write back only changed pages.
Edits in scope for this phase: comments, star rating, track title, and per-track
custom icons (stored in the `Color` / `Label` fields rekordbox already supports).

### Deliverables

**`RekordboxDbWriter`** (new `src/nfs/rekordbox_db_writer.h/.cpp`)
- Extends `RekordboxDb` with write capability.
- Reads the PDB page-by-page into a `QHash<uint32_t, QByteArray> pages_`.
- `setComment(uint32_t trackId, const QString& comment)` — marks the track's page
  dirty.
- `setRating(uint32_t trackId, int stars)` — 0–5, stored in the `Rating` column.
- `setLabel(uint32_t trackId, int colorIndex)` — rekordbox colour label (0–6).
- `save()` — writes back only dirty pages; if source is a mounted path, writes in
  4 kB aligned blocks.
- Optional `saveTo(const QString& path)` for writing to a copy (safe workflow).

**`TrackEditDialog`** (new `src/ui/track_edit_dialog.h/.cpp`)
- `QDialog` with:
  - `QLineEdit` for title and comment
  - `QSpinBox` 0–5 for star rating (rendered as star icons)
  - `QComboBox` for colour label (rekordbox's 7-colour palette)
  - "Save to DB" button — calls `RekordboxDbWriter::save()`
  - "Save Copy" button — calls `saveTo` variant

**Safety constraints**
- Never write while the pendrive's deck is actively playing (`NfsMountManager` tracks
  mount state — add an `isInUse` hint via deck play state).
- Show a confirmation dialog before any write: "Writing to pendrive — ensure no deck
  is reading from it. Proceed?"

### Key files
- `src/nfs/rekordbox_db_writer.h/.cpp` (new)
- `src/ui/track_edit_dialog.h/.cpp` (new)
- `src/ui/track_meta_dialog.cpp` (add "Edit" button to open edit dialog)

### Verification
```sh
# Open TrackMetaDialog, click Edit
# Change comment and rating, Save
# Re-open: new values persisted
# Rekordbox on another machine reads the edits correctly
# Hex-diff the PDB: only touched pages changed
```

```sh
geany-progress done 9 \
  -r src/nfs/rekordbox_db_writer.h \
  -r src/nfs/rekordbox_db_writer.cpp \
  -r src/ui/track_edit_dialog.h \
  -r src/ui/track_edit_dialog.cpp \
  -w "PDB3 page layout: each table row spans one or more 4096-byte pages — must parse row offset correctly before patching bytes" \
  -w "Do not edit live NFS mount; copy to tmpfs, edit, rsync back"
```

---

## Phase 10: Pendrive Cloning & Batch Title Fix

UI for copying a full rekordbox pendrive to local disk and back, plus the "remove
'(Original mix)' from all titles" batch operation.

### Background

Cloning is: copy `/Contents` and `/PIONEER` from the pendrive to a local directory.
Restoring is the reverse. The batch title fix modifies the `Title` column in PDB for
every track that matches the pattern.

### Deliverables

**`PendriveCloneDialog`** (new `src/ui/pendrive_clone_dialog.h/.cpp`)
- Source: pendrive mount path (from `QFileSystemWatcher` / `NfsMountManager`)
- Destination: local directory (QFileDialog)
- Progress bar driven by a `QThread` worker that uses `QFile::copy` per file
- Shows file count, bytes transferred, ETA
- "Clone to local" (pendrive → local) and "Restore to pendrive" (local → pendrive)
  modes, both with confirmation dialog
- Preserves file timestamps using `utimensat`

**NFS-based clone**
- If source is an NFS mount (deck pendrive), same logic applies — both `/Contents`
  and `/PIONEER` tree are copied.
- Note: NFS must be mounted first; this dialog integrates with `NfsMountManager`.

**Batch title fix** (`src/nfs/rekordbox_db_writer.cpp`)
- `removeBeatportSuffix(RekordboxDbWriter& db)` free function:
  - Iterates all tracks, strips ` (Original Mix)` (case-insensitive) from title.
  - Returns count of modified tracks.
- Exposed via `Menu > Tools > Remove "(Original Mix)" from titles…` with a
  source-selection dialog and confirmation.

### Key files
- `src/ui/pendrive_clone_dialog.h/.cpp` (new)
- `src/nfs/rekordbox_db_writer.cpp` (add `removeBeatportSuffix`)
- `src/ui/main_window.cpp` (Tools menu)

### Verification
```sh
# Insert rekordbox pendrive
# Tools > Clone Pendrive: select source + dest
# Clone completes; verify file count matches
# Restore from clone to a blank pendrive
# CDJ reads the restored pendrive correctly
# Tools > Remove (Original Mix): count shown, track titles updated
```

```sh
geany-progress done 10 \
  -r src/ui/pendrive_clone_dialog.h \
  -r src/ui/pendrive_clone_dialog.cpp \
  -r src/nfs/rekordbox_db_writer.cpp \
  -w "Cloning a live NFS pendrive is theoretically safe but risky if the deck writes during copy — warn user"
```

---

## Phase 11: DJ HUD (Second Monitor Window)

A full-screen companion window intended for a second display, showing now-playing
info, artwork, BPM, and a spectrum analyser.

### Background

Qt supports multi-screen with `QScreen` and `QWindow::setScreen()`. The HUD is a
separate `QWindow` that the user can move to a second monitor.

### Deliverables

**`HudWindow`** (new `src/ui/hud_window.h/.cpp`)
- `QWidget` (top-level, frameless optional) with dark background.
- Sections:
  - **Now playing**: large title + artist text (Deck 1 / Deck 2 side by side)
  - **Artwork**: current track art from `RekordboxDb`, scaled to ~256×256
  - **BPM**: large numeric display, updates from `network_->statusUpdated`
  - **Sync state**: coloured indicator per deck (synced / master / free)
  - **Spectrum analyser** (optional): renders a bar-chart spectrum from the
    beat grid widget's BPM pulse (synthetic spectrum based on beat timing, not
    audio). Use `QPainter` to draw N frequency bars animated by the beat phase.

**MainWindow integration**
- `Menu > View > HUD Window` toggles the HUD.
- `ConfigManager` saves `hud/screen_index` so it reopens on the same screen.
- HUD subscribes to same `network_` signals as the main window for live updates.

**Track art plumbing**
- `NowPlaying` currently has no art; add `setArtwork(QPixmap)` signal path from
  `TrackBrowser::trackSelected` through `MainWindow` to `HudWindow`.

### Key files
- `src/ui/hud_window.h/.cpp` (new)
- `src/ui/main_window.cpp` (menu toggle, signal wiring)
- `src/ui/now_playing.h/.cpp` (add setArtwork)

### Verification
```sh
# View > HUD Window: HUD opens
# Drag to second monitor if available
# BPM updates live from network
# Track art appears when track selected in browser
# Closing main window closes HUD
```

```sh
geany-progress done 11 \
  -r src/ui/hud_window.h \
  -r src/ui/hud_window.cpp \
  -r src/ui/main_window.cpp \
  -w "Spectrum bars are synthetic (beat-phase driven), not audio-derived — label clearly in UI"
```

---

## Phase 12: Playlist History & Session Export

Track what each deck plays during a session, support per-DJ splits, and export
human-readable tracklists.

### Background

Each time a new track starts on a deck (status packet shows new track ID), the
application can log it. The DJ change button starts a new "set". Export produces
a `.txt` or `.csv` with timestamps.

### Deliverables

**`SessionLog`** (new `src/session_log.h/.cpp`)
- `struct PlayEvent { QDateTime time; int deckId; QString title, artist, bpm; };`
- `struct DjSet { QString djName; QDateTime start; QList<PlayEvent> events; };`
- `SessionLog` accumulates `PlayEvent`s from `network_->statusUpdated` when title
  changes.
- `newSet(const QString& djName)` — closes current set, opens new one.
- `exportText(const QString& path)` — writes human-readable tracklist:
  ```
  DJ: teknopaul  2025-06-01 22:00
  22:00  [Deck 1]  Track Title — Artist  (128.0 BPM)
  22:04  [Deck 2]  Next Track — Artist   (130.2 BPM)
  ...
  ```
- `exportCsv(const QString& path)` — CSV variant.

**UI**
- New dock panel "Session" with:
  - DJ name `QLineEdit`
  - "New Set" button
  - Scrollable `QListWidget` of play events (auto-scrolls to latest)
  - "Export…" button → `QFileDialog` + `SessionLog::exportText`
  - Per-event "Notes" column — small `QLineEdit` inline per row for DJ comments
- `Menu > Session > New DJ Set…` and `Export Tracklist…` actions.

**Integration with TrackBrowser**
- When a track is selected (NowPlaying updates), `SessionLog::addEvent()` called
  from `MainWindow`.

### Key files
- `src/session_log.h/.cpp` (new)
- `src/ui/session_panel.h/.cpp` (new dock widget)
- `src/ui/main_window.cpp` (menu, wiring)

### Verification
```sh
# Connect to network (or run in demo mode)
# Session panel shows play events as decks change tracks
# Click "New Set", enter "DJ Name"
# Export produces correct timestamped tracklist file
# Notes field persists in exported file
```

```sh
geany-progress done 12 \
  -r src/session_log.h \
  -r src/session_log.cpp \
  -r src/ui/session_panel.h \
  -r src/ui/session_panel.cpp \
  -r src/ui/main_window.cpp \
  -w "Track change detection relies on title string comparison — NFS must be working for track names to be available"
```

---

## Phase 13: MIDI Routing

Show Pioneer players operating as MIDI controllers and route their MIDI output to
other software on the same machine.

### Background

When a Pioneer CDJ is connected via USB to the laptop and set to MIDI mode, it appears
as an ALSA MIDI device. The goal is: detect these devices, show them in the UI, and
provide a simple routing matrix to forward MIDI from a physical player to another
ALSA MIDI device (e.g., the software VDJ's ALSA sequencer port).

### Deliverables

**`MidiRouter`** (new `src/midi/midi_router.h/.cpp`)
- Uses ALSA sequencer API (`libasound`) or Qt-wrapped equivalent.
- Enumerates ALSA MIDI clients/ports with `snd_seq_client_info_t`.
- Opens a source and destination port and creates an ALSA subscription.
- Emits `Qt signals: midiClientAdded(QString name)`, `midiClientRemoved(QString)`.

**`MidiRoutingDialog`** (new `src/ui/midi_routing_dialog.h/.cpp`)
- Lists available ALSA MIDI inputs on the left (physical players).
- Lists ALSA MIDI outputs on the right (software synths, VDJ).
- Checkable routing matrix.
- "Apply" button creates/removes ALSA subscriptions.

**VDJ software MIDI target**
- Document (in dialog tooltip) how to configure the software VDJ as an ALSA MIDI
  target so a physical deck can control it.

**Integration**
- `Menu > Tools > MIDI Routing…` opens `MidiRoutingDialog`.
- `MainWindow` listens for `MidiRouter::midiClientAdded` to badge the toolbar.

### Key files
- `src/midi/midi_router.h/.cpp` (new)
- `src/ui/midi_routing_dialog.h/.cpp` (new)
- `src/ui/main_window.cpp`
- `CMakeLists.txt` (link `libasound`)

### Verification
```sh
# Connect CDJ via USB, set to MIDI mode
# MIDI Routing dialog shows CDJ as a MIDI input
# Route CDJ → software VDJ port
# Moving the jog wheel sends MIDI events to VDJ
```

```sh
geany-progress done 13 \
  -r src/midi/midi_router.h \
  -r src/midi/midi_router.cpp \
  -r src/ui/midi_routing_dialog.h \
  -r src/ui/midi_routing_dialog.cpp \
  -w "ALSA sequencer requires libasound2-dev; add to CMake find_package and deb Depends"
```

---

## Phase 14: Session Audio Recording

Record the mix output from the sound card to per-DJ MP3 files using ALSA/PulseAudio.

### Background

The DJ routes the mixer's record output into the laptop's line-in. The application
captures this PCM stream and encodes to MP3 (via `lame`) with automatic file splitting
on "DJ change" events. Tracklist timestamps in the MP3 tags link the recording to the
session log from Phase 12.

### Deliverables

**`AudioRecorder`** (new `src/audio/audio_recorder.h/.cpp`)
- Uses ALSA PCM API (`snd_pcm_open` capture mode) or PulseAudio simple API.
- Streams raw PCM to a `QThread` worker.
- Encodes to MP3 via `lame` CLI subprocess (pipe stdin → `lame -r - out.mp3`).
  - Alternative: link `libmp3lame` directly.
- `start(const QString& device, const QString& outputPath)` / `stop()`.
- Emits `levelChanged(float rms)` for a VU meter.

**`RecordingPanel`** (new `src/ui/recording_panel.h/.cpp`)
- Dock widget or toolbar extension:
  - ALSA device selector (`QComboBox` populated from `snd_device_name_hint`)
  - Record / Stop button
  - File path display (auto-named: `session_YYYYMMDD_HHmm_<djname>.mp3`)
  - VU meter (`QProgressBar` updated from `levelChanged`)
  - "Split (DJ change)" button — stops current MP3, starts a new one

**Integration with SessionLog**
- On `SessionLog::newSet(djName)`, `AudioRecorder::split(newPath)` automatically.
- Export embeds the MP3 filename in the tracklist.

### Key files
- `src/audio/audio_recorder.h/.cpp` (new)
- `src/ui/recording_panel.h/.cpp` (new)
- `src/ui/main_window.cpp`
- `CMakeLists.txt` (link `libasound` / PulseAudio, optional `libmp3lame`)

### Verification
```sh
# Physical cable: mixer record out → laptop line-in
# RecordingPanel: select ALSA capture device, click Record
# VU meter moves with audio
# Click "Split": new MP3 file created with correct naming
# Resulting MP3 is playable and timestamped
```

```sh
geany-progress done 14 \
  -r src/audio/audio_recorder.h \
  -r src/audio/audio_recorder.cpp \
  -r src/ui/recording_panel.h \
  -r src/ui/recording_panel.cpp \
  -w "lame subprocess approach avoids GPL linking concerns — ensure lame is in PATH or add to deb Depends" \
  -w "PulseAudio monitor source can capture mixer output without a cable — mention in UI tooltip"
```

---

## Summary

| Phase | Feature | Complexity |
|-------|---------|------------|
| 1 | About dialog, VDJ deck tint, NIC persistence | Small |
| 2 | VDJ master BPM ramp UI | Small–Medium |
| 3 | Auto-IP script & status widget | Small |
| 4 | .deb packaging with icon and proper deps | Small |
| 5 | Crash stability (double free) | Medium |
| 6 | Passive monitor mode | Medium |
| 7 | Rekordbox track browser (PDB, pendrive) | Large |
| 8 | Rekordbox metadata viewer (waveform, cues) | Medium |
| 9 | Rekordbox metadata edit (safe page-level writes) | Large |
| 10 | Pendrive cloning & batch title fix | Medium |
| 11 | DJ HUD second-monitor window | Medium |
| 12 | Playlist history & session export | Medium |
| 13 | MIDI routing (ALSA sequencer) | Medium–Large |
| 14 | Session audio recording to MP3 | Large |
