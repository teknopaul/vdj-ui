# Track View keyboard control of the Local Track Player

Design doc (final, read it first): `doc/trakcrate/track-view-keyboard-control.md`.
Source notes: `ai-output/track-view-keyboard-control-plan.md`.

## Decisions already made (do not re-open)

- Up/Down while the player has focus: **only move focus to the track list**, no extra row move.
- `a` `b` `c` jump to cues **only when the player has focus**. In the track list they stay type-ahead navigation (unchanged).
- The Local Track Player has no edit mode; no clash with the waveform's B/M/Delete keys (those belong to the trakcrate track editor's waveform, not the player).
- No Ctrl-modifier features. Key checks keep rejecting any modifier except the keypad one.
- BPM lock: **no clamping or range limiting** when applying the lock on load (preview; if it sounds bad the DJ must hear that).
- BPM revert: back to the loaded track's own BPM, lock state unchanged. Lock is not persisted; default off.
- Page Up/Down stop scrolling the track list while a track is loaded (accepted).
- `img/lock-16x16.png` exists.

## Codebase facts the phases rely on

- `src/ui/player_panel.{h,cpp}`: `PlayerPanel::event` (ShortcutOverride for Space, PgUp, PgDn), `keyPressEvent` (Space -> `playClicked()` if loaded, PgUp/PgDn -> `jumpBeats(+-4)`), `jumpToCue(double ms)` (does the quantize handling), `cueButtons_` built in `rebuildCueButtons()` from `analysis_.cues` (`AnalysisCue::hotCue`, `letter()` gives "A".."H", "" for memory cues, `timeMs`), `quantize_` button (checkable, `makeQuantizeButton`), `setTargetBpm()` (clamps to 0.5x..2x of `originalBpm_`), `loadImpl()` sets `originalBpm_`, `targetBpm_ = originalBpm_`, `player_->setTempo(1.0)`, `bpmReset_` -> `setTargetBpm(originalBpm_)`.
- `src/ui/trakcrate_table_view.h`: `TrakcrateTableView` has `event()` (ShortcutOverride for Up/Down), `keyPressEvent()` (Enter -> `onEnter`, Ctrl+Up/Down -> `onKeyMove`), callbacks as `std::function` members.
- `src/ui/trakcrate_panel.cpp` ~line 319: `table_->onEnter` emits `loadAndPlayRequested(dir)`.
- `src/ui/main_window.cpp` ~line 2455: `loadAndPlayRequested` handler loads, plays, then calls `playerPanel_->setFocus()` (to be removed).
- `src/ui/keyboard_shortcuts.cpp`: application shortcuts that collide with player keys: Space (VDJ play/pause), **B (copy BPM)**, Up/Down (pitch nudge), S, M, Tab. Qt sends `ShortcutOverride` first; a widget that accepts it gets the key instead of the shortcut.
- Tests: `tests/test_trakcrate_panel.cpp` (table/panel), `tests/test_player_quantize.cpp` (player). Build in `build/`; tests registered in `tests/CMakeLists.txt` (offscreen Qt).
- Never use git (user handles version control).

---

## Phase 1: Remote keys from the track list, keep list focus

Goal: the track list keeps focus after Enter and drives the player with Space, Page Up, Page Down. Up/Down with the player focused hands focus to the list.

Work:
1. `player_panel.h/.cpp`: add public `bool handlePlayerKey(QKeyEvent* e)` containing exactly the existing Space/PgUp/PgDn logic (modifier check included). Space returns false when no track is loaded (so the global VDJ Space shortcut still works); PgUp/PgDn likewise return false when not loaded. `keyPressEvent` calls it first, then `QWidget::keyPressEvent`. Add `static bool isPlayerKey(const QKeyEvent*)` (Space, PageUp, PageDown, no modifiers except keypad) used by `event()`'s ShortcutOverride check, so the two widgets share one key list.
2. Ask-for-list-focus path: add a signal `focusTrackListRequested()` to `PlayerPanel`. In `event()` ShortcutOverride accept Up/Down (no modifiers); in `keyPressEvent` Up/Down emit the signal and accept. Without a connected list nothing happens (the panel is also used for other track sources); main window connects it only to the trakcrate panel.
3. `trakcrate_table_view.h`: new member `std::function<bool(QKeyEvent*)> onPlayerKey;`. In `event()` ShortcutOverride: if `onPlayerKey` is set and `PlayerPanel::isPlayerKey(k)` and the view is not in `EditingState` and the callback would handle it, accept. Simplest: add a second callback `std::function<bool()> playerWantsKeys` (true when a track is loaded), or call a `canHandle`. Choose the simplest that keeps Space falling through to VDJ when nothing is loaded. In `keyPressEvent`, before `QTreeView::keyPressEvent`, when `state() != EditingState` call `onPlayerKey(e)` and return if it returned true.
4. `trakcrate_panel.h/.cpp` + `main_window.cpp`: expose the wiring (a setter on `TrakcratePanel` taking the `PlayerPanel*`, or a signal/slot pair; follow how `loadAndPlayRequested` is already wired in `main_window.cpp`). Wire `table_->onPlayerKey` to `playerPanel_->handlePlayerKey`, and `PlayerPanel::focusTrackListRequested` to `table_->setFocus()`. Remove `playerPanel_->setFocus()` from the `loadAndPlayRequested` handler so the list keeps focus; update the comment on that signal in `trakcrate_panel.h` ("... and focus the player").
5. Tests (`tests/test_trakcrate_panel.cpp` and/or `tests/test_player_quantize.cpp`): `isPlayerKey` accepts Space/PgUp/PgDn only without modifiers; table view calls `onPlayerKey` for Space and not while editing; Space with `onPlayerKey` returning false is not consumed; `handlePlayerKey` returns false for Space when nothing is loaded. Keep tests small and offscreen.
6. Build (`cd build && make -j8`) and run the touched tests with ctest.

Do not touch cue keys, BPM or tooltips here.

```sh
geany-progress done 1 -r src/ui/player_panel.cpp -r src/ui/trakcrate_table_view.h -r src/ui/main_window.cpp:2455 -w "Page Up/Down no longer scroll the track list while a track is loaded" -w "Space falls through to VDJ play/pause when no track is loaded"
```

---

## Phase 2: Cue keys a/b/c, cue labels in the waveform, q for quantize

Goal: with the player focused, `a` `b` `c` jump to the hot cues with those letters and `q` toggles quantize; the waveform labels the cue arrows.

Work:
1. `PlayerPanel::event`/`keyPressEvent`: accept in ShortcutOverride and handle (no modifiers): `Key_A`, `Key_B`, `Key_C` -> find the first `analysis_.cues` entry with `hotCue` and `letter()` equal to "A"/"B"/"C" and call `jumpToCue(double(timeMs))` (quantize rules already inside). No such cue -> status message "No cue A" and still accept. `Key_Q` -> `quantize_->toggle()` (button is checkable and its `toggled` handler already shows the "no beat grid" message). **`B` must be accepted in ShortcutOverride because `keyboard_shortcuts.cpp` binds B to "copy BPM".** Only when a track is loaded; otherwise ignore so B still copies the BPM.
2. These keys are not forwarded from the track list (list keeps type-ahead). Do **not** add them to `isPlayerKey`.
3. `waveform_widget.cpp` `paintCues`: draw the cue's letter (lower-case a/b/c for hot cues A..C; other letters/memory cues: draw nothing or the upper-case letter, keep it small) next to the yellow down arrow at the top, in dark text on a small yellow rect or just yellow text with a dark halo. Keep the existing one-pixel line and arrow untouched. Use `AnalysisCue::letter()`.
4. Tests: a test for the key -> cue lookup (extract a small pure helper, e.g. `static const AnalysisCue* hotCueForKey(const QList<AnalysisCue>&, int key)`), and that `q` toggles the quantize button.
5. Build and run tests.

```sh
geany-progress done 2 -r src/ui/player_panel.cpp -r src/ui/waveform_widget.cpp:paintCues -w "B is also the global copy-BPM shortcut: the player only claims it when a track is loaded and the player has focus"
```

---

## Phase 3: Tooltips and typed BPM

Goal: key hints on the transport buttons and a double click on the BPM display lets the DJ type a BPM.

Work:
1. `player_panel.cpp` button tooltips (around line 55-58): back -> "Back one bar (4 beats), Page Up", play -> "Play / pause, Space", forward -> "Forward one bar (4 beats), Page Down". Quantize button tooltip: add "(q)". Cue buttons: add the key ("a"/"b"/"c") when the cue letter is A, B or C.
2. Double click on the BPM label (`bpm_`) opens `QInputDialog::getDouble` (or an inline `QLineEdit` that replaces the label, whichever matches the surrounding code) pre-filled with `targetBpm_`, 2 decimals; OK calls `setTargetBpm(value)`. Needs the label to receive `mouseDoubleClickEvent`: use an event filter on `bpm_` installed by `PlayerPanel` or a tiny `QLabel` subclass; follow existing patterns in `src/ui`. Do nothing when no track with a BPM is loaded.
3. Note `setTargetBpm` clamps to 0.5x..2x of the original BPM. Leave that for manual changes; Phase 4 bypasses it only for the lock.
4. Build; add a test that the tooltips contain the key names if cheap.

```sh
geany-progress done 3 -r src/ui/player_panel.cpp
```

---

## Phase 4: BPM lock

Goal: a tiny lock toggle in the top right corner of the BPM display; when on, loading a new track keeps the current playback BPM.

Work:
1. `player_panel.h/.cpp`: new checkable `QToolButton`/`QPushButton` `bpmLock_` using `img/lock-16x16.png` (check how other icons are loaded and added to resources in `src/CMakeLists.txt`/`.qrc`; the file is already in `img/`). Placed in the top right corner of the BPM display (overlay on the `bpm_` label's layout or in its container). `Qt::NoFocus` like the other buttons so it never steals keys. Tooltip "Lock BPM: keep the current BPM when a new track is loaded". Default off, not saved to settings. Public accessor `bool bpmLocked() const`.
2. `loadImpl()`: capture `const bool keep = bpmLock_->isChecked() && targetBpm_ > 0;` and `const double wanted = targetBpm_;` before the existing reset. After the load succeeded and `originalBpm_` is known, if `keep && originalBpm_ > 0`: set `targetBpm_ = wanted` and `player_->setTempo(wanted / originalBpm_)` **without clamping** (do not go through the clamping `setTargetBpm`; add a private helper or a parameter for it), then `updateBpmLabel()`. If the new track has no BPM (`originalBpm_ <= 0`) it loads at normal speed and the lock stays on. Load failure path unchanged.
3. `bpmReset_`: unchanged behaviour (`setTargetBpm(originalBpm_)`); verify it does not touch the lock.
4. Other BPM changes (-/+, typed value, reset) are unaffected; the lock only applies on load.
5. Tests: with lock on, loading a second track with a different native BPM keeps `targetBpm()` and `player->tempo() == target/original`; ratio beyond 2x is not clamped; no-BPM track loads at tempo 1.0 with lock still on; reset leaves the lock state. Use the existing player test fixtures in `tests/test_player_quantize.cpp` for how a `PlayerPanel` is constructed and a track loaded (may need a small WAV fixture; look for existing ones before creating any).
6. Build and run tests.

```sh
geany-progress done 4 -r src/ui/player_panel.cpp -w "No clamping when the lock applies: a 128 -> 170 BPM load plays at a large tempo ratio on purpose" -w "Lock state is not persisted"
```

---

## Phase 5: Documentation

Goal: docs match behaviour.

Work:
1. `doc/trakcrate/track-view-keyboard-control.md`: tick off / remove the finished TODO items, keep the key table accurate (Up/Down on player focus = transfer focus only; Ctrl features already removed), document the BPM lock semantics (no clamping, revert unchanged, not persisted, default off) and that Page Up/Down no longer scroll the list while a track is loaded.
2. `doc/trakcrate/gui.md`: add a short "Keyboard" pointer to that doc (keep style of the surrounding file; use the `mdalign` skill for table alignment only if tables are added).
3. Run the full build once more and the whole ctest suite; report any failure that is unrelated to this work without fixing it.

```sh
geany-progress done 5 -r doc/trakcrate/track-view-keyboard-control.md -r doc/trakcrate/gui.md
```
