We should matche the folder view to what is seen in the info tab on real decks

-track name in the tree
  - artist
  - bpm
  - waveform
  - rating stars
  - comment
  
If possible these should be editable. (not the wave form)

To edit recordbox db there are lots of small changes to make that damages SDD disks with namy small writes.

To avoid this we should have edit mode that copies the whole DB to a tmpfs (e.g. /tmp)
Edits are made in a session to the tmpfs version
Saving is a single write back to the original location, keepin g the old one as a backup incase the write fails
