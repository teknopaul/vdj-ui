# Add to existing rekordbox

Without changing virtual pendrive exports processes, we need a new feature to modify existing pendrives written by other rekordbox or trakcrate instances.

## Add to rekordbox UI 

In the Playlists view right click menu popup  "Add to rekordbox >".
This menu item should have a sub-menu that (ideally dynamically when opened) shows mounted media that contain a `/PIONEER/rekordbox/export.pdb` files to add music to.
Selecting the sub-menu should immediately start the export detailed below without another dialog.
Warn the user if they are trying to export a folder with files that have not been analised (beat grids and waverrforms) User should be able to do the export without analysis files.
Ideally reuse the export progress / status / notifications mechanism that exists for virtual pendrive exports, UI should already support multiple concurrent exports.

## Add to rekordbox process

The way adding to rekordbox should work is similar to export, and with the same goal, to not damage existing pendrives, and survive early device removal events.

First copy the whole `export.pdb` from the pendrive to memory/tmpfs  i.e. a file in `/tmp/` , 
Make sure the export.pdb is a format that we can read, and manipulate, before attempting any changes on the pendrive.
Sanity check the db and it's records, report any error that mean we cannot export.
Given the existing pendrive has a db we can work with, copy the pendrive's db from memory/tmpfs to a disk backup  in `/var/tmp`.

From the selected trakcrate Playlist which was right clicked on, onto the chosen destination pendrive which was selected from the sub-menu.

- Add (if they are missing) audio files to ./Contents/ folder structure
- Add Artwork to the correct location
- Add ANLZ files in to the correct directories (per hashing rules)

Modify the _local_ copy of the `export.pdb` database adding the new playlist, tracks and meta data and pointers to files structure as we do for full exports.
Code can modify the local copy as we do the file copies, or at the end of data transfers whichever is easiest.
We shold be modifying a version in memory, we can presume /tmp is effectivly in memory, or if its easier operate in application memory directly.

When 
- all the _new_ data is in position on the pendrive,
- nothing has been removed or changed yet on the pendrive
- we have a correctly structured new `export.pdb` locally
- we have done what validation we can on the final database.

we can make the one modification necessary in a way that is safe in case of early pendrive ejects.

Copy our new `export.pdb` to the pendrive, (with a different file name)
rename the pendrives existing/old export.pdb file to .bak  (same backup naming procedure as for virtual pendrive exports if possible, for consistency)
rename our new `export.pdb` to the correct path, so that decks use it when the pendrive is plugged in.
These two renames do not need to be an atomic operation, renames are fast. Ideally status UI should indicate that this is about to happen.

## Code for feature

We should have most code for this type or export already?  Tell user if there is a big gap.
It would be good it trakcrate cli had this feature too.
It would be good if any common db editing code could be pushed to `libcdj`. Tell use if any libcdj changes are _necessary_.
Unnecessary but possbible future refactor recommendations for libcdj should be added to `ai-output/refactor-recommendations.md`
As much code as possbible should be tested during make builds.

## Non-requirements

We do _not_ want to import anything to trakcrate from pendrives modified this way.
When "Add to rekordbox" feature is selected, we just want to modify the pendrive adding a new playlist. There should be no changes to our trakcrate database.
Don't change any User Settings files on the device.

