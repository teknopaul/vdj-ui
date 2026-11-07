# Track View keyboard control: implementation plan

Design: `doc/trakcrate/track-vew-keyboard-control.md` (final). Decisions from the discussion:

- Up/Down with the player focused: transfer focus to the track list only (no extra row move).
- `a` `b` `c` cue jumps: player focus only. In the track list they stay type-ahead navigation, unchanged.
- The Local Track Player has no edit mode, so no key clashes with the waveform B/M/Delete keys.
- No Ctrl modifier features.
- BPM lock: no clamping, no range limiting. Preview only; if it sounds bad the DJ needs to hear that.
- BPM revert: goes back to the current track's own BPM, lock state unchanged.
- Lock state is not persisted. Default off.
- `img/lock-16x16.png` exists.

## Step 1: remote keys from the track view

- `PlayerPanel::handlePlayerKey(QKeyEvent*)` (public): holds the Space and Page Up/Down handling now in `keyPressEvent`
  (`player_panel.cpp:323`). Returns false for Space when no track is loaded, so VDJ's Space shortcut still fires.
- `PlayerPanel::keyPressEvent` calls it, so the player keeps working when it has focus.
- Shared helper `isPlayerKey(QKeyEvent*)` used by the `ShortcutOverride` checks in `PlayerPanel::event` and
  `TrakcrateTableView::event` so the two key lists cannot drift apart.
- `TrakcrateTableView` (`trakcrate_table_view.h`): new `std::function<bool(QKeyEvent*)> onPlayerKey`, called from
  `keyPressEvent` before `QTreeView::keyPressEvent`, skipped in `EditingState` (like `onEnter`).
- `trakcrate_panel.cpp` (~line 319, `onEnter`): stop focusing the player after loading; keep list focus. Wire `onPlayerKey`
  to the panel's `PlayerPanel`.
- `PlayerPanel`: Up/Down while the player has focus gives focus to the track list (needs a callback or signal into
  `TrakcrateSettingsView`/panel, the player does not know about the list).

## Step 2: cue keys, labels, quantize

- `a` `b` `c` in `PlayerPanel` (player focus only): quantized jump to the 1st/2nd/3rd cue point if present.
- Waveform cue arrows (`paintCues`, `waveform_widget.cpp`): draw the label a/b/c next to the yellow down arrow.
- `q` toggles quantize (player focus only).

## Step 3: tooltips and BPM entry

- `<<` `>` `>>` tooltips mention PgUp, Space, PgDn.
- Double click on the BPM display opens a numeric entry.

## Step 4: BPM lock

- Tiny lock toggle at the top right of the BPM display, using `img/lock-16x16.png`. Checkable; default off; not persisted.
- When locked, loading a new track keeps the current playback BPM: tempo ratio = lockedBpm / newTrackBpm. No clamping.
  Tracks without a BPM load at normal speed (no ratio to compute).
- BPM revert button: back to the loaded track's own BPM; lock state untouched.
- Other BPM changes (-/+, typed value) are unaffected; the lock only applies on load.

## Docs

- Update `doc/trakcrate/gui.md` with the key table once implemented.
- Page Up/Down no longer scroll the list while a track is loaded (accepted).
