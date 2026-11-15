# as_master_difflock Plan

## Overview

`difflock` keeps the ALSA sequencer a fixed offset from the beats of a real deck we follow. This plan adds
the mirror feature: when real decks follow **us** (the VDJ is master), the beat packets we send on the
Prolink network must sit a user-chosen number of milliseconds away from the ALSA MIDI clock beat.

Source prompt: `ai-prompts/as_master_difflock.prompt.md`.

### Findings from reading the code (do not re-derive)

- **vdj-ui never sends beat packets automatically.** The only caller of `NetworkManager::broadcastBeat()`
  (`src/network/network_manager.cpp:306`, wraps `vdj_broadcast_beat(v_, float bpm, uint8_t barPos)`) is the
  "Send Beat" button (`DeckControls::broadcastBeat_`, `src/ui/deck_controls.cpp:45`) via
  `broadcastBeatClicked` -> `DeckArea` -> `MainWindow` (`main_window.cpp:528`), plus
  `MainWindow::broadcastFromActiveDeck()` (`main_window.cpp:2290`, reachable from the joystick action
  `broadcast_beat`, `joystick_config_dialog.cpp:22`).
- `adj` sends beats from a `tick_handler` called by the sequencer loop every beat
  (`references/adj/src/libadj.c:319-322`, `adj_vdj.c:517 adj_vdj_beat()`). `vdj_broadcast_beat`
  (`parts/libcdj/src/src/c/vdj.c:410`) stamps `v->last_beat`, sets `v->bpm`, advances/sets `v->bar_index`
  (`bar_pos` 1..4 sets it, 0 auto-increments), builds the packet and sends it with `vdj_sendto_beat`. Its own
  comment says it "should run as close as possible in time to the beat played by midi instruments".
- `AlsaClock` (`src/midi/alsa_clock.{h,cpp}`) has a 2 ms polling thread that queues MIDI clock pulses two beats
  ahead; it has **no beat callback**. It does expose what we need to schedule from outside:
  `positionBeatsExact(int64_t* steadyNs)` (fractional beats since `start()`, with the matching
  `steady_clock` ns), `effectiveBpm()`, `barPos()` / `barBeats()` (bar label incl. whole-beat offset),
  `running()`. Pure helpers live in `src/midi/seq_bar.{h,cpp}` (`barPos`, `beatsToNextBeat`, `beatsToWallMs`),
  with tests in `tests/test_seq_bar.cpp`.
- `DiffLock` sign convention (`src/midi/diff_lock.h`, tooltip in `diff_lock_bar.cpp:33`): positive target = the
  sequencer's beat comes **before** the deck's. For the new setting use the same meaning so the two read
  alike: **positive `as_master_difflock_ms` = our beat packet is sent that many ms after the MIDI clock beat
  (the sequencer is early); negative = packet is sent before the MIDI beat.**
- The Difflock UI lives in `DiffLockBar` (`src/ui/diff_lock_bar.{h,cpp}`) inside the VDJ (local) `DeckWidget`;
  the VDJ row's `DeckControls` is where "Send Beat" sits. Settings persist via
  `config_->saveValue(section, key, value)` / `loadValue` (e.g. `dj/sync_calculated_bpm`, `main_window.cpp:1551`).
- The VDJ's bar position shown in the UI is already the sequencer's (`updateSequencerPlayhead`,
  `main_window.cpp:2279`), so the packet bar position must come from `AlsaClock::barPos()`, not the hidden
  `barPosSpin_`.
- Test conventions: QtTest executables registered in `tests/CMakeLists.txt` (see `vdj-ui-seqbar-test`,
  `vdj-ui-difflock-test`), linking `vdj-cdj`, with `QT_QPA_PLATFORM=offscreen`.

### Design

Beats are driven by **ALSA echo events** scheduled on the sequencer queue and delivered back to our own
client (no separate timing thread). A tick-scheduled echo per beat wakes the clock loop half a beat early; it
then schedules a real-time echo at the exact beat time plus `as_master_difflock_ms` (ns resolution, unlike
96 PPQ ticks); when that arrives, the beat packet is sent. Details in phases 1-2.

1. `master_beat_math` (pure): tick/real-time/pack helpers, unit tested.
2. `AlsaClock`: beat port, echo scheduling, input handling and a `BeatCallback`.
3. `MainWindow`: callback -> `NetworkManager::broadcastBeatQuiet`; offset from a new spinner.
4. UI: "Send Beat" button removed; replaced by a labelled integer spinner persisted in config.

Beats are sent whenever the sequencer is running, regardless of who is master (like `adj`); the offset only
matters when real decks follow us.

Everything must build and `ctest` pass at the end of each phase. Never use git (project rule).

---

## Phase 1 — Pure offset/echo maths + tests

**Goal:** the timing maths for an echo-event design, with no ALSA, threads or widgets.

### Design (replaces the earlier thread-and-spin idea)

ALSA's sequencer can deliver an event **to our own client** when the queue reaches a time (`SND_SEQ_EVENT_ECHO`,
dest = our client + a new writable input port). The kernel timer wakes us; we do not time anything ourselves.
Two kinds of echo, because ticks are only 96 PPQ (about 5 ms at 120 BPM: too coarse for a ms offset) while
real-time scheduling is ns-exact but does not follow tempo changes:

1. **Beat echo (tick-scheduled).** Once per beat the clock loop queues an echo at the tick of the *next but one*
   beat minus half a beat (tick `beat*96 - 48`), alongside the MIDI clock pulses it already queues two beats ahead
   (`AlsaClock::loop`, `alsa_clock.cpp:399`). Being tick-based it follows tempo changes, nudges and restarts for free
   (`stopLocked`/`restart` already `snd_seq_remove_events` everything on the queue).
2. **Send echo (real-time scheduled).** When a beat echo arrives, read the queue's exact position
   (`positionBeatsExact`), compute the queue real time of the upcoming beat `b` plus `as_master_difflock_ms`, and
   queue a second echo with `snd_seq_ev_schedule_real` at that time, carrying `b` and the bar position in the
   event's `data.raw32`. When that echo arrives, call the sink. Offset 0 still goes through both stages so every
   offset takes the same path. Positive offsets simply land after the beat, negative before it (half a beat of
   lead covers offsets up to about ±0.45 beat).
   If the tempo changes in the half beat between the two echoes the result is off by at most that change times the lead;
   the plan accepts this and the next beat corrects it.

### Files

- **create** `src/midi/master_beat_math.h`, `src/midi/master_beat_math.cpp`
- **edit** `src/CMakeLists.txt` (next to `midi/diff_lock.cpp`, line ~66)
- **create** `tests/test_master_beat_math.cpp`; **edit** `tests/CMakeLists.txt` (copy the `vdj-ui-seqbar-test` block,
  lines ~263-266, name `vdj-ui-master-beat-math-test`, keep `QT_QPA_PLATFORM=offscreen`)

### API (namespace `vdj`)

```cpp
// Tick (96 PPQ) at which the beat echo for whole beat `beat` must be queued: beat*96 - 48.
int64_t beatEchoTick(int64_t beat);
// Queue real time (ns) at which to queue the send echo for whole beat `beat`:
//   realNowNs + (beat - beatsNow) * 60e9 / bpm + offsetMs * 1e6
// beatsNow/realNowNs are one consistent reading (positionBeatsExact). offset clamped to +/-0.45 beat period.
// Returns false for bpm <= 0 or beatsNow < 0.
bool sendEchoRealNs(double beatsNow, int64_t realNowNs, double bpm, int offsetMs, int64_t beat, int64_t* outNs);
// Clamp helper exposed for the UI range check.
int clampAsMasterOffsetMs(int offsetMs, double bpm);
// Packing the beat number and bar position into / out of the two 32-bit event words.
void packBeatEcho(int64_t beat, int barPos, uint32_t out[2]);
void unpackBeatEcho(const uint32_t in[2], int64_t* beat, int* barPos);
```

Bar position for beat `b` is `barPos(barBeatsNow + (b - beatsNow))` from `seq_bar.h`; do this in the clock code
that calls these helpers, using the existing `barBeats()`.

### Tests (QtTest, pure arithmetic)

Tick for beats 0, 1, 100; real-ns at offset 0, +40, -40; clamp at 60 / 120 / 300 BPM; invalid input;
pack/unpack round trip incl. bar positions 1..4 and beat numbers above 2^31; bar position wrap with a
`shiftBeats` offset.

### Verify

Build and run `ctest -R master-beat-math`.

### Completion

```sh
geany-progress done 1 -r src/midi/master_beat_math.h -r src/midi/master_beat_math.cpp -r tests/test_master_beat_math.cpp -w "Positive offset = packet sent AFTER the MIDI clock beat (same meaning as difflock: sequencer early)" -w "Design is ALSA echo events, not a timing thread"
```

---

## Phase 2 — Echo events in AlsaClock, beat callback, MainWindow wiring (beats start flowing)

**Goal:** vdj-ui sends beat packets automatically, driven by ALSA echo events, offset fixed at 0. Verify with
tcpdump **before** the UI work.

### Files

- **edit** `src/midi/alsa_clock.h`, `src/midi/alsa_clock.cpp`
- **edit** `src/ui/main_window.h`, `src/ui/main_window.cpp`
- **edit** `src/network/network_manager.h/.cpp` (quiet send)
- **create** `tests/test_alsa_clock_beats.cpp` (skips when no ALSA sequencer is available); register in `tests/CMakeLists.txt`

### AlsaClock changes

- `open()`: create a third port `beatPort_` (`SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_NO_EXPORT`,
  not subscribable by outsiders), so only we receive on it.
- New API:
  ```cpp
  using BeatCallback = std::function<void(double bpm, int barPos)>;
  void setBeatCallback(BeatCallback cb);   // called from the clock thread, never with mutex_ held
  void setAsMasterOffsetMs(int ms);        // std::atomic<int>
  int  asMasterOffsetMs() const;
  uint64_t beatsSent() const;              // atomic, tests/telemetry
  int64_t lastSendErrorNs() const;         // event delivery time vs. the time asked for (event timestamp)
  ```
- `loop()` (`alsa_clock.cpp:399`) already queues clock pulses up to `now + 2*kPpq`. In the same pass also queue
  the beat echoes: keep `nextEchoBeat_` (reset with `resetNext_`/`restart()` like `next`), and for each whole beat
  whose `beatEchoTick(b) < horizon`, queue `SND_SEQ_EVENT_ECHO` to `client_:beatPort_`, `snd_seq_ev_schedule_tick`
  at `beatEchoTick(b)`, `data.raw32[0]` = tag `kBeatEcho` and `[1]` = `b`.
- **Input handling** — the loop currently sleeps 2 ms per pass. Do the input in the same thread: after the output
  step, `snd_seq_poll_descriptors` with `POLLIN` on the sequencer fd and a 2 ms timeout (replaces the
  `sleep_for(2ms)` so the thread wakes on the echo instead of at the next 2 ms tick; this is where the latency
  win comes from). While `snd_seq_event_input_pending(seq_, 1) > 0`, take `mutex_` briefly around
  `snd_seq_event_input`, handle the event, release.
  - Beat echo for `b` (tag kBeatEcho): with `mutex_` held, read `positionBeatsExact(&ns)` pieces (queue real
    time, tick position, model) as `positionBeatsExact` does, compute
    `sendEchoRealNs(beatsNow, realNs, effectiveBpm, offset, b, &t)`, queue the send echo
    (`snd_seq_ev_schedule_real`, tag `kSendEcho`, `raw32[1..2]` = `packBeatEcho(b, barPos)`), drain.
    `positionBeatsExact` locks `mutex_` itself: factor the body into a `positionBeatsExactLocked()` rather than
    recursively locking.
  - Send echo: unpack, **drop `mutex_`**, update `lastSendErrorNs_` from the event's timestamp vs. the scheduled
    time, call the callback with `effectiveBpm()` and the bar position.
  - Any other event type (e.g. SND_SEQ_EVENT_CLIENT_*, port subscription notices on the system announce port if
    we are subscribed) is ignored. Never let the clock output path block on this input path.
- `stopLocked()` / `restart()`: the existing `snd_seq_remove_events` on the queue already drops queued echoes;
  also reset `nextEchoBeat_`. A send echo that was already **delivered** to the input buffer just before a
  restart is dropped by comparing its beat number with the post-restart timeline (beat numbers restart at 0;
  record `echoGeneration_` incremented in `restart()`/`start()` and put it in `raw32[3]`, ignore other
  generations).
- No new thread, so the existing `quit_`/`thread_` lifecycle in `stop()` covers shutdown. `shiftBeats` and
  `phaseShiftMs` need no change: the echoes follow the queue, and the bar position is read when the beat echo
  arrives.

### MainWindow / NetworkManager

- `NetworkManager`: add `broadcastBeatQuiet(double bpm, int barPos)`: calls `vdj_broadcast_beat` and does **not**
  log through `PacketLogger` (check whether `PacketLogger` is thread-safe; if it is not, this must stay silent or
  marshal the log). No-op when `v_` is null. `vdj_broadcast_beat` is also called from `adj`'s own sequencer thread
  in the same way, so a non-GUI thread is the accepted pattern; confirm and comment any benign race on
  `v->bpm` / `v->bar_index`.
- `MainWindow` after `clock_->open(...)` (`main_window.cpp:~115`): `clock_->setBeatCallback([this](double bpm, int
  barPos){ network_->broadcastBeatQuiet(bpm, barPos); })`. In `shutdown()` (`main_window.cpp:~2329`) clear the
  callback before `network_` teardown. No change to `setVdjTransport`: echoes exist only while the clock runs.
- Offset stays 0 here; the old Send Beat button and `broadcastFromActiveDeck` stay until phase 3.

### Tests (`tests/test_alsa_clock_beats.cpp`)

Open a real `AlsaClock`; `QSKIP` with a message when `open()` fails (CI without `/dev/snd/seq`). Set BPM 150
(400 ms per beat), start, collect callback timestamps for ~2.5 s:
- 5-6 callbacks, intervals 400 ms ± 3 ms, bar positions cycle 1..4.
- `setAsMasterOffsetMs(+60)` and `(-60)`: compare against `beatOffsetMs`/positions: the callback lands 60 ms after
  / before the beat within ± 3 ms.
- `restart()` mid-run yields no duplicate beat numbers; `stop()` ends callbacks; no callback after `stop()` returns.
Use generous tolerances and a comment: this is a timing test.

### Manual verification (record in the progress notes)

Run vdj-ui, press Play on the VDJ deck, `sudo tcpdump -i <iface> -n udp port 50001 -c 40` (confirm the port from
`vdj_sendto_beat`, `parts/libcdj/src/src/c/vdj.c:765`): packets every `60/bpm` s with bar positions 1-4.
With a real deck present, check it locks to the VDJ when the VDJ is master.

### Completion

```sh
geany-progress done 2 -r src/midi/alsa_clock.cpp:399 -r src/ui/main_window.cpp:115 -r src/network/network_manager.cpp:306 -r tests/test_alsa_clock_beats.cpp -w "Beat packets now sent automatically whenever the ALSA clock runs, even when not master" -w "Clock thread now polls the sequencer fd instead of sleeping 2 ms: review for starvation of the clock-pulse queueing" -w "Verify with tcpdump on the beat port before trusting phase 3"
```

---

## Phase 3 — UI: replace "Send Beat" with the as_master_difflock spinner

**Goal:** the user sets `as_master_difflock` in ms (positive or negative) in the VDJ deck controls, live,
persisted.

### Files

- **edit** `src/ui/deck_controls.h`, `src/ui/deck_controls.cpp`
- **edit** `src/ui/deck_widget.h/.cpp`, `src/ui/deck_area.h/.cpp` (signal plumbing)
- **edit** `src/ui/main_window.cpp`, `src/ui/main_window.h`
- **edit** `src/ui/joystick_config_dialog.cpp`, wherever the `broadcast_beat` joystick action is dispatched
  (grep `broadcast_beat` and `broadcastFromActiveDeck` in `src/`)
- **edit** existing UI tests that reference `broadcastBeat` / "Send Beat" (grep `tests/`)

### Changes

1. `DeckControls`: delete `broadcastBeat_` (button, tooltip, layout slot `deck_controls.cpp:45,85`, visibility
   line `189`, connect `116-118`) and the `broadcastBeatClicked` signal (`deck_controls.h:68,82`). Replace with
   `QLabel("As master") + QSpinBox asMasterDiff_`: range -500..500, step 1, suffix " ms", default 0,
   `setKeyboardTracking(false)` is **not** wanted — values should apply live as the user types/spins. Visible
   for the VDJ row only (`setRemote(false)`), hidden for remote decks. Tooltip: "as_master_difflock: how far
   the beat packets we send to the real decks are from our MIDI clock beat, in ms. Positive: the packet is
   sent after the MIDI beat. Negative: before. Applies while real decks follow the VDJ."
   Keep `barPosSpin_` (still used by `d->setBarPos`).
2. New signal `asMasterDifflockChanged(int ms)` on `DeckControls`, forwarded through `DeckWidget` and
   `DeckArea` exactly the way `broadcastBeatClicked` was (`deck_widget.h:61`, `deck_area.cpp:41`);
   accessors `int asMasterDifflockMs() const` / `void setAsMasterDifflockMs(int)` (the setter blocks
   signals).
3. `MainWindow`: remove the `broadcastBeatClicked` connect (`main_window.cpp:528-533`). Add a connect to
   `asMasterDifflockChanged` that calls `clock_->setAsMasterOffsetMs(ms)` and
   `config_->saveValue("dj", "as_master_difflock_ms", QString::number(ms))`. At start-up load
   `dj/as_master_difflock_ms` (default 0), `setAsMasterDifflockMs`, `clock_->setAsMasterOffsetMs`.
   Follow how `sync_calculated_bpm` is loaded at `main_window.cpp:1551`.
4. Joystick action `broadcast_beat`: it no longer makes sense now that beats are automatic. Remove it from
   `kActions` and from its dispatcher. Old saved joystick configs that contain the name must be ignored
   quietly (check the loader; do not crash or warn per-frame). Delete `broadcastFromActiveDeck()` if nothing
   else uses it.
5. Remove the dead `NetworkManager::broadcastBeat` only if nothing else calls it after step 4; the quiet
   variant from phase 2 may then simply become `broadcastBeat` (drop the log line and rename; update the
   one caller).
6. Update tests and any docs/screenshots text that mention the Send Beat button (grep `README.md`, `doc/`).

### Tests

- Extend an existing deck-controls/deck-area test (find with `grep -l DeckControls tests/*.cpp`), or add a
  small one: spinner range, default 0, negative values accepted, `asMasterDifflockChanged` emits on change and
  not from `setAsMasterDifflockMs`, hidden on a remote row, "Send Beat" button absent.
- Re-run the whole `ctest` suite.

### Completion

```sh
geany-progress done 3 -r src/ui/deck_controls.cpp -r src/ui/main_window.cpp:528 -r src/ui/joystick_config_dialog.cpp:22 -w "Send Beat button and the broadcast_beat joystick action are removed" -w "Spinner range is +/-500 ms"
```

---

## Phase 4 — Telemetry, docs, end-to-end check

**Goal:** make the feature observable and documented; confirm the offset really moves packets.

### Files

- **edit** `src/ui/debug_overlay.cpp` — show `AlsaClock::beatsSent()` and `lastSendErrorNs()` (added in phase 2)
- **edit** `src/ui/main_window.cpp` / `src/midi/sync_telemetry.{h,cpp}` — add `asMasterDiffMs` and
  `beatSendJitterUs` to the telemetry sample only if `SyncTelemetry` already has an obvious place for
  per-sample fields (see `diffUsedMs`, `sync_telemetry.h:37`); otherwise skip this and show them in the debug
  overlay (`src/ui/debug_overlay.cpp`) as one line "master beat: N sent, last err X us, offset Y ms"
- **edit** `README.md` and the relevant `doc/` page that describes difflock / the VDJ deck controls
  (grep `difflock` in `doc/`): document `as_master_difflock`, its sign, its persisted key
  `dj/as_master_difflock_ms`, and that beats are now sent automatically with the ALSA clock

### Checks

- `ctest` all green; build with the default and the asan build dirs if the project normally does (`build-asan`).
- Manual: with a real deck following the VDJ (or tcpdump timestamps vs. an MIDI clock capture,
  e.g. `aseqdump -p vdj-ui`), change the spinner by +50 ms and confirm the packet-to-clock gap moves by
  ~50 ms, and moves the other way for -50 ms. Record the observed numbers in the progress notes.
- Confirm restart (`clock_->restart()`), the ⏪/⏩ beat jump buttons, nudge and BPM change keep packets flowing
  without double-sends or gaps > 1 beat.

### Completion

```sh
geany-progress done 4 -r doc -r README.md -w "Measured packet-vs-clock gap with spinner at +/-50 ms is recorded in the progress notes" -w "Telemetry fields only added where SyncTelemetry already had a natural slot"
```
