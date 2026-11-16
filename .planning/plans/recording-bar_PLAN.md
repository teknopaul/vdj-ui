# Recording bar: crash fix, lame, folder, compressor, one-line bottom bar

Source: `ai-prompts/recording-panel-ui.prompt.md`. Today the recording UI is a right dock (`RecordingPanel`, `src/ui/recording_panel.{h,cpp}`, 172 lines) on top of
`AudioRecorder` (`src/audio/audio_recorder.{h,cpp}`, ALSA capture thread -> `lame` process). Every phase ends with a green build and `ctest -R "level-meter|controls|recording"`
(plus the full suite at the end; `vdj-ui-audio-test` and `vdj-ui-trakcrate-panel-test` failed before this plan).

## What the code shows (do not re-derive)

1. **The crash.** `RecordThread::run` (`audio_recorder.cpp:34-95`) calls blocking `snd_pcm_readi`. `AudioRecorder::stopMonitor()` / `stopThread()` do `requestStop(); wait(3000); deleteLater()`.
   On a device that opens but delivers no data (e.g. `sysdefault:CARD=Xone43C,DEV=0`: a plug/hw capture that is not started or has no signal clock) `readi` never returns, the stop flag is never seen,
   `wait(3000)` times out and the `QThread` is deleted while running: Qt prints "QThread: Destroyed while thread is still running" and calls `abort()` = "Aborted (core dumped)".
   `default` and `pipewire` deliver data continuously, which is why they work. The monitor starts the thread **on every combo change** (`recording_panel.cpp:79`), so just selecting the device crashes.
   Other hazards in the same function: `set_channels(…, 2)` fails on multi-channel devices (the Xone:43C has more) and is reported only as "hw_params failed"; the return values of the `snd_pcm_hw_params_set_*`
   calls are unchecked; `snd_pcm_recover` loops forever on a dead device.
2. **lame** is started as an external process (`startLame`, `audio_recorder.cpp:200`); `CPACK_DEBIAN_PACKAGE_DEPENDS` (`CMakeLists.txt:90`) does not list it, nor does `snap/snapcraft.yaml` (stage-packages).
3. **Output folder** is fixed: `makeFilename` writes to `~/Music/` (`recording_panel.cpp:90`).
4. **Compressor**: nothing exists. `/usr/lib/ladspa/caps.so` (package `caps`) has `Compress` (mono) and `CompressX2` (stereo); `ladspa.h` is **not** installed here, so the host defines the few LADSPA structs itself.
5. The panel is a `QDockWidget` (`MainWindow::setupRecording`, `main_window.cpp:2561`); `shutdown()` stops a running recording (`main_window.cpp:2340`) but `closeEvent` (`:2325`) does not ask.
6. `LevelMeterWidget` (`level_meter_widget.h`) is a 260x64 two-bar meter with advice text underneath: too big for a one-line bar.

## Ground rules

- Never use git. Never use sudo (tell the human what to run). Stdlib/Qt only. Tests that need a sound card must `QSKIP` without one; use the ALSA `null`/`file` plugins through `ALSA_CONFIG_PATH`.
- Keep `AudioRecorder`'s signals (`levels`, `recordingStarted`, `recordingStopped`, `errorOccurred`, `monitorFailed`) so other code keeps working.
- Settings go through `ConfigManager` (`config_->loadValue/saveValue`), section `recording`.

---

## Phase 1 — Fix the crash: a capture thread that always stops

**Goal:** selecting any capture device, working or not, never aborts the program.

### Files
- **edit** `src/audio/audio_recorder.cpp` / `.h`
- **create** `tests/test_audio_recorder.cpp` (+ `tests/CMakeLists.txt`)

### Changes
1. In `RecordThread::run`: open with `SND_PCM_NONBLOCK`-free blocking mode but wait with `snd_pcm_wait(pcm, 100)` (or `poll`) before each `readi`, so the loop checks `stop_` every 100 ms even when no data arrives. After `snd_pcm_hw_params` call `snd_pcm_prepare` and `snd_pcm_start` explicitly.
2. Negotiate the format instead of assuming it: `snd_pcm_hw_params_set_channels_near` (ask for 2; if the device gives more channels, capture all and use channels 0 and 1; if it gives 1, duplicate to stereo), check every return value and emit `errorOccurred` with the failing call. Limit `snd_pcm_recover` to 5 consecutive failures, then report and stop.
3. **Never delete a running `QThread`.** One helper `static void stopAndRelease(RecordThread*& t)`: `requestStop()`, `wait(2000)`; if it is still running, detach it: `t->setParent(nullptr)`, connect `finished -> deleteLater` and drop the pointer (the thread deletes itself when it finally ends). Use it in `stopMonitor`, `stopThread` and the destructor. Disconnect all its signals first.
4. `startMonitor` must not block the GUI thread for the wait: the worst case is the 100 ms poll plus the 2 s bound above.
5. `monitorFailed` / `errorOccurred` carry the device name.

### Tests (`vdj-ui-audio-recorder-test`)
- With `ALSA_CONFIG_PATH` pointing at a generated config defining `pcm.silent { type null }` (capture returns zeros) : start a monitor, get `levels`, stop, assert no crash and `isMonitoring()` false.
- A config with `pcm.stuck { type file; slave.pcm null; infile "<fifo>"; format raw }` where the FIFO never gets data: `startMonitor("stuck")`, 300 ms later `stopMonitor()` returns in under 3 s and the process survives (the case that aborted). `QSKIP` if the file plugin is not available.
- A device name that does not exist emits `monitorFailed` and does not crash.

### Completion
```sh
geany-progress done 1 -r src/audio/audio_recorder.cpp -r tests/test_audio_recorder.cpp -w "Root cause: blocking snd_pcm_readi + deleting a running QThread after wait(3000) timed out; the thread now polls and is never deleted while running" -w "Not verified on the Xone:43C itself: ask the human to try it"
```

---

## Phase 2 — lame as a real dependency, and a clear message when it is missing

**Goal:** the package installs `lame`; the program says so if it is not there.

### Files
- **edit** `CMakeLists.txt` (`CPACK_DEBIAN_PACKAGE_DEPENDS` += `lame`, `CPACK_DEBIAN_PACKAGE_RECOMMENDS` = `caps`), `snap/snapcraft.yaml` (stage-packages: `lame`, `caps` if available for the base)
- **edit** `src/audio/audio_recorder.{h,cpp}`: `static bool lameAvailable(QString* path = nullptr)` (QStandardPaths::findExecutable)
- **edit** `README.md` / `doc` install notes

### Changes
- `AudioRecorder::start` fails early with `"lame is not installed (sudo apt install lame)"`; no thread is started.
- Test: with `PATH` emptied `start()` returns false and emits `errorOccurred` (add to the phase 1 test file).

### Completion
```sh
geany-progress done 2 -r CMakeLists.txt -r snap/snapcraft.yaml -r src/audio/audio_recorder.cpp -w "Snap: check that lame exists in the base's archive; if not, the snap needs a part that builds it"
```

---

## Phase 3 — Compressor: an in-process LADSPA host

**Goal:** an optional stereo compressor applied to the audio before `lame`.

### Files
- **create** `src/audio/ladspa_min.h` (the LADSPA structs and constants copied by hand: `LADSPA_Descriptor`, port descriptor bits, `LADSPA_Handle`; LGPL header content only as far as needed), `src/audio/ladspa_host.{h,cpp}`
- **edit** `src/audio/audio_recorder.{h,cpp}`: `setCompressor(bool)`; the chunk is processed on the capture thread
- **edit** `src/CMakeLists.txt`; link `dl`
- **create** `tests/test_ladspa_host.cpp` with a tiny test plugin built as a shared object in the test directory

### Changes
1. `LadspaHost::findCompressor()`: search `LADSPA_PATH` (default `/usr/lib/ladspa:/usr/local/lib/ladspa`) for the labels, in this order: `CompressX2` (caps.so), `sc4_1882` (swh), `dysonCompress_1403` (swh, mono: use two instances). Choose ports **by name and hints**, not fixed indices: audio in/out (two), then set sensible controls by name match (`threshold`, `ratio`, `attack`, `release`, `gain`/`makeup`), leaving the plugin default when a name is not found. Document the chosen defaults (about 3:1, -18 dB threshold, light makeup) in a table and in `doc/recording.md`.
2. `process(const int16_t* in, int16_t* out, size_t frames)`: convert to float, `run`, convert back with clipping; instance created at 44100 Hz on the capture thread, `activate` / `cleanup` handled.
3. `AudioRecorder::compressorAvailable()` (static, cached) for the UI; if the toggle is on while the plugin is missing, `errorOccurred("no LADSPA compressor found (install the 'caps' package)")` and recording continues uncompressed.
4. The meter keeps showing the **input** level (before compression).

### Tests
- A test plugin (a descriptor with one audio in, one out, one control "gain") compiled by CMake; `LADSPA_PATH` set to its folder; assert discovery by label, that a known gain is applied exactly, and that a missing label reports unavailable.
- Clipping/rounding at full scale.

### Completion
```sh
geany-progress done 3 -r src/audio/ladspa_effect.cpp -r src/audio/ladspa_min.h -r tests/test_ladspa_host.cpp -w "ladspa.h is not installed here: the structs are written by hand, check them against the LADSPA spec" -w "Control names of caps CompressX2 are matched by name: confirm by ear on the real recording"
```

---

## Phase 4 — The recording bar

**Goal:** replace the dock by one full-width bar at the bottom:

```
Recording ⚫ | [Capture v] | L,R VU meters |--- (stretches) | Monitor [x] | Compressor [x] | [Select Dir... v] | (⏺ Record) | (⏭ Split) | (⏹ Stop)
```
The dot is grey (U+26AB, "⚫" styled grey) when idle and red (U+1F534 "🔴" or a red `●`) while recording.

### Files
- **create** `src/ui/recording_bar.{h,cpp}` (replaces `RecordingPanel`; keep `makeFilename` as a static used by tests, taking the folder)
- **edit** `src/ui/level_meter_widget.{h,cpp}`: a compact mode (two thin horizontal bars, ~120x22, no advice text, clip light still clickable; `setCompact(bool)`); keep the existing tests green
- **edit** `src/ui/main_window.{h,cpp}`: `setupRecording()` builds a `QToolBar` (not movable, `Qt::BottomToolBarArea`, objectName `recordBar`) holding the bar; `recordAction_` toggles it; remove the dock and `recordDock_`
- **delete** `src/ui/recording_panel.{h,cpp}` after moving what is still used; update `src/CMakeLists.txt`
- **edit** `tests/test_controls.cpp` or **create** `tests/test_recording_bar.cpp`

### Changes
1. Widgets left to right, with `QHBoxLayout` margins 4/2, spacing 6, the meter with `Expanding` horizontal policy: title label, dot label, capture `QComboBox` (width-limited, elided), meter, Monitor checkbox, Compressor checkbox (disabled with a tooltip when `compressorAvailable()` is false), folder `QToolButton` with a menu: the current folder (elided), "Choose folder..." (`QFileDialog::getExistingDirectory`), and the last 5 folders; Record / Split / Stop buttons with the glyphs shown (text plus glyph, `⏺ Record`, `⏭ Split`, `⏹ Stop`). Record enabled when idle, Split and Stop only while recording.
2. Settings (section `recording`): `device`, `dir` (default `~/Music`, created when missing), `monitor`, `compressor`, `recent_dirs`. The capture list is refreshed when the combo is shown (a USB card plugged in later).
3. The file name uses the chosen folder: `session_YYYYMMDD_HHmm.mp3`; a name clash gets `_2`, `_3`. The folder is checked for writability before recording; a failure is a status bar message, not a crash.
4. Keep the monitor logic (restart on device / checkbox change, off when the bar is hidden, off while recording) but start it with a 150 ms single-shot so scrolling through the combo does not open each device in turn.
5. Remove the advice label and the hint text; the meter tooltip carries the advice (`zone()`).

### Tests
- Bar builds offscreen; Stop/Split disabled when idle; the dot is grey, then red after `recordingStarted` (drive the recorder's signals directly); the Compressor box is disabled when no plugin; the folder setting round-trips through a temporary `ConfigManager`; `makeFilename` clash numbering.

### Completion
```sh
geany-progress done 4 -r src/ui/recording_bar.cpp -r src/ui/main_window.cpp:2561 -r src/ui/level_meter_widget.cpp -w "RecordingPanel and its dock are gone; settings moved to config section 'recording'" -w "Look at it at 1280 px wide: the meter is the part that shrinks"
```

---

## Phase 5 — Warn before closing while recording, docs, final check

### Files
- **edit** `src/ui/main_window.cpp` (`closeEvent`, `:2325`)
- **create** `doc/recording.md`; **edit** `README.md`
- tests

### Changes
1. `closeEvent`: when `recorder_->isRecording()`, `QMessageBox::question` "A recording is running. Stop it and quit?" (buttons: Stop recording and quit / Keep recording); *Keep* ignores the close. Also guard the File > Quit action (it ends in the same path) and the session-end signal where possible.
2. `doc/recording.md`: the bar, the folder, the compressor (plugin search, defaults, how to install `caps`), lame, the crash fix and what to do if a device is silent (`arecord -l`, `arecord -D <name> -d 2 /dev/null`).
3. Full `ctest`; list anything still failing that was already failing.

### Tests
- A `QMessageBox` cannot be driven without a hook: add an overridable `bool MainWindow::confirmQuitWhileRecording()` (virtual-free: a `std::function<bool()>` member `quitWhileRecordingHook_` for tests) and test both answers.

### Completion
```sh
geany-progress done 5 -r src/ui/main_window.cpp:2325 -r doc/recording.md -w "Human retest: pick every capture device in the list (incl. the Xone) with the bar visible, record 10 s with and without the compressor, then close the window while recording"
```
