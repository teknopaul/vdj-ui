# Sync improvements

## Goals

- Pressing the Sync button should synchronize the ALSA sequencer and DJ Tools to the to the playing decks.
- Should be able to Sync to either deck and to master
  - The Sync button on the Deck panel selects that specific deck
  - The Sync button on the local/VDJ deck syncs to the master (if there is one)
  - If we have follow master we are syncing to whichever deck is master and that should change when master changes
  - If we are in Sync mode pressing the Sync button again should stop syncing
   - Thus Sync state should be visible in the GUI
  - When Sync is selected we should stay in Sync
  - If we become master we should stop syncing, but Sync mode stays on, so if we stop being the master we maintain sync with the new master.
  - Master changes should not results in any immediate BPM or position changes
    - When the DJ changes master they do not expect it to have any effect on the playing tracks immediate timing.
    - Thus offsets may need to be updated to match the reality of the diff between playing ALSA and the new master.
  - When we are master a human asking for a nudge does NOT want a BPM change, there is a BPM slider to change BPM if needed.
  - We should have a checkbox option to Sync to calculated BPM.
    - I will test to see if this makes a significant difference.
    - We need to support BPM changes on a remote master, and following that change
    - This is complicated because we only have the reported BPM when it changes
    - Currently we do auto nudges to stay in time on the last beat of the bar, that might not be often enough when the bpm is being changed.
    - We don't get any indication that BPM "is being changed"  just that a change has happened now.
    - Since we cant tell when a continuous change is being made by the DJ we need to make sure that our sync adjustments are being executed fast enough so that any difference is not noticeable. 
    - I will need to test that before we write any code that tries to compensate for presumed/unknown actual BPM vs reported values.
   
The user/DJ should be able to make micro adjustments to the sync offsets.

When the user presses the Sync button they should Sync to the Beat, user has "back|forward one beat" options to synchronize to the bar. If Quantize is on the jump should happen at the next beat.

Copy BPM just updates the BPM rate of the ALSA player, the Sync button should copy BPM and align/synchronize to the beat.

When master, it should be possible to adjust the BPM of the VDJ in real time and have decks follow the change, (if the DJ has put them in sync mode).

N.B. some of this functionality is in `libcdj` some of it is in `vdj-ui`.

## Offsets

Bass drum beats often have a few milliseconds of attack before they reach peak volume, to sync drum beat you typically need to sync the peaks not the sample start.
A human can do this adjustment easily and naturally. It is almost impossible for a computer to calculate the apparent beat start to millisecond precision.
This difference is currently encapsulated in the `difflock` value.

The DJ Tools will go through Linux audio stack and may well have significant latency if pulse is used, we need to be able to compensate for this audio latency.  (+/-)
- If we are only playing DJ Tools, difflock would be sufficient However, if we are playing both DJ Tools and MIDI, there are two diffs to keep track of.
- DJ Tools latency should be an additional value relative to the ALSA midi clock
- We can presume the latency of the audio stack is consistent and so if the user sets the latency compensation value it should be saved across restarts.

# Post sync adjustments

After the initial setup of a sync when decks have been running together for some time.

1. Either one might drift, timing clocks might be difference across different hardware.
2. You might also need to nudge the deck if the track itself has millisecond changes in _apparent_ beat point.
3. CDJ/CDJ don't appear to keep very precise BPM clocks, our calculated BPMs seem to change quite a lot. (we need to verify that our calculated BPM code is correct and does not have rounding issues)

If we are Syncing to a remote master

in case 1. we need to catch up and _might_ need make a micro adjustment to the BPM to get the real BPMs in sync.
in case 2. we need to nudge a bit without changing the BPM.


