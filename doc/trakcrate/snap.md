# trakcrate-ui as a Snap

The trakcrate library window (`vdj-ui --trakcrate`) packaged as a strictly confined Snap. Plan: `.planning/plans/trakcrate-snap_PLAN.md`.
This first step covers audio and USB sticks only. `vdj-ui` (the deck network features) stays a deb.

## Building the Snap

From the repository root:

```sh
snapcraft --destructive-mode
sudo snap install --dangerous trakcrate-ui_*.snap
sudo snap connect trakcrate-ui:removable-media
```

- `snap/snapcraft.yaml` first builds `libcdj` and `libvdj` from `references/libcdj` (a symlink to the sibling libcdj tree, so a clean/LXD build cannot see it: use `--destructive-mode`), then the `trakcrate-ui` part with CMake.
- The build passes `-DVDJ_SETCAP=OFF -DVDJ_BUILD_TESTS=OFF`, so it never calls `sudo` and builds no tests.
- The yaml has not been built yet: `snapcraft` was not installed when it was written.

## Interfaces

| Plug              | Used for                                             |
|-------------------|------------------------------------------------------|
| `home`            | the library in `~/trakcrate` (visible folders only)  |
| `audio-playback`  | track preview through PulseAudio / PipeWire          |
| `removable-media` | pendrives in `/media/<user>` and `/run/media/<user>` |

No network plug: nothing in the library window needs one.

## Audio

- Track preview plays through libpulse (`pa_simple`), the output list comes from `pa_context_get_sink_info_list`. Both go through the PulseAudio socket that `audio-playback` allows, on PulseAudio and on PipeWire (pipewire-pulse).
- No `alsa` plug is needed. If the sound server is unreachable the output list shows only "Default" and a failed open shows "Audio output: cannot open audio output: ..." in the status line (already the behaviour, no change).

Manual test (not run yet, see the progress file):

1. `sudo snap install --dangerous trakcrate-ui_*.snap`, run `trakcrate-ui`, open `~/trakcrate`.
2. Select a track and press play: sound comes out of the default device.
3. Change the output device in the preview player: playback moves to it.
4. Play two tracks in a row and let the first end by itself.
5. `snap connections trakcrate-ui` shows `audio-playback` connected, and `sudo journalctl -k | grep DENIED | grep trakcrate` shows no sound related denials.

## USB sticks

- Sticks are found by scanning `/media/<user>` and `/run/media/<user>` (a one second timer, no inotify), which the `removable-media` plug covers. The user name now comes from the account (`getpwuid`), not from `$USER` or the home folder name (a Snap's home folder is `~/snap/trakcrate-ui/<revision>`).
- `removable-media` is not connected automatically on a sideloaded Snap: `sudo snap connect trakcrate-ui:removable-media`. The "no rekordbox pendrive mounted" menu entry says so when the app runs in a Snap.
- An export first builds its files in a staging folder. `/dev/shm` is skipped inside a Snap (it may not create files there), so the Snap's private `$XDG_RUNTIME_DIR` (also a tmpfs) is used, and the system temp directory after that.
- The library window has no eject or unmount button and runs no `udisksctl` or `umount`, so nothing is disabled. Eject from the file manager.

Manual test (not run yet, see the progress file):

1. `sudo snap connect trakcrate-ui:removable-media`, insert a FAT32 stick.
2. The stick appears in the pendrive menus of the library window within a couple of seconds.
3. Export a small playlist to it, then eject it from the file manager.
4. Put the stick in a deck, or open it with the rekordbox import to check it reads back.
5. `sudo journalctl -k | grep DENIED | grep trakcrate` shows no denials on `/media` or `/dev/shm`.

## One library for the Snap and the deb

Both open `~/trakcrate`. The window configuration (which library was open, window layout) and the stretch cache are per install: the Snap keeps them under `~/snap/trakcrate-ui/`, the deb under `~/.config/vdj-ui` and `~/.cache/vdj-ui`. The first time the Snap starts with no remembered library it opens `~/trakcrate` if that folder exists; otherwise use **New library** (the dialog starts in your real home folder).

The library lock (`.trakcrate-lock`) stops a second program editing at the same time, so the Snap, the deb and the `trakcrate` command line tool can take turns on the same folder. Manual check (not run yet): open the library in both, create a playlist in one, see it appear in the other, then try to edit from both at once and see the lock message.

## Limits of the Snap

| Feature                 | In the Snap                                                                                                                                                    |
|-------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Crate and track scripts | Run inside the confinement. They see the Snap's programs only, not the host's. The menu says so.                                                               |
| External stretchers     | Programs from `~/.config/vdj-ui/stretchers` or `<library>/stretchers` that are not in the Snap cannot run. The built-in engines (rubberband, soundtouch) work. |
| `trakcrate` command     | Not included. Install the `trakcrate-cli` deb for it; it uses the same library.                                                                                |
| Eject                   | Use the file manager.                                                                                                                                          |
| Network, AutoIP, NFS    | Not in this Snap (no network plug). Use the vdj-ui deb for the decks.                                                                                          |

## Not yet

These items from `ai-prompts/snap-distribution.prompt.md` are not solved here and need their own plan. `vdj-ui` stays a deb until they are.

- Networking and AutoIP (`pkexec`, `avahi-autoipd`, `nmcli`, UDP port 111 for the NFS file service): see `LINUX_NETWORKING.md` and `doc/privileged-port-111.md`
- Joystick (`/dev/input`)
- MIDI and the ALSA sequencer clock
- A trakcrate-only CMake target, so the Snap does not contain the network code
