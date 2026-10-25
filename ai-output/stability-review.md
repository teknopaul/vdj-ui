# Stability review of the vdj-ui C++ code (2026-10-25)

Read-only review, no code was changed. Method: read the code that runs on, or is called from, other threads (libcdj's network threads, `std::thread`/`QThread` workers),
the code that owns objects by hand, the parsers of network input, and the code written or changed in this session. About a third of `src/` (18k lines) was read; the
widget code (`deck_widget`, `waveform_widget`, `beat_grid_widget`, `hud_window`, dialogs), `rb_presence`, `nfs_observer`, `rb_mode`, `midi_router`, `instrument_manager`
and `config_manager` were not. Findings are from reading, not from running: none is proven by a sanitizer yet (see "How to confirm").

Severity: **A** = can crash or corrupt memory in normal use, **B** = crashes on a specific sequence (quit, disconnect, a slow device), **C** = freezes the UI or fails
quietly, **D** = fragile, no known failing sequence.

## Update after the ASAN crash log (same day)

The crash you captured was **not** one of the findings below: `AlsaClock::loop()` (`src/midi/alsa_clock.cpp`) called `snd_seq_queue_status_alloca()` inside its `while (!quit_)` loop, which
runs every 2 ms. `alloca()` memory is only released when the function returns, so the clock thread's stack grew until it overflowed, a couple of minutes after the play button started the
clock (sooner under ASAN). Fixed by allocating once before the loop. Lesson for this class of bug: `*_alloca` macros in loops (the other uses in `midi_router`, `instrument_manager`,
`usb_config_dialog` are once per call or per client in a short enumeration, so they are fine).
Correction to B1: `MainWindow::shutdown()` (run from `closeEvent`, `main_window.cpp` ~1379) already disconnects the network before the destructor runs, so B1 only bites if the window is
destroyed without a close event. A1 (logger) is now addressed by the packet-logging option below.

## What is good

- `db_codec.cpp` `StreamParser`: every read is bounds-checked (`Reader::want`), lengths are capped (`kMaxFieldBytes`), arguments are limited to 12, a malformed
  stream ends the session instead of reading past the buffer. This is the code most exposed to the network and it is careful.
- `DbHandler` looks tracks up by id and returns "no such item" for an unknown id (`db_handler.cpp:114,348,359`); sessions keep copies, not references into the library.
  A deck that keeps its session across a pendrive swap gets stale menus, not a crash. `RbLibrary::load()` clears its caches first.
- `RbLibrary::artwork()` refuses path traversal (`safeRelativePath`). `AlsaClock`, `AudioPlayer`: mutex/atomics, threads joined in the destructor.
- `AudioRecorder`: samples cross threads through a queued signal and `QProcess` is written on the GUI thread.
- `NetworkManager::noteRemoteLoad`/`usbInUseBy` use a mutex; `statusStarted_`/`joining_` are atomic.

## Findings

### A1. `PacketLogger::log()` is not thread-safe, and is called from every libcdj thread

`src/debug/packet_logger.cpp:55-78`, `src/debug/packet_logger.h:36-40`.
`log()` appends to `QVector<PacketEntry> entries_` (and, when the 5000-entry buffer is full, does `entries_.remove(0, ...)`, a memmove of the whole vector) and writes `file_`, with
no lock. It is called from `onUpdate`, `onBeat`, `onDiscovery` (`network_manager.cpp:73,95,135`, three different libcdj threads, several times a second per deck) and from the
GUI thread (`USER`, `LINK` lines, `network_manager.cpp:39,169,...`). It records into `entries_` even when logging to file is off. `DebugPanel::refresh` and `main_window.cpp:1427`
read `entries()` (a reference to the live vector) on the GUI thread at the same time.
Failure: two threads append/remove at once, or the GUI iterates while a thread shifts the vector, so a heap corruption or a read of a freed `QString` shows up later as a
segfault, only while connected, intermittently, worse with more decks and with `--debug`. **Most likely candidate for "segfault while connected".**
Also: the remove-from-front is O(5000) on every log call once full (CPU cost on the network threads).
Fix: a mutex around `entries_`/`file_`; `entries()` returns a copy (or the panel reads a snapshot); a ring buffer (`std::deque`) instead of `remove(0, n)`.

### A2. `NetworkManager::teardown()` deletes the LINK export before it stops libcdj's threads

`src/network/network_manager.cpp:216-232` (calls `stopLinkExport()` at 219, then `vdj_stop_*` at 221-223) and `:333-337`, `:424-433`, `:319-326`.
The comment at 335 says "the update thread is stopped before it is deleted"; in `teardown()` the order is the opposite. `rawStatusPortPacket` (update thread) reads the plain
pointer `linkExport_` and calls `le->onStatusPortPacket()` (an `invokeMethod` on the object) while the GUI thread sets `linkExport_ = nullptr` and `delete le`.
`v_->raw_update_h = nullptr` (line 427) is a plain store and does not stop a call already in flight. `setLinkExportFolder()` (319) does the same stop/start while connected.
Failure: Disconnect (or a quit, or `setLinkExportFolder`) while a media query arrives, so a call on a destroyed `QObject`. Window is small, the consequence is a crash.
Fix: stop the update/beat threads first (or at least clear `raw_update_h` and wait), or make `linkExport_` an `std::atomic<LinkExport*>`/guard it with a mutex and `QPointer`.

### A3. `PendriveToolBar` timer polls `LinkExport::busyReason()` from the GUI thread every 0.7 s (added this session)

`src/ui/main_window.cpp` `updatePendriveBar()`; `src/network/link_export.cpp` `busyReason()` -> `nfsBytesServed()` -> `nfsd_bytes_read()` and `NetworkManager::usbInUseBy()`.
The NFS byte counter is written by libcdj's nfsd service thread. If libcdj keeps it as a plain 64-bit integer this is a benign torn read; I did not check
`nfsd_bytes_read`/`nfs_dirfs_bytes_read` in libcdj. `busyReason()` also has side effects (`lastNfsBytes_`, `lastNfsMs_`), so polling it changes what
"busy in the last 3 s" means. Low risk, but it is new code on a timer; note it when reading the crash log.

### B1. `~MainWindow()` frees `clock_` while the network threads can still call into it

`src/ui/main_window.cpp:319-323` and `:99` (`setBeatProbe` lambda uses `clock_`), `network_manager.h:53-57`.
`~MainWindow` runs `delete clock_` and returns; `network_` is a child `QObject`, destroyed afterwards, and only then `teardown()` stops the beat thread. In between, `onBeat`
(`network_manager.cpp:90`) calls `sequencerOffsetNow()` -> the lambda -> `clock_->beatOffsetMs` on freed memory (`clock_` is not nulled). Quit while connected.
Fix: `network_->disconnectFromLink()` (or `delete network_`) before `delete clock_`, and null the pointer.

### B2. Stop-with-timeout then `deleteLater()` on a thread that may still be running (the same bug class as the clone dialog)

- `src/midi/midi_map_input.cpp:350-363` `stop()`: `wait(1000)`, then `thread_->deleteLater()`, `worker_ = nullptr`; `start()` and the destructor then `delete map_` (lines 326-340)
  while a worker that did not stop in time is still reading `map_` in `dispatch()`. A timeout means `QThread: Destroyed while thread is still running` (abort) or a use-after-free.
- `src/midi/joystick_seq_bridge.cpp:93-120`: `running_` is set to true **inside** `run()`, so a `stop()` that arrives before `run()` starts is overwritten and the loop never ends;
  `wait(1000)` times out and the `QThread` (a child of the bridge) is destroyed while running. `stop()` never clears `thread_`, `start()` twice overwrites it (leak), and
  `setSeq()` writes `seq_` on the GUI thread while the worker reads it.
- `src/audio/audio_recorder.cpp:200-206` `stopThread()`: `wait(3000)` then `deleteLater()`; `snd_pcm_readi` can block indefinitely if the capture device goes away.
- Both worker flags are `volatile bool` (`midi_map_input.cpp:311`, `joystick_seq_bridge.cpp:70`): not atomic, not a valid thread flag in C++.
Fix (same as the clone dialog): `std::atomic<bool>` flags, set the flag before the thread starts, `quit()` + `wait()` with no timeout (or finish through the thread's `finished`
signal), delete the thread only from `finished`, and delete shared data (`map_`) only after the thread has finished.

### B3. `offerPort111Capability()` (added this session) runs a blocking dialog and a 2-minute wait inside the connect path

`src/ui/main_window.cpp` ~710 (`box.exec()`) and ~719 (`setcap.waitForFinished(120000)`). The signal chain is `LinkExport::startNfs()` -> `portmapNotBound` ->
`linkExportNeedsPort111` -> this slot, all synchronous inside `LinkExport::start()` <- `NetworkManager::startLinkExport()` <- `connectToLink()`. While the nested event loop runs,
Network > Disconnect (or a quit) is processed: `teardown()` runs `vdj_destroy(v_)` and when the dialog returns `startLinkExport()` continues with `v_->raw_update_h = ...` on freed memory.
Before my change the warning used `box->open()` (non-blocking), which was safe. Fix: queue the call (`QTimer::singleShot(0, ...)` or a `Qt::QueuedConnection`) and run `setcap` with `QProcess` + signals.
It does not show on your machine now because `ip_unprivileged_port_start` is 111.

### B4. `DbServer::onDbConnection` hands a raw `Client*` to a lambda that can outlive the client

`src/dbserver/db_server.cpp:155-161,165-172`. `readyRead` is connected with `[this, raw]`; `dropClient()` (on `disconnected`) removes the `shared_ptr<Client>`, which frees it, but
the socket is only `deleteLater()`ed, so a late `readyRead` would call `readClient()` on freed memory. Qt normally emits `readyRead` before `disconnected`, so this is unlikely, not impossible.
Fix: look the client up from the socket in the lambda (`clients_.value(s)`), or disconnect the socket's signals in `dropClient()`.

### B5. Uncaught C++ exceptions end the process

There is no `try`/`catch` in the program. `encodeAppend` throws `std::invalid_argument` (`db_codec.cpp:151,180`) and is called from the `readyRead` slot (`db_server.cpp:239`). Qt does not
catch exceptions in slots, so a handler that builds a message with more than 12 arguments, or a blob argument without its length number, aborts the whole app (SIGABRT). Today the
handlers build consistent messages, so this is latent. Fix: catch around the reply loop in `readClient`, log it, close that session.

### B6. `NetworkManager::connectToLink` pumps the event loop inside libcdj's join

`src/network/network_manager.cpp:26-42,184`. `onProgress` calls `processEvents(ExcludeUserInputEvents)` from inside `vdj_exec_discovery()` for about four seconds. Timers, queued signals and
`QTimer::singleShot`s run in that window (the sequencer timer, AutoIP, the new pendrive timer). Anything in them that touches `network_` while `v_` is half set up, or a quit/`teardown()` triggered
by a non-input event, re-enters the manager. Not seen failing; it is the kind of thing that fails once in a hundred joins.

### C. UI freezes (not crashes)

All on the GUI thread, with a timeout that is paid in full when the peer is gone: `XdjNfs::fetchFile`/`fileSize`/`mountExport` (synchronous UDP RPC with `kTimeoutMs`, per 8 kB chunk, from
`TrackBrowser::openRemoteDeck`); `NfsMountManager::unmount` `waitForFinished(5000)` per mount at shutdown (`nfs_mount_manager.cpp:81`); `AudioRecorder::stopLame` up to 8 s
(`audio_recorder.cpp:199`); `AutoIpManager::stop()` up to 5 s (`auto_ip_manager.cpp` ~242, `terminate()` on a root-owned pkexec process cannot work, so it always waits for the stop file, about 1 s);
`DbServer` writes two log files (`flush()` each) per request on the GUI thread. `PacketLogger` flushes the file on every entry.

### D. Smaller points

- `link_export.cpp:44-47`: `nc.root` points into a local `QByteArray` and `nc.exports` into a local array; libcdj's `nfsd_start` copies the root (`strdup` in `nfs_dirfs_open`) but keeps `d->cfg = *cfg`
  (`nfsd.c:174`), so `cfg.root`/`cfg.exports` dangle after `startNfs()` returns. Safe only while nothing reads `d->cfg.root` later.
- `XdjNfs::fetchFile` ignores `out.write()` errors (a full disk gives a truncated file that is renamed as complete if the byte count still matches).
- `NetworkManager::onUpdate` starts the status thread (`startStatusThread`) from the update thread while the GUI thread can call `teardown()`; create/stop race, rare.
- `MainWindow` destructor does not stop `midiIn_`'s thread safely (B2) or the recorder; relies on child-object order. `main_window.cpp` is 1670 lines with many lambdas capturing `this`
  and raw widget pointers (`deckAt()` results) used later from timers; I did not audit them one by one.
- `vdj_init_iface(const_cast<char*>(iface.toUtf8().constData()))` (`network_manager.cpp:162`) passes a pointer into a temporary; fine today because libcdj only reads it during the call.

## How to confirm (suggested, nothing run)

1. **ThreadSanitizer build** (`-fsanitize=thread`, a new build directory), connect to the decks for a minute: it should report A1 (PacketLogger) and A2/B1 immediately. ASAN (what you are running) only
   shows the damage after a race has corrupted the heap, so it may not name the cause.
2. If the ASAN log shows `QVector`/`QString`/`QArrayData` frames from `PacketLogger::log`, A1 is it.
3. `--debug` increases A1's exposure (more file writes and panel refreshes); a crash that only happens with `--debug` points at A1.

## Suggested order of work

1. A1 (lock + snapshot in `PacketLogger`), small and very likely the crash.
2. A2 + B1 (shutdown/disconnect order in `NetworkManager`/`~MainWindow`).
3. B3 (make my `setcap` popup non-blocking).
4. B2 (one shared "stop a worker thread" helper for the MIDI, joystick and recorder threads, as the clone dialog now does).
5. B4/B5, then the freezes in C.
6. Add a ThreadSanitizer test target for `NetworkManager` with a fake `vdj_t` if possible.
