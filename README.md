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
spinner (default 0 ms). Same idea as `adj`'s difflock.

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

### DJ Tools ([sanename](https://sanename.org): djtoolz)

*View > DJ Tools* shows the live DJ Tools. It plays in time with the VDJ sequencer (the clock the decks follow), so it is silent while the
sequencer is stopped. Hiding the panel does not stop it. Samples are `.wav` files; *Sample folder...* chooses the folder shown in the tree
(double click a file to hear it). *Add tool* adds any number of:

- **Bombomatic**: the sample on every beat (a kick to keep the beat through a breakdown). *1 bar* plays one bar of it.
- **Dembomatik**: the dembow kick. The bar is cut into 16 equal parts and the sample plays on `X . . X . . X . X . . . . X . .` (parts 1, 4, 7, 9
  and 14). *1 bar* plays one bar of it.
  Its *swing* slider (0-127) delays every second sixteenth note (the 2nd and 4th of each beat): 127 is a quarter of a beat, one step.
- **DR-6S**: a drum machine of six voices, BD SD SY CL HH OH, in a row of buttons; the lit one is the voice you are editing. Each voice has its own
  sample (the same chooser with preview and up / down arrows), offset, volume, choke and a pattern of 16 steps, the bar cut in sixteenths:
  click a round step for on (red), click it again for off (yellow), Ctrl+click for half volume (pink); a plain click on a pink step turns it off. The wheel over the editor changes the voice's volume
  (Shift: steps of 10); anywhere else over the tool (and over the swing slider) it changes the whole kit's volume. *Choke* cuts every hit of the voice
  off after 1, 2, 4, 8 or 16 quarter beats (off = play to the end) with a fade over 100 ms, e.g. to shorten a long open hi-hat. *Swing* is the same as the
  Dembomatik's. The tool takes a whole row of the grid. *Kit*, *Save kit* and *Delete* keep **drum kits** (the six voices and the swing, not the
  tool's name, switch or volume) in `drum-kits/` of the config folder, apart from the DJ Tools presets; the kit list is shared by all DR-6S tools.
  *Save kit > Export to Folder...* copies the (up to six) samples the kit uses into an empty folder, with a `drumkit.json` that loads the kit back
  (the original samples are only read). To use such folders on this or another PC add them in Config > Instruments > *Presets*: their kits
  appear in every DR-6S list (read only; the folder must stay where it is).
  Alt+click (or middle click) on a voice button mutes that voice (it is dimmed); the mute is saved with the tool but not in kits.
  The tool switch (and its F key) mutes the whole machine; *1 bar* plays one bar of the pattern.
- **Stab player**: the sample on the beats of the bar you choose (the round buttons: red = on, yellow = off). *Reverse sync* makes the sample
  end on the beat instead of starting on it. *1 bar* (green) plays exactly one bar from the next bar line, even when the tool is switched off.
- **Acid loop**: a sample that is a 4 beat loop, time-stretched to the sequencer's BPM and started on the bar line. *1 bar* plays one pass.
  The loop's BPM comes from the file (`acid` chunk, or `128bpm` in the name) or the BPM box; with none it is assumed to be 4 beats long.
- **One shot**: *Fire* plays the sample once, on the next beat (*Quantize*) and stretched to the BPM when it is known (*Stretch*).
- **MIDI matic**: a MIDI note on the chosen beats to an ALSA port (device, channel, note). It leaves the *DJ Tools MIDI* port of the `vdj-ui`
  ALSA client; with no device chosen it goes to whatever is connected to that port.

- **FX**: an effect on everything vdj-ui plays itself (the DJ Tools and the local track player), any **LADSPA** plugin with one or two audio
  inputs and outputs (filters, reverbs, delays ... the standard Linux effects, e.g. packages `swh-plugins`, `caps`, `lsp-plugins-ladspa`).
  Pick the plugin in the *effect* list and one slider per knob of the plugin appears; the on/off switch bypasses it. Several FX tools run one
  after the other in the order of the panel. The decks' own outputs do not pass through vdj-ui, so they are not affected. Plugins are
  searched in `$LADSPA_PATH` (else `/usr/lib/ladspa`, `/usr/local/lib/ladspa`, `~/.ladspa`) and in the `ladspa` folder next to the program.
  vdj-ui ships the **Xone filter** (`xone-ladspa.so`, also usable in other LADSPA hosts): the high pass filter of the LMMS *Xone* effect, with
  the same two knobs, *Freq* (the cutoff, 10-14000 Hz) and *Wild* (0.01-6.5, the resonance at the cutoff). Unlike LMMS the resonance fades out
  towards 10 Hz (full from 80 Hz up), so with Freq right down the filter does nothing, even at full Wild.
  In *Controller: Tools* mode the strip of an FX tool drives two of its controls: the **knob** one and the **fader** another, chosen in the *knob* and
  *fader* lists of the tool (default the plugin's first and second, i.e. Freq and Wild), with the usual pickup.

Every tool has a name, an on/off switch and an **offset** of +/- 80 ms to the beat. The arrows next to a sample name step to the previous /
next `.wav` of the same folder (ASCIIbetical); the new sample is used from the tool's next hit, so you can flick through kicks live.
*Preset* saves and loads the whole set-up (tools, settings, folder, volume) as JSON in the config folder (`djtoolz-presets/`); the
working state is kept in `djtoolz-current.json` and comes back at the next start.

The DJ Tools plays on the output chosen for the Local Track Player. Sounds are rendered ahead of the beat by the sound server's buffer,
40 ms by default; if the kick sounds early or late put `latency_ms=40` (change the number) in `djtoolz.conf` in the config folder. In debug mode (*View > Debug Panel*)
each sound start is logged with how far it was rendered from its scheduled time (a few microseconds is normal).

#### Controlling the DJ Tools with MIDI (nanoKONTROL Studio)

A MIDI controller with 8 strips (mute, solo, rec, select buttons, a knob and a fader each, plus track back / forward) can drive the DJ Tools.
Connect it to the *vdj-midi-in* ALSA port that the deck maps use (*Config > Instruments*, or `aconnect`; a second controller can be wired
automatically with `midi_input_djtoolz=<device key>` in the `[instruments]` section of the config). The *Controller: Tools / DR-6S* box in the
DJ Tools panel picks what the strips drive, one mode at a time:

| control | Tools mode (strip n = tool n, the F1..F12 order) | DR-6S mode (strip n = voice n: BD SD SY CL HH OH) |
|---|---|---|
| mute | the tool's on / off | the voice on / off (a muted voice is dimmed; Alt+click or middle click on a voice button does the same) |
| solo | the green button (1 bar / Fire) | - |
| rec / select | next / previous sample in the folder | - |
| knob | offset, -40 ms .. +40 ms (middle = 0); FX tool: its *knob* control | the voice's offset |
| fader | the tool's volume 0-127; FX tool: its *fader* control | the voice's volume |
| track back / forward | - | previous / next drum kit |

With several DR-6S tools the one you touched last is driven (else the first). Faders and knobs use **pickup**: after a mode change, a preset
load or a change made with the mouse they do nothing until they reach the value on screen, so the sound never jumps. A voice mute is saved with
the tool and the presets, not in drum kits. The controller is never given LED feedback.

The mapping is a normal adj `.adjmm` file (`<config folder>/djtoolz-control.adjmm`, or `djtoolz_map=` in the `[midi]` section), the same format
as the deck maps and the one `adj_midilearn` writes, so it can also be the same file as the deck map. Each line is `name channel controller value`
(channel 0 = any, `N` as the controller = a note, `*` = any value; `#` starts a comment). The names are `strip1_mute`, `strip1_solo`, `strip1_rec`,
`strip1_select`, `strip1_knob`, `strip1_fader` ... `strip8_*`, `track_back`, `track_fwd` and `mode_toggle`. Lines starting with `#!` are settings:
`#! source nanoKONTROL` (only events from a MIDI client with that in its name), `#! pickup 1` and `#! rec_is_forward 1` (rec = next sample;
0 swaps rec and select). Other lines (the deck's `start`, `stop` ...) are ignored by the DJ Tools, and `adj` ignores the DJ Tools lines.
**The numbers in the example file the program creates are a guess** (a nanoKONTROL2 style layout); use *Controller...* in the DJ Tools panel:
select a cell, press *Learn* and move or press that control (*learn strip 1 fills the row* fills strips 2-8 with the next numbers), then *Save*.
Once a control is learned the dialog moves on to the next cell down by itself, so you can go through a whole strip column in one go; *Skip* (or the Tab key) leaves a cell as it is and moves on. Saving changes only the lines you edited; everything else in the file is kept. *View > MIDI monitor* shows the raw events.

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
