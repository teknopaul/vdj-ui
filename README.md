![VDJ](img/vdj-64x64.png)
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

The interface for the decks is asked once and saved; Config > *Forget Saved Network Interface* asks again (use it when the NIC changes). The status bar shows
`Prolink:` (the link state), `Master:` (grey when none, the deck-master yellow otherwise) and `AutoIP:` (green with the address whenever the interface has a 169.254.x.x address,
whoever assigned it). When AutoIP gets no address in time and NetworkManager is present, vdj-ui offers to enable link-local on the connection with `nmcli`
(`ipv4.link-local enabled`, left enabled afterwards). Config > *Application Font...* sets the font for the whole app (default Liberation Sans, package `fonts-liberation`).

### Sampler

*View > Sampler* shows the live sampler. It plays in time with the VDJ sequencer (the clock the decks follow), so it is silent while the
sequencer is stopped. Hiding the panel does not stop it. Samples are `.wav` files; *Sample folder...* chooses the folder shown in the tree
(double click a file to hear it). *Add module* adds any number of:

- **Bombomatic**: the sample on every beat (a kick to keep the beat through a breakdown). *1 bar* plays one bar of it.
- **Dembomatik**: the dembow kick. The bar is cut into 16 equal parts and the sample plays on `X . . X . . X . X . . . . X . .` (parts 1, 4, 7, 9
  and 14). *1 bar* plays one bar of it.
  Its *swing* slider (0-127) delays every second sixteenth note (the 2nd and 4th of each beat): 127 is a quarter of a beat, one step.
- **DR-6S**: a drum machine of six voices, BD SD SY CL HH OH, in a row of buttons; the lit one is the voice you are editing. Each voice has its own
  sample (the same chooser with preview and up / down arrows), offset, volume, choke and a pattern of 16 steps, the bar cut in sixteenths:
  click a round step for on (red), click it again for off (yellow), Ctrl+click for half volume (pink); a plain click on a pink step turns it off. The wheel over the editor changes the voice's volume
  (Shift: steps of 10); anywhere else over the module (and over the swing slider) it changes the whole kit's volume. *Choke* cuts every hit of the voice
  off after 1, 2, 4, 8 or 16 quarter beats (off = play to the end) with a fade over 100 ms, e.g. to shorten a long open hi-hat. *Swing* is the same as the
  Dembomatik's. The module takes a whole row of the grid. *Kit*, *Save kit* and *Delete* keep **drum kits** (the six voices and the swing, not the
  module's name, switch or volume) in `drum-kits/` of the config folder, apart from the sampler presets; the kit list is shared by all DR-6S modules.
  The module switch (and its F key) mutes the whole machine; *1 bar* plays one bar of the pattern.
- **Stab player**: the sample on the beats of the bar you choose (the round buttons: red = on, yellow = off). *Reverse sync* makes the sample
  end on the beat instead of starting on it. *1 bar* (green) plays exactly one bar from the next bar line, even when the module is switched off.
- **Acid loop**: a sample that is a 4 beat loop, time-stretched to the sequencer's BPM and started on the bar line. *1 bar* plays one pass.
  The loop's BPM comes from the file (`acid` chunk, or `128bpm` in the name) or the BPM box; with none it is assumed to be 4 beats long.
- **One shot**: *Fire* plays the sample once, on the next beat (*Quantize*) and stretched to the BPM when it is known (*Stretch*).
- **MIDI matic**: a MIDI note on the chosen beats to an ALSA port (device, channel, note). It leaves the *Sampler MIDI* port of the `vdj-ui`
  ALSA client; with no device chosen it goes to whatever is connected to that port.

Every module has a name, an on/off switch and an **offset** of +/- 80 ms to the beat. The arrows next to a sample name step to the previous /
next `.wav` of the same folder (ASCIIbetical); the new sample is used from the module's next hit, so you can flick through kicks live.
*Preset* saves and loads the whole set-up (modules, settings, folder, volume) as JSON in the config folder (`sampler-presets/`); the
working state is kept in `sampler-current.json` and comes back at the next start.

The sampler plays on the output chosen for the Local Track Player. Sounds are rendered ahead of the beat by the sound server's buffer,
40 ms by default; if the kick sounds early or late put `latency_ms=40` (change the number) in `sampler.conf` in the config folder. In debug mode (*View > Debug Panel*)
each sound start is logged with how far it was rendered from its scheduled time (a few microseconds is normal).

### Exporting a pendrive to the decks (LINK export)

Network > *Export the Loaded Database as a LINK Player* makes vdj-ui serve the rekordbox database open in the browser (a pendrive or export folder) to the
other decks like a player with a USB stick in it: they browse it, load tracks and play them. It needs UDP port 111, a privileged port.
See `doc/link-export.md` and `doc/privileged-port-111.md`. (`doc/joining-the-link.md` explains how the virtual player joins the link without the decks complaining.)

#### Virtual pendrives, USB STOP and Insert

The drive viewer has a tool bar with a list of your virtual pendrives (named folders, managed in Network > *Exported Pendrive* > *Manage Virtual Pendrives...*) and the
pendrives plugged into the PC, and the buttons **USB STOP** and **Insert**. Choosing a pendrive only shows it in the viewer; *Insert* exports it to the decks and *USB STOP*
unmounts the exported one, like the buttons on a deck (USB STOP is dimmed when a deck is using the stick and would refuse). **Run with `VDJ_UI_MODEL=XDJ`**: the decks only
follow the switching when vdj-ui announces itself like an XDJ-700. See `doc/link-export.md`.

## Packaging a .deb

```sh
cmake -B build
cmake --build build
make -C build deb        # produces vdj-ui_<version>_<arch>.deb in build/
````

## Man page

```sh
man -l doc/vdj-ui.1
```


![screeshot](img/screenshot-2026-10-25.png)
