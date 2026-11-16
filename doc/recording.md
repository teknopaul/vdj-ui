# Recording

View > Recording Bar shows one line across the bottom of the window:

```
Recording ⚫ | [Capture v] | L,R meters ------------ | Monitor [x] | Compressor [x] | [📁 folder v] | ⏺ Record | ⏭ Split | ⏹ Stop
```

- **Dot**: grey when idle, red while recording.
- **Capture**: the ALSA capture devices (`default` and `pipewire` follow the system; choose the mixer's own card to record it directly). The choice is remembered.
- **Meters**: left and right input level (RMS bar, peak hold, clip light; click the meter to clear the clip light). The tooltip says whether the level is low, good (peaks between -18 and -6 dB), hot or clipping.
- **Monitor**: show the input level when not recording. It opens the capture device, so untick it when another program needs the input. While recording the meter shows the *input*, before the compressor.
- **Compressor**: runs a LADSPA compressor before the encoder. Needs a plugin: the `caps` package (`CompressX2`), or swh-plugins (`sc4_1882`, `dysonCompress_1403`). The box is disabled, with a tooltip, when none is found. The plugin's default settings are used; set `LADSPA_PATH` to look elsewhere. It applies to the next recording or split.
- **Folder**: the menu has "Select Dir..." and the last five folders. Default `~/Music`. Files are named `session_YYYYMMDD_HHmm.mp3` (`_2`, `_3` when the name exists).
- **Record / Split / Stop**: Split closes the file and starts the next one without a gap (a DJ change).
- Closing the window during a recording asks: *Stop recording and quit* or *Keep recording*.

Settings are in the config section `recording` (`device`, `dir`, `monitor`, `compressor`, `recent_dirs`).

## lame

The mp3 encoder is the `lame` command (a dependency of the .deb: `sudo apt install lame`). Without it Record says so and does nothing.

## A silent or crashing device

Selecting some capture devices used to abort the program ("Aborted (core dumped)"): the capture thread blocked in `snd_pcm_readi` on a device that opens but delivers nothing, and stopping it
after a timeout deleted a running thread. The thread now waits for data in 100 ms steps and is never deleted while it runs. A device that cannot deliver 16 bit audio is reported instead.
Multi-channel interfaces are opened with all their channels; the first two are recorded. To test a device by hand: `arecord -l`, then `arecord -D <name> -f S16_LE -c 2 -r 44100 -d 2 /dev/null`.
