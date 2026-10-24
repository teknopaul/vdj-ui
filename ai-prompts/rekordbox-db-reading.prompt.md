`.planning/plans/future-features_PLAN.md` specified the need to read rekordbox data from network connected players and pendrives.

It was not done.  It was scoped as follows

## File browser

The UI currently has a random file browser that shows the home directory of the user, or root its the UI ws run with sudo.
This part of the UI should be able to view music files and metadata in rekordbox databases on remote players/decks or the local filesystem, or pendrives inserted in to this computers USB sockets..

We need to read pendrives in rekordbox format plugged in in to the PC, we need to see if the rekordbox db format is the same locally as over NFS presumably it is.
This file view should be able to chose, which deck or which folder on the local machine or pendrive to show the contents of the rekordbox database from.
rekordbox database has at least 2 views of the music data, a "folder" view that shows files not in the rekordbox database, and then the "playlist" view that has meta data about the available music tracks (mp3s or wavs)
UI should show the track art image, but very small, or only when clicked, since tyipcally DJs have 1000's tracks on pendrives.

NFS mount fails: must be superuser to setup correct NFS bind ports.  We dont want to run the vdj-ui as root if we can avoid it.
Currently NFS is not working completly. Need to resolve issues not running nfs C apis as root, "mount failed bad optin"
When NFS is working...
We should be able to load each physical deck's rekrdbox database from inserted pendrives, and parse the db now, we have the C code.
We need to display the current playing track, that is sent in update network as a reference to that deck's database, not as a string.
Browsing physical decks music database should now be possible.
At least we should show some high level info, indicate if NFS is working or not on a particular deck.

## About menu


`/media/teknopaul/TEKNOPAUL64/` is the filesystem of a currently pluggined valid pendrive creted by the real Pioneer rekordbox app.

You can referse engineer the layout of the file storage from this. Tts just directories with specific naming conventiona AFAICS.

I beleive that the rekordbox database file is  `/media/teknopaul/TEKNOPAUL64/PIONEER/rekordbox/export.pdb` (verify this) And I hope thats exactlythe same file that gets read over the network by players.

Invesitgate the layout and write a mardown document `./doc/pendrive-format.md` detailintg the pendrive storage layout and DB format.

Document the db files internal format by just referencing code on this laptop and github.
