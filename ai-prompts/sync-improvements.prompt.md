# Sync improvements

Goals

- Pressing the Sync buttton should synchronise the ALSA sequencer to the to the playing decks.
- Should be able to Sync to either deck and to master, 
  - the Sync buttong on the Deck panel select that deck
  - the Sync button on the local/VDJ deck syncs to the master (if there is one)
  - If there is no master we the Sync button on the cdj deck should be desabled
  - If we have follow master we are syncing to which ever deck is master and that should change when master changes, if we become master we should stop syncing
- ALSA should not drift
  - When Sync is selected we should stay in Sync
  - If we are in Sync mode pressimg the Sync button agains shold stop syncing
  - Thus Sync state should be visible in the UI

When the user presses the Sync button they should Sync to the Beat, user has "back|forward one beat" options to synchronize to the bar. If Quantize is on the jump should happen at the next beat

Copy BPM just updates the BPM rate of the ALSA player, the Sync button should copy BPM and align/synchronize to the beat.

When master, it should be possible to adjust the BPM of the VDJ in real time and have decks follow the change, (if the DJ has put them in sync mode).
