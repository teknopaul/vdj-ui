# A QT based GUI for vdj

## Building

```sh
cmake -B build
cmake --build build
```

Run the unit and integration tests:

```sh
ctest --test-dir build
```

## Usage

```sh
./build/vdj-ui                  # start the GUI
./build/vdj-ui --debug          # start with the debug packet log panel open
./build/vdj-ui --mock-beat      # run with interior mock beat data (no network)
```

### Keyboard

- `F11` toggle full screen
- `Ctrl+D` toggle the debug packet log panel
- `Ctrl+Q` quit
- Play/pause, sync to master, become master, copy BPM, and pitch nudge have
  configurable shortcuts for low-light operation

### Difflock (the VDJ's ALSA sequencer follows a deck)

The VDJ deck owns an ALSA sequencer (client `vdj-ui`, port `MIDI clock`; connect drum machines to it with
`aconnect`). Its Play button starts and stops the MIDI clock. Click **Sync** on another deck to *difflock* the
sequencer to that deck: the offset between the deck's beat and the sequencer's beat is measured on every beat and,
on the last beat of each bar, the sequencer is nudged (at most 20 ms) towards the offset set in the panel's
spinner (default 20 ms). Same idea as `adj`'s difflock.

- **Follow master** locks to whichever deck is master; **Off** unlocks; **Capture** uses the measured offset as the target
- **-10 -1 +1 +10** nudge the sequencer: the next beat is that many ms longer (+) or shorter (-). While locked the
  target moves with the nudge so the lock keeps the new alignment
- Keys (as in adj): `Shift+U I O P` lock to deck 1-4, `Esc Esc` off, `Shift+M` follow master, `<` `>` change the offset

### Network

Select a network interface to connect to the ProLink network. The application
lists connected decks, shows the master, BPM with two decimal places, and
renders each deck's beat grid aligned across all deck rows. The Network > AutoIP
menu item starts the link-local address helper (avahi-autoipd based,
polkit prompt for root, start/stop toggle). NFS shares from connected
players are mounted under `~/.cache/vdj-ui/mnt/` and released on shutdown.

### Exporting a pendrive to the decks (LINK export)

Network > *Export the Loaded Database as a LINK Player* makes vdj-ui serve the rekordbox database open in the browser (a pendrive or export folder) to the
other decks like a player with a USB stick in it: they browse it, load tracks and play them. It needs UDP port 111, a privileged port.
See `doc/link-export.md` and `doc/privileged-port-111.md`. (`doc/joining-the-link.md` explains how the virtual player joins the link without the decks complaining.)

## Packaging a .deb

```sh
cmake -B build
cmake --build build
make -C build deb        # produces vdj-ui_<version>_<arch>.deb in build/
```

## Man page

```sh
man -l doc/vdj-ui.1
```

## TODO

Project structure

- `cmake` build system for C++
- Goal is to have as much test coveragbe as possible
- C++ wrappers for `libcdj` and `vdj` C code.
- Single Page QT Application, with menu bar, and icons tool bar at the top, and a status bar at the bottom, and configuration via popup configuration dialogs.
- Config and state to reuse `vdj` as much as possible and for GUI specific use a flat file in /etc/ that should be read-write by the normal user (id 1000)
- QT Signals and Slots hooks for VDJs various triggers.
- Render real-time updates from the ProLink network of connected devices.
- Dark mode this will typically used in a low light nightclub environment.
- keyboard operation for runtime operations, again presuming a mouse is complicated in a night club and a backlit keyboard is available.

VDJ player modes and high level choices for participation in the ProLink network

- View & select a network interface for ProLink
- setup/start autoip (requires root)
- Passively listen to beat packets
- Partaking as a VDJ
  - partaking in sync
  - (optionally) NFS read
  - (optionally) NFS publish
- Debug mode where packets are logged along side the user operations, this will aid us developing things like master handoff which is a multi-device handshake
- Ability to restart the VDJ, this is neceassry sometimes because if you restart a physical CDJ it does not like the VDJ on the network when it boots.

Primary function is to render the relative beat position of each deck

- Pioneer decks do this by rendering the beatgrid moving left to right with the central unmoving vertical line being the current point in time.  
  We should render a 4/4 (one bar grid) for each deck including the virtual deck and move a vertical line across it to indicate time.
- UI should support up to 5 decks (typcially 2 physical decks and one VDJ)
- Each deck should occupy a horizontal screen region, one on top of the other, and aligned vertically.  
  This means you can visually see if the line for each deck is moving in sync to the 
  to the deck that is currently being heard, i.e. connected via an audio mixer to speakers.
   
Show state of connected decks

- Show auto IP network information,  i.e. autoip assigned IP of each deck,show open/active ProLink ports on each deck.
- Prominantly show BPM with two decimal places `???.??`, allow manual setting of the BPM of the VDJ.
- Show which is master.
  N.B. ProLink has the concept of on-air but no body uses it, most DJ setups have separate decks and use an analog audio mixer to choose which is playing at a time
  our digital network does not have any information as to which is playing to the audience now.
  We _do_ have a notification of which is "master"  i.e. which is the current time-keeper device. When using a VDJ the VDJ will typcially keep time so the VDJ should 
  bet the top-most deck, then it should be ordered according to player number.
- Show if the rekordbox format NFS link is available on each desk
- there are various deck states from the ProLink network to render such as playing/stopped and source of the music on that deck
- Render the rekordbox data such as tracks and playlists
- render now playing, if it can be determined from network information and the data available over NFS data

Show deck functions
- pitch and job
- set BPM
- play / pause
- sync to down beat
- copy BPM from master
- copy BPM from specific other deck
- become master

Show USB configuration
- allow reconnecting local USB devices such as drum machines to the VDJs alsa midi output
- allow midi input, e.g. defining one XDJ as a midi source. or allow a different midi controller
- show joystick configurations for controlling with a playstation controller
  - tools to configure new USB and new joystics with auto lern capabilities writing device config files
