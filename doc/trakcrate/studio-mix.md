# Studio mix

A studio mix turns a playlist into **one mixed mp3**: every track is stretched to one master tempo, the tracks are overlapped in *mix periods*, and volume, bass and high curves decide what you hear. It is made in the **Studio mix** window (File > Studio mix... in the trakcrate window, View > Studio mix... in the main window). Only analysed tracks (waveform and beat grid) can be used; the new mix dialog shows the others in red with the reason and has an **Analyse** button.

## The window

- **Lanes.** One lane per track, in playlist order, each starting where its mix period with the previous track puts it. The lane shows the *edited* track (after stretching, cuts and loops) as the coloured waveform, bar lines on the ruler and the track name with `bpm <- original bpm, stretch %`.
- **Mix periods.** The two ends of a pair are drawn over both tracks at 20 % opacity. Drag a period's start on either track (it snaps to bars; hold **Shift** to snap to beats instead) and the later tracks follow, because a track starts where its period lines up with the outgoing one. Drag the right end to change the length. A period is always inside both tracks.
- **Curves.** Volume (yellow), Bass (red), High (cyan), 0 to 100 %, drawn on the selected lane. Keys **1 / 2 / 3** choose the curve (or the radio buttons in the Track dock). Click on the line to add a point, drag a point (snaps to beats; **Alt** drags freely, **Ctrl** locks the beat, **Shift** locks the value), **Delete** removes it. Two points on one beat make a vertical change; no change is ever faster than 5 ms.
- **Navigating.** Ctrl+wheel zooms, the wheel scrolls, the zoom slider and **Fit mix period** (key **F**) centre the period nearest the play position with 8 bars of each track on both sides; **[** and **]** go to the previous / next period.
- **Playing.** **Space** plays the mix from the green line (click the ruler or a lane to place it; a click while playing jumps there) and stops again; **Home** goes to the start. The tracks are prepared in the background: the first start of a track waits ("Preparing track N...") while it is stretched, later ones are cached. A track that is not ready in time is silent and the status bar says so. Any edit stops the playback.
- **Removing a track.** Right click a lane > *Remove track N from the mix*. The track stays in the playlist; only the mix drops it. The tracks on both sides get a fresh 8 bar period (of the same type as the pair before the gap) and the curve points of the two old periods are dropped. Undo brings the track back. A mix keeps at least one track.
- **Undo.** Every change is one undo step (Ctrl+Z / Ctrl+Shift+Z).

## Mix types

Right click a period to choose how it is mixed. A type writes curve points into both tracks of the pair (you can edit them afterwards; the type is only the starting point). `o0`/`oE` are the start and end of the period on the outgoing track, `i0`/`iE` on the incoming.

| type | outgoing track | incoming track |
|---|---|---|
| `cross-fade` | volume 100 -> 0 across the period | volume 0 -> 100 across the period |
| `cross-fade-drop` | as cross-fade | as cross-fade, bass is out until `iE` |
| `bass-drop` | volume 100 until `oE`, then 0; bass fades out across the period | volume fades in; bass is out until `iE` |
| `rough-cut` | full until `oE`, then cut | silent until `iE`, then full |
| `drop-cut` | full until 4 beats before `oE`, fades to 0 at `oE` | silent until `iE`, then full |
| `stop-cut` | full until 2 beats before `oE`, fades to 0 one beat before `oE` | silent until `iE`, then full |
| `tail-cut` | full until `oE`, then fades out over 8 more beats | silent until `iE`, then full |
| `bass-switch` | full, bass cut halfway through the period | comes in 8 beats early with no bass, bass in halfway |

Assumptions to check by ear: `stop-cut` fades over one beat that ends one beat before `oE`; `bass-switch` keeps the outgoing at full volume until `oE`; the cut types keep the incoming silent until `iE`, so the period is the outgoing track playing alone with the incoming waiting (draw the incoming volume up if you want it earlier).

## Master tempo and stretching

**Master BPM** (toolbar, 40 to 300) is the tempo of the whole mix; it starts at the first track's tempo, **Use track's bpm** copies the selected track's. Every track is stretched to it before anything else happens, so beats stay where they are when the tempo changes. Changes of more than 8 % are marked with a warning on the lane, more than 2x (or 0.5x) cannot be stretched. A track can opt out of stretching in the Stretch dock ("do not stretch"); the mix then warns when two tracks of a pair differ by more than 2 %.

The **Stretch dock** chooses the engine for the mix and, per track, an override, with the options of the engine. **Re-stretch** redoes the selected track, **Audition...** stretches an excerpt (the mix period plus 4 bars either side) with the engines you tick and plays them one after the other so you can compare on your own music (**blind** hides the names until the end).

| engine | notes |
|---|---|
| `rubberband-lib-r3` / `rubberband-r3` | Rubber Band R3 (library / command line), pitch preserved, best transients, needs `librubberband-dev` (library) or `rubberband` |
| `rubberband-lib-r2` / `rubberband-r2` / `ffmpeg-rubberband` | R2 engine: lighter, but a steady low tone drifts by about the stretch ratio |
| `soundtouch` | SoundTouch, in process, needs `libsoundtouch-dev`; onsets smear a little at large ratios |
| `sox-tempo` | `sox tempo`, needs `sox` |
| `ffmpeg-atempo` | the ffmpeg filter, always available with ffmpeg |
| `wsola` | built in, simple |
| `varispeed` | resampling: no artefacts, but the pitch follows the tempo (fine for a few percent) |

Engines that are not installed are greyed out in the dock. Install: `sudo apt install rubberband-cli sox librubberband-dev libsoundtouch-dev`.

### Your own engine: `*.stretch`

Put a text file ending in `.stretch` in `<library>/stretchers/` or `~/.config/vdj-ui/stretchers/` and it appears in the dock when its program exists. `doc/trakcrate/example.stretch` shows every key: `id`, `name`, `description`, `command` (split on spaces, quotes group words, no shell; `{in}` is a 32 bit float WAV, `{out}` where the program must write, `{ratio}` target bpm / source bpm, `{bpm_from}`, `{bpm_to}`, `{opt:name}`), `pitch: preserved|follows` and `option.<name>:` lines (`a|b|c` for a choice, `lo..hi=default` for a number).

### Cache

Stretched tracks are kept in `~/.cache/vdj-ui/stretch` (or `$XDG_CACHE_HOME/vdj-ui/stretch`), keyed by the audio, engine, options and ratio, at most 4 GB (the oldest are removed). Deleting the folder is always safe; the tracks are stretched again when needed.

## EQ, trim, filters

- **Trim** (Track dock, 0 to 200 %) is the level of one track.
- The bass and high curves use a 3 band crossover (Linkwitz-Riley, 12, 24 or 48 dB per octave, default 24) at 120 Hz and 2.5 kHz. The bass curve scales the band below the lower frequency, the high curve the band above the upper one.
- **Master filters** (a dialog): a built-in compressor + limiter (threshold -3 dBFS, 10:1, 1 ms attack, 80 ms release, 2 ms lookahead; on by default) followed by any LADSPA plugins you add, in order. A plugin that cannot be loaded is skipped with a message.

## Cuts and loops, grid nudge

The **Cuts and loops** dock is the track editor's panel for the selected track (the options are those of [track edits](track-edits.md)). Press **R** (or the button), drag on the track to mark a range on its beats, then Cut or Loop. Regions are applied on the stretched track; curves and periods keep their beat positions, so a cut that shortens a track clamps them into the track (the status bar says so). Orange ticks at the bottom of a lane mark where cuts and loops join.

**Nudge** (Track dock) moves one track's beat grid by +-1 ms or +-0.1 ms for this mix only. `beatgrid.txt` is never changed.

## Saving and the `mixes/` folder

Ctrl+S asks for a name, author and cover image the first time and saves `<library>/mixes/<name>.mix`; later it overwrites silently. Save As takes `Folder/Name`. The **Mixes** dock lists `mixes/` and its folders (double click opens, right click: Open, Rename, Delete, New folder; nothing outside `mixes/` is touched). The title shows `*` while there are unsaved changes. A track that has gone from the library opens as a red MISSING lane, and the mix cannot be rendered until it is replaced or removed. The source track folders are only ever read.

### The `.mix` format

XML, version 1. Positions are in beats of the edited track (stretched to the master tempo, then cuts and loops applied).

```xml
<mix version="1" rate="44100">
  <name>Friday warmup</name><author>Alice</author><image>covers/friday.jpg</image>
  <playlist>House</playlist>
  <tempo masterBpm="124"/>
  <stretch engine="rubberband-r3"><option name="formant" value="preserved"/></stretch>
  <eq bassHz="120" highHz="2500" slopeDb="24"/>
  <filters>
    <filter kind="builtin-compressor" on="1" threshold="-3" ratio="10" attack="1" release="80"/>
    <filter kind="ladspa" plugin="caps.so|Compress" on="1"><control name="..." value="..."/></filter>
  </filters>
  <tracks>
    <track file="House/Foo/track.mp3" trim="100" gridNudgeMs="0">     <!-- stretch="off" keeps the track's tempo; a <stretch> child overrides the engine -->
      <regions><region kind="cut" start="32" len="16" algo="cross" param="1" times="1" earlyShift="0" earlyAfter="0" earlyJoinAlgo="cut" earlyJoinParam="0"/></regions>
      <curve name="volume"><p b="0" v="100"/><p b="224" v="0"/></curve>
      <curve name="bass"/><curve name="high"/>
      <next outStart="224" inStart="0" len="32" type="cross-fade"/>   <!-- the period with the following track; none on the last track -->
    </track>
  </tracks>
</mix>
```

`file` is relative to the library root (absolute paths are accepted), `image` likewise. `p b` is the beat, `v` the percent (0 to 100). `type` is one of the names in the table above. Unknown filter kinds are ignored with a warning.

## Rendering

File > **Render MP3...** asks for name, author, cover image (a thumbnail is shown) and folder (remembered; the author too), shows the file name and length it will get, warnings (missing or unanalysed tracks, periods outside a track, tracks that differ by more than 2 % in tempo) and a progress bar with Cancel. The file is `<name>.mp3` (never overwritten: `<name>_2.mp3`), 44.1 kHz stereo, average 256 kbps VBR, written as `.mp3.part` and renamed when complete, so a cancel or failure leaves nothing. Tags: title = mix name, artist = author, album = playlist, comment "Studio mix", picture = the cover, tempo = master BPM. The name, author and image are stored in the mix.

## Limits

One constant master tempo for the whole mix; a constant tempo per track (the beat grid is constant); no key matching or shifting; float processing inside the mix with no dithering; the preview prepares the tracks near the play position, so a jump far away waits for the track and a very long track may start a little late.
