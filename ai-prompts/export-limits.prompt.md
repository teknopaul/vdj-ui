# Pendrive export limits

We need to implement the same limits that real deck have or they will run out of memory.

These are theoretical limits and most DJs will never reach them but its important not to break pendrives.
N.B these are limits for pendrive exports not for our `trakcrate` db we have no limits in our local database. even if rekordbox does have.

Old decks

Artists: 10000
Albums : 10000
Tracks : 10000

Newer decks

Albums : 16000
Tracks : 20000
Artists: 16000

I don't know which decks suppport which limits, so we should default to the lower limits they are ample.
We need a UI option if we hit the limits to "Apply modern deck limits YMMV" then raise to the higher limits
trakcrate CLI needs to support `--raise-limits` to support exports between the lower and upper limits.

When a user tries to export a pendrive above these limit we should refuse, with information about why and let the user fix their lpaylists them selves in the existing tool.

update...

- code for the vdj-ui
- trakcrate cli
- documentation in doc/trakcrate/rekordbox-db-output.md
