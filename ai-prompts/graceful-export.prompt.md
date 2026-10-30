# Graceful degradation during export

When exporting a pendrive its quite common to change the virutal pendrive on the PC and then want to update the physical device.

Currentty, we show in red "The destination already has Concent (an earlier export)?"

We should show a check box to "Update" the DB, which will update the audio files and write a new rekordbox format DB as follows...

We need to support update gracefully without damaging the device or its current data.

- First we should create the whole rekordbox `export.pdb` in memory.  Its only a couple of megabytes.
- Then we write the audio data (e.g wavs and mp3s)
- Then we write the relevant smaller f(iles artwork and USBANLZ files).
- Then we backup the current /PIONEER/rekordbox/export.pdb file on the device itself to export.pdb.bak if that exists write export.pdb.bak.2  we shoudl keep 3 backups on the device.
- Then we write from memory our new export.pdb to the device

We should have button to Cancel the export.   This should be able to cancel when we are writing the data files but should not interrupt the export.pdb write.
the export.pdb must be whole to be of use.

Currently there is a Cancel button on the write phase in the status bar, but if you click it it just crasehes the UI and you have to reboot.
We need to fix that so cancel works on export, (but does not breka the database)

