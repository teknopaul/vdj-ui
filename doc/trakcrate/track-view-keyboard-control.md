# Track View Player Control

A Playlist's Track View can be used to control the Local Track Player to help preparing a playlist by previewing audio

  ┌────────────────┬─────────────────────────────────────┬──────────────────────────────────────────────────┬────────────────┐
  │      Key       │          Track list focus           │                   Player focus                   │    Neither     │
  ├────────────────┼─────────────────────────────────────┼──────────────────────────────────────────────────┼────────────────┤
  │ Up / Down      │ move highlight                      │ transfer focus to track list                     │ pitch nudge    │
  ├────────────────┼─────────────────────────────────────┼──────────────────────────────────────────────────┼────────────────┤
  │ Enter          │ load track                          │ n/a                                              │ n/a            │
  ├────────────────┼─────────────────────────────────────┼──────────────────────────────────────────────────┼────────────────┤
  │ Space          │ play/pause local player (if loaded) │ play/pause local player                          │ VDJ play/pause │
  ├────────────────┼─────────────────────────────────────┼──────────────────────────────────────────────────┼────────────────┤
  │ Page Up / Down │ ±4 beats (if loaded)                │ ±4 beats                                         │ n/a            │
  ├────────────────┼─────────────────────────────────────┼──────────────────────────────────────────────────┼────────────────┤
  │ a, b, c        │ (any letter) list navigation        │ cue point jumps                                  │ n/a            │
  └────────────────┴─────────────────────────────────────┴──────────────────────────────────────────────────┴────────────────┘


## Local Track Player

Keys work while the Local Track Player has the focus (click it). The track list forwards Space and Page Up / Down, see below.

- Page Up & Page Down - (optionally quantized) ±4 beat jumps
- Space - toggle play/pause
- `a`, `b`, `c` - (quantized) jump to the hot cue A, B or C, if the track has it. The cue arrows in the wave view carry the letter.
- `q` - toggle quantize
- Up / Down - give the focus to the track list
- Double click the BPM - type the BPM to play at
- The `<<` `>` `>>` button tooltips name their keys (Page Up, Space, Page Down)

### Loops

A cue with a loop (a deck loop copied back, or one made in the track editor; `cuemarks.md`) loops the audio:

- Its cue button (or key) jumps to the cue (quantized like any cue) and starts the loop. The button is highlighted while the loop runs.
- Pressing the **same** button again does not move the position: the audio carries on past the loop end. A third press jumps back and loops again.
- Pressing a **different** cue leaves the loop; if that cue has a loop too, it is entered.
- A click on the waveform, a beat jump or stop also ends the loop. Pause keeps it.
- The loop start and end are exact; only the jump into the loop waits for the quantize grid. A 5 ms fade in at the loop start and a 20 ms fade out at the end hide the click when the wrap is not on a zero crossing.

`b` is also the application's "copy BPM" shortcut; the player only claims it while a track is loaded and the player has the focus.

### BPM lock

- A tiny lock button in the top right corner of the BPM display (`img/lock-16x16.png`) toggles the lock. Off at start, not saved.
- While locked, loading a new track keeps the current BPM: the track plays at that BPM, whatever its own BPM is. A track without a BPM loads at normal speed; the lock stays on and the next track is back at the locked BPM.
- The panel does not limit the ratio (it is a preview, if it sounds bad the DJ needs to know). The time stretcher itself still stops at 0.5x .. 2x.
- Nothing else changes: BPM -/+ and a typed BPM still work, and the revert button goes back to the loaded track's own BPM and leaves the lock as it is.

## Track View

These keys remotely operate the Local Track Player and maintains track list focus

- Enter - load track in Local Track Player and trakcrate track view 
- Space - toggle play/pause in the Local Track Player
- Page Up & Page Down - (optionally quantized) ±4 beat jumps
- Up / Down arrows - navigate the list
- Left / Right arrows - side scroll the list

Space and Page Up / Down only act while a track is loaded in the Local Track Player; with nothing loaded Space stays the VDJ's play/pause. While a track is loaded Page Up / Down no longer scroll the list. Letter keys (`a`, `b`, `c`, ...) stay list navigation.
