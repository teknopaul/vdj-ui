# ALSA Sequencer Integration Plan

## Overview

Integrate a MIDI clock sequencer (equivalent to `libadj`) directly into vdj-ui as a
native C++ Qt component.  The sequencer drives connected MIDI devices with a stable
MIDI clock signal, supports BPM control, nudge, quantized restart, and is controllable
via Qt buttons, keyboard shortcuts, and joystick axes.

### Approach decision: reimplemented in C++ Qt (not linked to libadj.so)

| Criterion          | Link libadj                               | Reimplement in Qt              |
|--------------------|-------------------------------------------|--------------------------------|
| Thread model       | pthreads globals clash with Qt shutdown   | QThread integrates cleanly     |
| Multiple instances | Blocked by module-level `_Atomic` globals | Supported as QObject instances |
| ALSA API surface   | ~60 lines in libadj.c — well understood   | Same calls, no extra dep       |
| libasound          | Already linked in CMakeLists              | Already linked                 |
| Maintenance        | Two repos                                 | One repo                       |

The ALSA calls are already understood from `libadj.c`.  `libasound` is already in
`src/CMakeLists.txt`.  The reimplement path is cleaner and keeps everything in one repo.

### Constants (from adj.h)

```cpp
static constexpr int PPQ              = 96;   // ticks per quarter note
static constexpr int CLOCKS_PER_BEAT  = 24;   // MIDI spec
static constexpr double BEATS_QUEUED  = 0.25; // lookahead (≈125 ms @ 120 BPM)
static constexpr int BEATS_PER_BAR    = 4;
static constexpr double MIN_BPM       = 20.0;
static constexpr double MAX_BPM       = 300.0;
```

---

## Phase 1 — AlsaSequencer engine class

**Goal:** a self-contained `AlsaSequencer : public QObject` that owns all ALSA
sequencer state and runs its clock loop on a dedicated `QThread`.  No UI code in this
phase.

### Files to create

**`src/midi/alsa_sequencer.h`**

```cpp
namespace vdj {

class AlsaSequencer : public QObject {
    Q_OBJECT
public:
    explicit AlsaSequencer(QObject* parent = nullptr);
    ~AlsaSequencer();

    bool init(const QString& clientName = QStringLiteral("vdj-midi-clock"));
    void shutdown();

    bool isRunning() const;
    bool isPlaying() const;
    double bpm() const;
    int clientId() const;
    int portId() const;

public slots:
    void start();
    void stop();
    void toggle();
    void quantizedRestart();
    void nudge(int multiplier);         // ±N × 0.1 BPM for one beat
    void nudgeMillis(int ms);           // shift by N ms for one beat
    void adjustTempo(double bpmDelta);  // permanent ±N BPM
    void setTempo(double bpm);          // absolute BPM

signals:
    void started();
    void stopped();
    void tempoChanged(double bpm);
    void tickAdvanced(int tick);        // fired each MIDI clock tick
    void eventsInQueue(int n);
    void stateChanged(const QString& seqState, const QString& qState);
    void errorOccurred(const QString& msg);

private:
    // internal ALSA helpers (all called from clock thread)
    bool alsaInit(const QString& name);
    void alsaClose();
    int  setTempoInternal(double bpm);
    int  clearQueue();
    void midiStart();
    void midiStop();
    void sendClock(unsigned int tick);

    void* seq_   = nullptr; // snd_seq_t*
    int   port_  = -1;
    int   queue_ = -1;
    int   clientId_ = -1;

    std::atomic<bool> running_{false};  // main loop alive
    std::atomic<bool> playing_{false};  // queue is running
    std::atomic<bool> doStop_{false};
    std::atomic<bool> doStart_{false};
    std::atomic<bool> doQRestart_{false};
    std::atomic<int>  nudgeMultiplier_{0};
    std::atomic<int>  nudgeMs_{0};
    std::atomic<double> pendingBpm_{0.0};
    std::atomic<double> pendingBpmDelta_{0.0};

    double bpm_  = 120.0;
    unsigned int tick_ = 0;

    QThread* clockThread_ = nullptr;
    friend class AlsaClockWorker;
};

} // namespace vdj
```

**`src/midi/alsa_sequencer.cpp`**  — implement all of the above.

Key internal design (mirrors libadj architecture):

```
AlsaClockWorker : public QObject  (runs on clockThread_)
  └─ run()  — loop: queue BEATS_QUEUED worth of SND_SEQ_EVENT_CLOCK ticks,
              nanosleep(BEATS_QUEUED beats), handle pending commands
```

The worker loop:
1. Call `setTempoInternal(bpm_)` to configure queue tempo via
   `snd_seq_set_queue_tempo()`.
2. Enter loop: queue `BEATS_QUEUED * PPQ * CLOCKS_PER_BEAT / PPQ` = 6 clock events
   (`CLOCKS_PER_BEAT × BEATS_QUEUED = 24 × 0.25 = 6` clocks), drain output.
3. `nanosleep()` for `BEATS_QUEUED / bpm_` seconds.
4. Check atomics: `doStop_`, `doStart_`, `doQRestart_`, `nudgeMultiplier_`,
   `pendingBpm_`, `pendingBpmDelta_`.
5. Emit `tickAdvanced(tick_)` and `eventsInQueue(n)` via Qt::QueuedConnection
   back to main thread.

ALSA queue init sequence (from libadj.c):
```c
snd_seq_open(&s, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
snd_seq_set_client_name(s, name);
port = snd_seq_create_simple_port(s, "midi-clock",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
    SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
q = snd_seq_alloc_queue(s);
```

Nudge algorithm (same as adj_get_nudge_bpm / adj_get_nudge_micros):
- multiplier nudge: set tempo to `bpm + 0.1*N`, sleep one beat, restore.
- millis nudge: set tempo to `micros_per_beat + ms*1000`, sleep one beat, restore.

### CMake changes

In `src/CMakeLists.txt`, add to `vdj-cdj` source list:
```cmake
midi/alsa_sequencer.cpp
```
(`libasound` is already linked.)

### Completion marker

```sh
geany-progress done 1 \
  -r src/midi/alsa_sequencer.h \
  -r src/midi/alsa_sequencer.cpp \
  -w "Clock accuracy depends on nanosleep() + queue lookahead; jitter < 5ms is acceptable for MIDI clock" \
  -w "AlsaSequencer uses std::atomic not _Atomic — confirm C++17 or later in CMake"
```

---

## Phase 2 — MidiClockPanel widget

**Goal:** a `QWidget` subclass that is the full graphical equivalent of `adj_tui`.
No new ALSA code; it only connects to `AlsaSequencer` signals/slots.

### Files to create

**`src/ui/midi_clock_panel.h`**

```cpp
namespace vdj {

class AlsaSequencer;

class MidiClockPanel : public QWidget {
    Q_OBJECT
public:
    explicit MidiClockPanel(AlsaSequencer* seq, QWidget* parent = nullptr);

public slots:
    void onTempoChanged(double bpm);
    void onStarted();
    void onStopped();
    void onTickAdvanced(int tick);
    void onEventsInQueue(int n);
    void onStateChanged(const QString& seq, const QString& q);
    void onError(const QString& msg);

private slots:
    void onStartClicked();
    void onStopClicked();
    void onToggleClicked();
    void onQRestartClicked();
    void onSetBpmClicked();
    void onNudgeClicked(int multiplier);
    void onTempoAdjClicked(double delta);

private:
    AlsaSequencer* seq_;

    // Status labels (mirrors adj_tui data_item layout)
    QLabel* lblSeqState_;
    QLabel* lblQState_;
    QLabel* lblBpm_;
    QLabel* lblEvents_;
    QLabel* lblClientId_;
    QLabel* lblPort_;

    // Beat grid (custom painted sub-widget)
    class BeatGridBar;
    BeatGridBar* beatGrid_;

    // Controls
    QPushButton* btnStart_;
    QPushButton* btnStop_;
    QPushButton* btnToggle_;
    QPushButton* btnQRestart_;
    QDoubleSpinBox* bpmSpin_;
    QPushButton* btnSetBpm_;

    // Nudge row:  <<<  <<  <   start/stop   >  >>  >>>
    // (multipliers: -20, -10, -1, toggle, +1, +10, +20)

    // Tempo adjust row:  -1  -0.1  -0.01   +0.01  +0.1  +1
};

} // namespace vdj
```

**`src/ui/midi_clock_panel.cpp`**

Layout (QVBoxLayout):
```
┌─────────────────────────────────────────────────────────┐
│  seq state: running │ q state: paused │ bpm: 120.0000   │
│  client_id: 14      │ port: 0         │ events: 6       │
├─────────────────────────────────────────────────────────┤
│  [Beat grid — 64-cell bar, one cell per quarter-beat]   │
├─────────────────────────────────────────────────────────┤
│  [<<<] [<<] [<]  [Start] [Stop] [Toggle] [QRestart] [>] [>>] [>>>]  │
├─────────────────────────────────────────────────────────┤
│  [-1.0] [-0.1] [-0.01]  BPM: [120.00 ▲▼]  [Set]  [+0.01] [+0.1] [+1.0] │
└─────────────────────────────────────────────────────────┘
```

**`BeatGridBar`** (inner class, custom `paintEvent`):
- 64 cells, one per quarter-beat (4 bars × 4 beats × 4 quarter-beats).
- Current cell highlighted (like `\033[7m` in adj_tui).
- Bar separators at positions 0, 16, 32, 48 (`|`), beat separators every 4 (`:`)
  within a bar, quarter-beat dots elsewhere.
- Updated by `onTickAdvanced(tick)`: `currentCell = (tick / CLOCKS_PER_BEAT) % 64`.
- `update()` called each tick; paintEvent draws all 64 cells in one pass.

### Add to CMakeLists.txt

```cmake
ui/midi_clock_panel.cpp
```

### Completion marker

```sh
geany-progress done 2 \
  -r src/ui/midi_clock_panel.h \
  -r src/ui/midi_clock_panel.cpp \
  -w "BeatGridBar does not use QTimer — it is driven by tickAdvanced signal from clock thread; verify queued connection to avoid cross-thread painter issues"
```

---

## Phase 3 — MainWindow integration + CMake wiring

**Goal:** mount `AlsaSequencer` and `MidiClockPanel` inside `MainWindow`, add a dock
widget, and wire all signals/slots.  Update CMake.  Build must succeed.

### Changes to `src/ui/main_window.h`

Add members:
```cpp
#include "../midi/alsa_sequencer.h"
class MidiClockPanel;
// ...
AlsaSequencer* midiClock_ = nullptr;
MidiClockPanel* midiClockPanel_ = nullptr;
QDockWidget*   midiClockDock_  = nullptr;
```

### Changes to `src/ui/main_window.cpp`

In constructor, after `midiRouter_` init:
```cpp
midiClock_ = new AlsaSequencer(this);
if (!midiClock_->init()) {
    qWarning("AlsaSequencer: ALSA init failed — MIDI clock unavailable");
}
midiClockPanel_ = new MidiClockPanel(midiClock_, this);
midiClockDock_  = new QDockWidget(tr("MIDI Clock"), this);
midiClockDock_->setObjectName(QStringLiteral("midiClockDock"));
midiClockDock_->setWidget(midiClockPanel_);
addDockWidget(Qt::BottomDockWidgetArea, midiClockDock_);
```

Connect AlsaSequencer → MidiClockPanel (done inside MidiClockPanel ctor, it receives the seq pointer).

Connect AlsaSequencer → existing BPM infrastructure (optional, Phase 3 stretch):
```cpp
connect(midiClock_, &AlsaSequencer::tempoChanged, this, [this](double bpm) {
    // update status bar MIDI clock BPM label
});
```

Add menu item under existing Tools/MIDI menu:
```cpp
auto* showClockAct = new QAction(tr("MIDI Clock"), this);
showClockAct->setCheckable(true);
showClockAct->setChecked(true);
connect(showClockAct, &QAction::toggled, midiClockDock_, &QDockWidget::setVisible);
midiMenu->addAction(showClockAct);
```

In `MainWindow::shutdown()` (called from `closeEvent`):
```cpp
if (midiClock_) midiClock_->shutdown();
```

### CMakeLists.txt — ensure new files are listed

Both `midi/alsa_sequencer.cpp` and `ui/midi_clock_panel.cpp` must appear in the
`add_library(vdj-cdj STATIC ...)` source list.

### Verify build

```sh
cd build && cmake .. && make -j$(nproc) 2>&1 | tail -20
```

### Completion marker

```sh
geany-progress done 3 \
  -r src/ui/main_window.cpp \
  -r src/ui/main_window.h \
  -r src/CMakeLists.txt \
  -w "Dock starts visible; user can hide it from the MIDI menu" \
  -w "ALSA init failure is soft (warning only) so the app still starts without an ALSA sequencer"
```

---

## Phase 4 — Keyboard shortcuts for sequencer

**Goal:** extend `KeyboardShortcuts` so the user can control the MIDI clock from the
keyboard, matching the key map from `adj_keyb.c` adapted to Qt (no terminal raw mode).

### Key map (avoids clashing with existing pitch shortcuts)

| Key combination | Action             | adj_keyb equivalent |
|-----------------|--------------------|---------------------|
| Ctrl+Space      | toggle MIDI clock  | Space               |
| Ctrl+Right      | nudge +10          | Arrow Right         |
| Ctrl+Left       | nudge −10          | Arrow Left          |
| Ctrl+Up         | nudge +20          | Arrow Up            |
| Ctrl+Down       | nudge −20          | Arrow Down          |
| Ctrl+Home       | quantized restart  | Home                |
| Ctrl+= (plus)   | adjust tempo +0.1  | `+`                 |
| Ctrl+- (minus)  | adjust tempo −0.1  | `-`                 |
| Ctrl+]          | adjust tempo +1.0  | `r`                 |
| Ctrl+[          | adjust tempo −1.0  | `w`                 |
| Ctrl+.          | adjust tempo +0.01 | `v`                 |
| Ctrl+,          | adjust tempo −0.01 | `x`                 |

### Changes to `src/ui/keyboard_shortcuts.h`

Add signals:
```cpp
void midiClockToggleRequested();
void midiClockNudgeRequested(int multiplier);
void midiClockTempoAdjRequested(double bpmDelta);
void midiClockQRestartRequested();
```

### Changes to `src/ui/keyboard_shortcuts.cpp`

Add `QShortcut` entries for each key combination above, wiring to the new signals.

### Changes to `src/ui/main_window.cpp`

In `MainWindow::setupShortcuts()`, connect the new keyboard signals to `midiClock_`:
```cpp
connect(kbd, &KeyboardShortcuts::midiClockToggleRequested,
        midiClock_, &AlsaSequencer::toggle);
connect(kbd, &KeyboardShortcuts::midiClockNudgeRequested,
        midiClock_, &AlsaSequencer::nudge);
connect(kbd, &KeyboardShortcuts::midiClockTempoAdjRequested,
        midiClock_, &AlsaSequencer::adjustTempo);
connect(kbd, &KeyboardShortcuts::midiClockQRestartRequested,
        midiClock_, &AlsaSequencer::quantizedRestart);
```

### Completion marker

```sh
geany-progress done 4 \
  -r src/ui/keyboard_shortcuts.h \
  -r src/ui/keyboard_shortcuts.cpp \
  -r src/ui/main_window.cpp \
  -w "Ctrl+Space conflicts if Qt uses it for something — verify no grab by OS/WM" \
  -w "Existing plain-Arrow pitch nudge shortcuts are preserved; Ctrl+Arrow is the sequencer layer"
```

---

## Phase 5 — Joystick sequencer support

**Goal:** extend the existing joystick framework so joystick axes and buttons can also
control the MIDI clock sequencer, matching the `adj_js.c` mapping.

### Review `adj_js.c` axis/button map

```
Axis 0 (main left L/R)   → adjust tempo ±0.1
Axis 1 (main left U/D)   → adjust tempo ±1.0
Axis 2 (left trigger)    → nudge ±10
Axis 3 (right U/D)       → nudge ±1 / micro tempo
Button 8 (top-left trig) → toggle
Button 9 (bottom-left)   → stop (also acts as modifier)
Button 10 (top-right)    → trigger on downbeat (quantized restart)
Button 11 (bottom-right) → quantized restart
Buttons 0-3 (face)       → copy BPM from CDJ 1-4 (already in vdj-ui)
```

### Changes to `src/ui/joystick_config_dialog.h/.cpp`

Extend `kActions[]` with:
```cpp
"midi_clock_toggle",
"midi_clock_nudge_pos",
"midi_clock_nudge_neg",
"midi_clock_tempo_up",
"midi_clock_tempo_down",
"midi_clock_qrestart",
```

### New class: `JoystickSequencerBridge`

Create `src/midi/joystick_seq_bridge.h/.cpp`:
- Opens `/dev/input/jsX` (path from ConfigManager).
- Reads `js_event` structs in a `QThread`.
- Maps axes/buttons to `AlsaSequencer` slot calls using the `kActions` config.
- Emits Qt signals that `MainWindow` connects to `AlsaSequencer`.

This mirrors `adj_js.c` but uses `QThread` + `AlsaSequencer` signals instead of direct
C function calls.

Default axis mapping (mirrors adj_js.c Logitech mapping):
```
Axis 0 → adjustTempo(±0.1)
Axis 1 → adjustTempo(±1.0)
Axis 2 → nudge(±10)
Axis 3 → nudge(±1)
```

Default button mapping:
```
Button 8 → toggle()
Button 9 → stop()
Button 10 → quantizedRestart()
Button 11 → quantizedRestart()
```

### CMakeLists.txt

Add `midi/joystick_seq_bridge.cpp` to the source list.

### MainWindow wiring

In `MainWindow` constructor, after `midiClock_` init:
```cpp
auto* jsBridge = new JoystickSequencerBridge(config_, this);
jsBridge->setSequencer(midiClock_);
jsBridge->start();  // opens device and starts thread
```

### Completion marker

```sh
geany-progress done 5 \
  -r src/midi/joystick_seq_bridge.h \
  -r src/midi/joystick_seq_bridge.cpp \
  -r src/ui/joystick_config_dialog.cpp \
  -r src/ui/main_window.cpp \
  -w "Joystick device path must be configured; bridge skips silently if device is absent" \
  -w "adj_js.c axis values are ±32767; map to ±1 multipliers, not raw values" \
  -w "Logitech axis mapping is hardcoded by default; full remapping via JoystickConfigDialog is stretch goal"
```

---

## Summary of new files

| File                               | Purpose                                     |
|------------------------------------|---------------------------------------------|
| `src/midi/alsa_sequencer.h`        | AlsaSequencer class declaration             |
| `src/midi/alsa_sequencer.cpp`      | ALSA clock engine (≈350 lines)              |
| `src/ui/midi_clock_panel.h`        | MidiClockPanel widget declaration           |
| `src/ui/midi_clock_panel.cpp`      | UI panel + BeatGridBar painter (≈300 lines) |
| `src/midi/joystick_seq_bridge.h`   | JoystickSequencerBridge declaration         |
| `src/midi/joystick_seq_bridge.cpp` | Joystick → sequencer bridge (≈150 lines)    |

Modified files: `src/CMakeLists.txt`, `src/ui/main_window.h/.cpp`,
`src/ui/keyboard_shortcuts.h/.cpp`, `src/ui/joystick_config_dialog.h/.cpp`.

## Dependencies already present

- `libasound` — already in CMakeLists
- Qt5/Qt6 Widgets — already linked

