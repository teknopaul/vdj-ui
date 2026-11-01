# Future Features

## Stability


## Easily Identify the descks on screen

The VDJ should have a different shade/colour backgound to pysicaly decks, (very slightly green)

## VDJ become master

It should be possible to adjust the BPM of the VDJ in real time and have decks follow the change, (if the DJ has put them in sync mode).

## File browser

We need to display the current playing track, that is sent in update network as a reference to that deck's database, not as a string.

## Passive monitor mode

It would be good to have  a passive monitor mode, where we don't do the full deck handshake for each player to get sent update packetes, and just monitor the broadcast packets.
Naturally the apps,  functionality will be limited in the mode.  We have similar CLI tools, e.g. cdj-mon

### Track meta data edit options 

Add some very small (just a few pixes) icons at specific time offsets.  
e.g.
- mark on the timeline where the track has vocals.
- mark permanent memory points graphically (not limited by CDJs cue limit)
- change the basic wave color (by default the audio data is represented in white and blue) The user should be able to decide.
- add track comments
- set 0 to 5 stars
- overlay very small icons on the artwork in the top right corner, e.g. to mark tracks as 
  - dangerous (hard to mix)
  - low volume 
  - cheesy
  - has vocals
  - vocal samples
  - especially good
- Option to remove " (Original mix)" in all track titles ona pendrive, beatport adds this to all track titles.
- It would be good to export history playlists from decks or pendrives to laptop, and convert to human readable text if they are not already, to create tracklisting with timestamps

Edits should be done in a way that means the real recordbox software and players can access and modify the database correctly as well. (we can presume no concurrent editing, natch)

### DJ HUD

It would be good to have a separate montor attached to the computer, so a window can be created in the vdjui software application that can
show the playing tracks on each deck, and show the art work of the currently paying track
show the sync info
show the full BPM value
There is sound in, whow a spectrum analizer there is open source code to render a scope the the ~/github_workspace/lmms project to copy from

### Mixxx integration

It would be good to be able run the Linux DJ software mix on the computer and have mixx sync to the playing CDJs, this means modifying mixxx softare and loading our libraries and connecting up Mix to the the VDJ UI somehow.
Mixxx is open source and downloaded at github_workspce/mixxx


