The display area UI needs work

It starts with a line of buttons, which are not needed
Play/Pause - not needed , DeckArea has it
Syndc - not needed , DeckArea has it
BPM+ and BPM- - DeckArea has a sipnner
CONNECT & DISCONNECT -  already on the Network Menu


The Green (local) VDJ deck represents this application's vdj, ALSA sequencer, and application state.
This deck should have different controls to other decks.

It should always be first in the list.


It should NOT have a sync button, you can't sync to yourself.

It should have...

Play|Stop - start or stop the sequencer txt changes according to state
Q-Restart - restart seq at the first bar
Sync - state indicate and can be used for unsync
Master - Does nothing it seems? Should initial the become_master protocl
BPM 123.00 - BPM selector
The nudge buttons -20 -10 -1 +1 +10 +20    (moved from the bottom of the screen)
The pitch slider Aligned right  for changing BPM, Currently Broken needs fixing to moving it changes the BPM (some math needed to cal from % to BPM change)
All the BPM Ramp controls on a separate line.

What does "<< Beat" button do? Seems like nothing so remove it (cofnirm its not used)

Other decks have...

Copy BPM - works
Follow - works
"BPM BPM 123.00" - n.b. doubled text
Should have estimated BPM, Calculated by taking a rolling average of received Beat packets, being careful to notice if a packet is dropped. beat pos and par pos should increment by one or reset

Spurious text should be removed

```
Deck 5
-- no track -- 
```

MIDI/ALSA debug should be removed
```
Midi Ttext
alsa debug information
```

The last line of the UI has info ordered strangely, change to this

Difflock button
Offset desired amount and spinner
Offset actual - should print the calculated offset to help with working out what desired should be
Follow Master - button
Following: - text indicating which deck VJD follows
