# Future Features

## Stability

Currently the UI crashes after a while
```
double free or corruption (!prev)
Aborted (core dumped)
```

## Easily Identify the descks on screen

The VDJ should have a different shade/colour backgound to pysicaly decks, (very slightly green)

## VDJ become master

It is possible for the software VDJ to become master, the libray code supports this, gui does not yet have this wired up.
Becoming master means the computer can define the BPM and pyhsical players can follow the VDJ's bmp, sync works much more reliably when the VDJ is master.
It should be possible to adjust the BPM of the VDJ in real time and have decks follow the change, (if the DJ has put them in sync mode).
The UI should support a perfectly gradual BPM change to a new value over time. 
- This feature will only will work if the VDJ is currently master, otherwise greyed out 
- Buttons to add or subtract 1 bpm (in the future) that can be clicked repeatedly e.g to increase BPM by 5 click the + button 5 times.
- a numeric entry box for number of bars over which to apply the BMP change when it is applied (default 32 bars)
- an "apply bpm" button that applys the BPM change to the VDJ now, the physical decks that are synced will automtatically follow when they see the change in packets on the network.

## auto-ip setup user interface

I have to run adj's auto-ip script as root.  This UIs autoip does not seem to work
Instead of running a script for autoip it puts a long `bash ....` command in the ps list, this project should have its own auto-ip launch script that UI can use.
ps list should be able to see that it is running.
UI needs to indicate if autoip worked (or not) and what the autoip being maintained is.

## Distribution

Need a .deb build that uses ../deb-template, and builds the software for Ubuntu with an icon and the binaries and scripts needed to run.
The deb should speficy any necessary Depends:  so apt auto installs the application's runtime dependencies

## UI memory

UI should remember the NIC that was selected last time the UI was connected to the nprolink network, so user does not have to select it every time.
In the future there will be other things for the UI to remember such as save directories and so we need a generic app state mechanism if we dont alreayd have one.

## File browser

The UI currently has a randow file browser that shows the home directory of the user, or root its the UI ws run with sudo.
This part of the UI should be able to view music files and metadata in rekordbox databases on remote decks or the local filesystem, or locally inserted pendrives.
It would be good if we could read pendrives in rekordbox format plugged in in to the PC, we need to see if the rekordbox db format is the same locally as over NFS presumably it is.
This file view should be able to chose which deck or which folder on the local machine or pendrive to render the rekordbox database from.
rekordbox database has at least 2 views of the music data, a "folder" view that shows files not in the rekordbox database, and then the "playlist" view that has meta data about the available music tracks (mp3s or wavs)
UI shuld show the track art image, but very small, or only when clicked, since tyipcally DJs have 1000's tracks on pendrives.

NFS mount fails: must be superuser to setup correct NFS bind ports.  We dont want to run the vdj-ui as root if we can avoid it.
Currently NFS is not working completly. Need to resolve issues not running nfs C apis as root, "mount failed bad optin"
When NFS is working...
We should be able to load each physical deck's rekrdbox database from inserted pendrives, and parse the db now, we have the C code.
We need to display the current playing track, that is sent in update network as a reference to that deck's database, not as a string.
Browsing physical decks music database should now be possible.
At least we should show some high level info, indicate if NFS is working or not on a particular deck.

## About menu

Menu > Help > about does nothing, it should show a app name author (teknopaul)
and the image for application can be taken from https://github.com/teknopaul/adj/raw/master/doc/adj-logo-full.png

## Passive monitor mode

It would be good to have  a passive monitor mode, where we don't do the full deck handshake for each player to get sent update packetes, and just monitor the broadcast packets.
Naturally the apps,  functionality will be limited in the mode.  We have similar CLI tools, e.g. cdj-mon

## Rekordbox meta data view

The rekordbox database should have meta data for each track including art work, beat grid and a gif image of a wave representation of the music.
Users need to be able to view the meta in the GUI.

## Rekordbox meta data edit

It would be good to be able to edit rekordbox databases that are available via the filesystem (e.g. local disk copied on rekordbox DBs on pendrives plugged in to computer running the GUI.
The real sowftware does a lot of small writes to the pendrives and can cause damage, since SDD rewrites 16k for each bit written.
Our software should support more intelligent local editing in memory or on disk (via tmpfs) and then updating the pendrives in larger writes that occur less often
Consider using something like the rsync protocol so we dont rewrite the whole DB each time we save changes to a rekordbox database.

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

### Adding new tracks

Support adding new tracks, and all metadata and beatgrid info to a recordbox DB, without using the real rekordbox software.
If we know the BPM we can presume the beat grid, almost all electronic music has fixed grid gaps.
I use LMMS to write music and convert it to MP3, it would be good if  we supported external software generating the beatgrid and waver forms.

### Pendrive cloning

It would be good to be able to clone pendrives to the local hard disk with all tracks and metadata (Its just a case of copying the /Contents and /PIONEER folders off a pendrive and then back on to a different one)  
Then add to the UI the ability to make copies of saved pendrives. So DJs can travel with 2 or more identical pendrives.
I have bash scripts to do this but they are clunky, doing it in teh UI would be better.
Since its just copying files, if both those folders are available over NFS, it might be possible to copy a pendrive that is currently inserted into a player and being used. If it is possibel we should support it in the UI.

### Midi routing

Pioneer Players can behave as usb c onnected midi controllers, it would be good to show players currenlty setup as controllers in the UI somwhow.
And it would be good to be able route midi to software on the same computer to sync different physical and virtual midi devices connected.
It must be possible to route a single player running as a midi controller to control the software VDJ.

It would be good to run a software audio sampler in the software so the computer can play samples and loops in time to the decks.
The vdj-ui has enough information to quantize the start of playing a long sample so that it is exactly in time.
The sampler could be existing software: Playing a sample in time is potentially so easy now that we can build a sampler into the vjd-ui for triggering long samples or loops.
When playing loops the sampler should autocorrect synconization, based on realtime information received from the decks.

### Sound routing

DJs can route sound card outputs and inputs to the external club mixer, via software setup and a physical cables.
It would be good to record the mixer output on the computer (using alsa or pulse on the laptop and a cable from the mixer tot he laptop ) and make MP3s of the whole session, per DJ, with tracklisting of which tracks are in which the MP3 recording.
There should be a DJ change button to split the MP3, so for a 24 hour party we dont have to end up with one 24hours long MP3


### DJ HUD

It would be good to have a separate montor attached to the computer, so a window can be created in the vdjui software application that can
show the playing tracks on each deck, and show the art work of the currently paying track
show the sync info
show the full BPM value
There is sound in, whow a spectrum analizer there is open source code to render a scope the the ~/github_workspace/lmms project to copy from

### playlist exporting

It would be good to be able to save playlist histories by DJ name so after a party we can publish each DJs tracks played in their set.
It would be good to have facilities to publish track list on the internet when DJs are playing on the radio, so listenerts on the radio can see playingtrack and get session track list
Abiltiy for the DJ to enter notes about the currently playing tracks, and then after finishing a session read thees notes and decide if rekordbox metadata changes are needed or if any other action needs to be taken related to the play of the track.

### Mixxx integration

It would be good to be able run the Linux DJ software mix on the computer and have mixx sync to the playing CDJs, this means modifying mixxx softare and loading our libraries and connecting up Mix to the the VDJ UI somehow.
Mixxx is open source and downloaded at github_workspce/mixxx


