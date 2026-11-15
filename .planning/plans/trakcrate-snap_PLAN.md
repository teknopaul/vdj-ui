# trakcrate-ui Snap (step 1: audio and USB only)

Source prompt: `ai-prompts/snap-distribution.prompt.md`.

## Goal

Ship `trakcrate-ui` (the library window of the vdj-ui binary, started as `vdj-ui --trakcrate` or through the `trakcrate-ui` symlink, see `src/main.cpp:14`) as a strictly confined Snap.

## Scope

- **In:** dependencies, a shared library location, audio, USB media.
- **Out:** networking, AutoIP, NFS or port 111, joystick, MIDI. `vdj-ui` stays a normal (deb) install until its networking can be solved, so the Snap and the deb must work on the same data.
- Never use git (the user handles all version control).

## Decisions already made

1. **The library lives in the real home directory, in a visible (non-hidden) folder.** The default is `~/trakcrate`, which is also the CLI default (`src-cli/script/usr/bin/trakcrate:316`). The `home` plug gives a Snap access to non-hidden files in the real home, and nothing in a hidden folder.
2. **The library is the shared database.** Everything that must be shared is already inside it: tracks, playlists, `user-settings/`, `stretchers/`, `.trakcrate-lock`, `.trakcrate-history`. The lock file already stops two instances (Snap and deb, GUI and CLI) from editing at once.
3. **Per-install state is not shared.** The UI config (`ConfigManager`, `trakcrate/library` key and so on) and the stretch cache stay private to each install. A Snap sees `$HOME` as `~/snap/trakcrate-ui/<rev>`, and that is fine for these files.
4. **The non-Snap build gets the same default.** A first run with no remembered library offers `~/trakcrate`, and the docs recommend it for both.

## Facts about the current code (verified)

- Library path is remembered as config `trakcrate/library` (`src/ui/trakcrate_panel.cpp:343,383`). `ConfigManager::configDir()` (`src/config/config_manager.cpp:48`) uses `/etc/vdj-ui` if writable, else `QDir::homePath()/.config/vdj-ui`.
- `QDir::homePath()` and `$HOME` are rewritten inside a Snap. `$SNAP_REAL_HOME` holds the true home directory.
- Pendrives are found by scanning `/media/$USER` and `/run/media/$USER` (`src/export/pendrive_registry.cpp:53-60`), watched with `QFileSystemWatcher` (`pendrive_registry.h:57`). The same scan is also used from `src/ui/mounted_sticks.cpp` and `src/ui/trakcrate_export_dialog.cpp`.
- The exporter stages in `/dev/shm`, then `$XDG_RUNTIME_DIR`, then the system temp directory (`src/trakcrate/exporter.cpp:37,406`). A Snap may not write directly to `/dev/shm`; it only gets `/dev/shm/snap.<name>.*`.
- Audio goes through libpulse / libpulse-simple (`src/CMakeLists.txt:178`). The library window uses it for track preview.
- Build dependencies: Qt6 (or Qt5.15) Widgets and Network, FFmpeg (`libavformat libavcodec libavutil libswresample`), `libpulse`, `libpulse-simple`, `samplerate`, optional `aubio`, `rubberband`, `soundtouch` (`src/CMakeLists.txt:283-294`).
- The binary is one executable for both apps, so the trakcrate Snap still contains the network code. It simply gets no network plugs.

## Phases

Each phase fits in one Sonnet context. Do them in order. After each phase run the unit tests (`ctest` in the existing `build/` directory) and the geany-progress command at the end of the phase.

---

### Phase 1: Real-home helper and default library location

**Read:** `src/ui/trakcrate_panel.cpp` (lines 330-390), `src/config/config_manager.cpp`, `src/trakcrate/stretch_cache.cpp:40-50`, `src/trakcrate/stretch_external.cpp:175-190`, `src-cli/script/usr/bin/trakcrate:295-320`, `doc/trakcrate/gui.md`.

**Do:**

1. Add a small header-only helper (suggest `src/trakcrate/real_home.h`, no Qt in it, so the CLI-side C++ can use it): `std::filesystem::path realHome()` returns `$SNAP_REAL_HOME` if set, else `getpwuid(getuid())->pw_dir`, else `$HOME`.
2. Add `defaultLibraryDir()` to the same header: `realHome() / "trakcrate"`.
3. In `TrakcratePanel`, when there is no remembered library (or the remembered folder no longer exists), do not stay empty: show the existing "open library" flow with the dialog starting at `defaultLibraryDir()`. If `defaultLibraryDir()` exists, open it silently as the last library does. If it does not exist, offer to create it. Keep the current behaviour for a remembered library.
4. The file dialogs in the library window that start in `QDir::homePath()` should start in `realHome()` instead (grep `homePath` under `src/ui/trakcrate_*` and `src/ui/mounted_sticks.cpp`).
5. Check `stretch_cache.cpp` and `stretch_external.cpp`: they use `$XDG_CACHE_HOME` / `$XDG_CONFIG_HOME` / `$HOME`. Under a Snap those point at the private directories, which is intended (decision 3). Add a comment saying so, no code change. The `stretchers/` folder inside the library remains the shared place for user stretchers (`defaultStretchDirs` already reads it).
6. Add a unit test next to `tests/test_trakcrate_panel.cpp` (look at how the existing tests are registered in `tests/CMakeLists.txt`): `realHome()` honours `SNAP_REAL_HOME`, and falls back when it is unset (use `setenv`/`unsetenv` around the call).
7. Update `doc/trakcrate/gui.md`: a short "Where the library lives" section recommending `~/trakcrate`, saying that the same folder can be used by the Snap, the deb and the CLI, and that the lock stops two programs editing at once. Run the `mdalign` skill on any table you touch.

**Do not:** move or migrate any existing library, change the CLI, or touch `vdj-ui` (non-trakcrate) code paths.

**Done when:** tests pass; running `./build/src/vdj-ui --trakcrate` with `SNAP_REAL_HOME=/tmp/x` and an empty config offers `/tmp/x/trakcrate`.

```sh
geany-progress done 1 -r src/trakcrate/real_home.h -r src/ui/trakcrate_panel.cpp -r doc/trakcrate/gui.md -w "Default library is ~/trakcrate; existing remembered libraries are untouched"
```

---

### Phase 2: Snap skeleton that builds and starts

**Read:** `CMakeLists.txt`, `src/CMakeLists.txt` (lines 170-345, in particular the `trakcrate-ui` symlink), `data/trakcrate-ui.desktop`, `make-deb.sh` (for the dependency list that already works), `src/main.cpp`.

**Do:**

1. Create `snap/snapcraft.yaml` (the standard location, so `snapcraft` works from the repo root):
   - `name: trakcrate-ui`, `base: core24`, `confinement: strict`, `grade: devel`, version taken from the `version` file (`adopt-info` with `craftctl set version=$(cat version)` in `override-pull`, or hard-code and say so).
   - Part `trakcrate-ui`: `plugin: cmake`, `source: .`, `build-packages` for Qt6 widgets/network dev, `libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libpulse-dev libasound2-dev libsamplerate0-dev pkg-config`; `stage-packages` the runtime libraries including `libpulse0`, the FFmpeg runtime libs and `libsamplerate0`. Add `libaubio-dev`, `librubberband-dev` and `libsoundtouch-dev` (and runtime libs) so that BPM detection and time-stretch work. Use the `kde-neon-6` extension for Qt6; if it is not available for the `core24` base, fall back to staging the Qt6 packages and say so in a comment.
   - Build option: `-DCMAKE_BUILD_TYPE=Release`. Check the root `CMakeLists.txt` for options that skip tests or `install(CODE ...)` blocks (lines 54-58, probably icon cache or setcap steps) that must not run in the Snap build. If needed, guard them with an `if(NOT SNAPCRAFT_BUILD)`-style option and pass it from the YAML.
   - `apps: trakcrate-ui` with `command: usr/bin/vdj-ui --trakcrate` (or the symlink path, whichever the install puts there), `desktop:` pointing at the installed desktop file, `plugs: [home, desktop, desktop-legacy, wayland, x11, opengl, audio-playback, removable-media]`. Nothing else. No `network*` plug in this step.
2. Create `snap/gui/trakcrate-ui.desktop` (copy of `data/trakcrate-ui.desktop` with `Exec=trakcrate-ui` and `Icon=${SNAP}/meta/gui/trakcrate-ui.png`) and copy a 128px icon to `snap/gui/trakcrate-ui.png`.
3. Add a `README` section or `doc/trakcrate/snap.md` stub titled "Building the Snap" (commands: `snapcraft`, `sudo snap install --dangerous trakcrate-ui_*.snap`, and the `snap connect` lines for the interfaces that are not auto-connected).
4. Build it if `snapcraft` is available (`snapcraft --destructive-mode` on the host is acceptable). If it is not installed, do not install it; write down in the progress warning that the YAML is unbuilt.

**Do not:** add network plugs, change C++ code, or attempt the store listing.

**Done when:** `snap/snapcraft.yaml` validates (build passes, or the missing tool is reported), and the app starts with the library window. A missing sound or USB feature is expected at this stage.

```sh
geany-progress done 2 -r snap/snapcraft.yaml -r snap/gui/trakcrate-ui.desktop -w "Snap built only if snapcraft is installed; check the warning in the progress file" -w "Strict confinement, no network plugs yet"
```

---

### Phase 3: Audio

**Read:** `src/audio/` (`stream_player.*`, the preview player used by the library window), `src/CMakeLists.txt:176-181`, `src/ui/mix_previewer.cpp` (only the output-device code), `snap/snapcraft.yaml`.

**Do:**

1. Find out how audio output is opened (PulseAudio simple API, device name, any `/dev/snd` or `/proc/asound` access, `PULSE_SERVER` use). List every path the code touches outside the library (grep `"/dev/snd"`, `"/proc/asound"`, `pa_simple_new`, `pa_context_new`).
2. Make sure the Snap plug list covers it: `audio-playback` is enough for Pulse or PipeWire-pulse. Add `alsa` only if the code really opens ALSA PCM or `/dev/snd` directly (the MIDI sequencer is out of scope). Document the decision in `snapcraft.yaml` comments.
3. If the code enumerates sinks and fails when the list is empty or access is denied, make sure that error shows a clear message in the status line and does not crash. Fix only that.
4. Stage the Pulse client libraries (`libpulse0`, `libpulse-mainloop-glib0` if used, `libasound2-plugins` so ALSA apps are redirected to Pulse).
5. Add a manual test list to `doc/trakcrate/snap.md`: install, play a preview, change the output device, use headphones cue if present, play two tracks in a row.
6. Run a quick check with the real binary: `snap run trakcrate-ui`, start a preview, and check `snap connections trakcrate-ui` and `journalctl -k | grep DENIED` for AppArmor denials. Record any denial verbatim in the progress file as a warning.

**Do not:** touch the MIDI or `AlsaClock` code, or the recording code (`audio_recorder.cpp` is `vdj-ui` only).

**Done when:** preview audio plays from the Snap on the user's desktop, with no AppArmor denials related to sound, or every denial is documented.

```sh
geany-progress done 3 -r snap/snapcraft.yaml -r doc/trakcrate/snap.md -w "Verified only on this machine's sound server (state which: PulseAudio or PipeWire)"
```

---

### Phase 4: USB media

**Read:** `src/export/pendrive_registry.cpp` and `.h`, `src/ui/mounted_sticks.cpp`, `src/ui/trakcrate_export_dialog.cpp`, `src/trakcrate/exporter.cpp` and `.h` (the staging code near lines 37 and 400-415), `src/tools/usb_watch.cpp` (only to decide whether the library window uses it).

**Do:**

1. **Plug and mount points.** Confirm `removable-media` gives access to both `/media/$USER` and `/run/media/$USER` on Ubuntu (udisks mounts under `/media/$USER`). It does not auto-connect on all systems, so document `sudo snap connect trakcrate-ui:removable-media` in `doc/trakcrate/snap.md`, and make the "no pendrive found" message in the export dialog mention it when `$SNAP` is set (read `qEnvironmentVariableIsSet("SNAP")`). Keep the wording short.
2. **Username.** `PendriveRegistry::scanPhysical` reads `$USER`/`$LOGNAME`; check both are available in a Snap (they are passed through), and fall back to `getpwuid` rather than `QDir::home().dirName()`, which would be wrong under a Snap (home dir name is the revision number). Reuse the helper from phase 1 or add a sibling `realUserName()`.
3. **Staging directory.** In `exporter.cpp` the order is `/dev/shm`, then `$XDG_RUNTIME_DIR`, then temp. Writing to `/dev/shm` directly is denied for a Snap. Use `access(W_OK)` or an actual create attempt (not only `is_directory`) to choose, and when `$SNAP` is set prefer `$XDG_RUNTIME_DIR`, which is private to the Snap and tmpfs. Add a test for the selection logic if it can be isolated (`stagingBase` is already a field).
4. **Watching.** Check that `QFileSystemWatcher` on `/media/$USER` works under confinement (inotify is allowed). If a stick is mounted after the app starts and the watcher misses it, fix with the existing periodic refresh (`watchTimer_`) rather than new code.
5. **Writes.** An export to a pendrive creates files and deep folders and then syncs. Check that `fsync`, `rename` across the stick, and removing the stick (unmount / eject from the app, if offered) do not need an extra interface. If the app calls `udisksctl` or `umount`, list it: a strict Snap cannot call them, so hide or disable that button when `$SNAP` is set and tell the user to eject from the file manager.
6. Add to the manual test list in `doc/trakcrate/snap.md`: insert a FAT32 stick, see it appear, export a small playlist to it, eject from the file manager, then put the stick into a deck or run the rekordbox reader tests if available.

**Do not:** use `system-observe`, `mount-observe` or `hardware-observe` unless a failing case proves they are needed; if one is, note it as a store-review risk in the warning.

**Done when:** a stick mounted by the desktop is listed and an export to it completes from the Snap, with the staged files written under the Snap-private runtime directory.

```sh
geany-progress done 4 -r src/export/pendrive_registry.cpp -r src/trakcrate/exporter.cpp -r doc/trakcrate/snap.md -w "removable-media may need a manual snap connect; documented" -w "Eject/unmount from the app is disabled in the Snap if it relied on external tools"
```

---

### Phase 5: Shared-data verification and finish

**Read:** `doc/trakcrate/snap.md`, `doc/trakcrate/gui.md`, `src/ui/trakcrate_scripts.cpp` (scripts section: lines 240-310), `TRAKCRATE-FORMAT.md` (headings only).

**Do:**

1. **Two installs on one library.** With the Snap installed and the deb (or the build tree) run as normal user, open `~/trakcrate` in both. Check: a playlist created in one appears in the other (the app already watches the folder); a second instance gets the lock message rather than corrupting anything; settings in `user-settings/` and tracks' `.trakcrate-history` are read by both.
2. **Scripts and external tools.** `trakcrate_scripts.cpp` runs user scripts and an editor (`script_editor` config) and looks for the `trakcrate` command with `findExecutable`. In a strict Snap these external programs are not reachable. Make the Scripts menu show a clear one-line explanation when `$SNAP` is set instead of failing, and list the limitation in `snap.md`. The same goes for external stretchers (`stretch_external.cpp`): the built-in engines still work, the external ones do not.
3. **Documentation.** Finish `doc/trakcrate/snap.md`: install, interfaces to connect, where the library lives, limits of the Snap (no scripts, no external stretchers, no network, no eject), and the plan for `vdj-ui` ("vdj-ui remains a deb until the networking questions are solved; both can share `~/trakcrate`"). Link it from `README.md` features section and from `doc/trakcrate/gui.md`. Run `mdalign` on new tables.
4. Add the remaining items from the prompt (networking, joystick, MIDI, ALSA seq) as a "Not yet" list at the bottom of `snap.md` pointing at `LINUX_NETWORKING.md`, so the next plan has a starting point. Do not plan them here.
5. Final run of the unit tests and a review of `snap/snapcraft.yaml` for plugs that are not needed.

**Done when:** the checks in 1 pass by hand (record the results in the progress file), the docs are linked, and `snapcraft.yaml` lists only the plugs the app uses.

```sh
geany-progress done 5 -r doc/trakcrate/snap.md -r snap/snapcraft.yaml -r README.md -w "Snap has no scripts menu, external stretchers, or eject; vdj-ui stays a deb" -w "Store submission (grade stable, name registration, interface approval) not started"
```

---

## Open risks

- **Qt6 extension:** `kde-neon-6` on `core24` may not match the Qt version CMake finds; the fallback is staged Qt packages, which makes a bigger Snap.
- **Binary contains the network code:** the Snap is bigger than needed and `main.cpp` still links the vdj-ui code. A later plan could add a trakcrate-only CMake target; this plan deliberately avoids it.
- **Interface approval:** `removable-media` does not auto-connect for most store snaps. Users run one `snap connect` command, or the publisher asks the store for auto-connect.
- **AppArmor denials** are only known after testing on real hardware; phases 3 and 4 record any in the progress file.
