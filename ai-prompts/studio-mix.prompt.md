# Studio mix

Studio mixes are pre-prepared whole sessions made of mixing multiple tracks by rather than mixing live, the user can make mixes in advance taking advantage of the click avoidance algo already developed for making edits.
Mixes generally are longer than the edit joins so some new features are needed.
Studio mixes should only be possible for analyzed tracks.

## UI

- We should open a new window for studio mixes with normal OS buttons for maximizing to full screen
- User should be able to select a playlist to create a studio mix.
- UI should render these whole tracks with waveforms, each track should be on its own line across the screen.
  - track heights should be small(50px), medium(100px) or large(200px)
  - The timeline across the top should indicate time in `mins:secs`,  a 2 hour mix should be `120:00`
  - UI should support a play position that is moved by clicking the mouse, start stop preview with space bar.
  - The first track starts at time 0:00:00
  - Following tracks start below that starts at the end of the track above it.
  - There should be a cross over period (mix period) where both tracks are playing, the top one is being mixed out the bottom mixed in. Use should be able to drag the start and end of the mix period.
  - Since the time line is long it should be possible to zoom in and out, and since there may be many tracks vertical scrolling is needed.
  - Beat grid should be rendered.
  - It shold be possible to make micro adjustments to the beat grid of each track, when creating a studio mix.
  - It shold be possible to set the volume change points for the mixes
    - each volume point should be a number from 1 to 100
    - between volume points the change should be gradual
    - it should be possible to make a volume change that is essentially vvertical (but when rendered uses 20ms for click avoidance)
    - Volume changes should be rendered on the trackview as a yellow line with dots on each volume change point.
    - It should be possible to change the volume of the bass, thus we need a simple EQ with one global bass crossover point and EQ slope
    - It shold be possible to set the master volume of the track
- At the start the UI should setup automatic cross-fade mix type with a mix period of 8 bars for each track, so when laoding a playlist there is automatically a working mix.
- Use should then be able to individually edit how each pair ot tracks is mixed in and out.
- User should be able to decide the start and end bar for the mix period in the outgoing and incomming track.  
  - The mix period must be the same for the outgoing and the incoming track. 
  - It should be rendered as an start line and end line and an area overlay at 10Opacity of both tracks.
  - The use can move the start point of the mix period for the outgoing track.
  - The user can define the length of the mix period.
  The outgoing track shold be able to play past the end of the mix period, if the volume markers indicate this so the mix can include the full tal of an outgoing track that may not end on a beat grid mark point.
- Within a track, user should be able to define cuts and loop using the same tools and options as we developed for making edits.
- All changes should support undo with Ctrl+Z
- Since rendering may take time, we need a output status bar similar to the UI for exporting pendrives.
- UI should upport opening saved mix files with a file chooser.
- UI should render a tree of mixes in teh trakcrate db.
- UI should support defining one or more LADSP audio filters to the whole mix, by default we should add a hard knee compressor so we don't have peaks that distort.


## Rendering

- We need a new file format to save the mix information as a flat text file. Internal format should be XML. file extension should `.mix`
  - It should be possible to save the mix without rendering it yet.  (Ctrl+S shold save the .mix file while creating it)
  - Mix files should be saved within the trakcrate database, in a new folder called `mixes`.
  - the file shold container the source playlis tand the list of audio files used, relative to the track crate root
- Output should be rendered to an .mp3 at bitrate 256 VBR.

## Mix types

We shold support some standard mix types, user should be able to apply a standard mix types to a mix period which should render what the mix does, and then the user can edit it.

- `cross-fade` - over the mix period, the outgoing track is faded out, the incomming track is faded in.
- `cross-fade-drop` - over the mix period, the outgoing track is faded out, the incomming track is faded in, but the incomming track has bass volume set to zero for the mixperiod and then the bass comes back to full volum when the mix period is over.
- `bass-drop` - over the mix period, the outgoing track' bass is faded out but main volume is not, the incomming track is faded in with no bass, at the end of the mix period the outgong track volume is set to zero, the bass comes back to full volume for the incomming track.
- `rough-cut` - at the end of the mix period the outgoing track volume is set to zero and the incoming is set to 100% (with minimal click avoidance)
- `drop-cut` - at the end of the mix period the outgoing track volume is faded to zero over one bar, and the incoming is set to 100% at the end of the mix period
- `stop-cut` - at the end of the mix period the outgoing track volume is faded to zero one beat before the end of the mix period, and the incoming is set to 100% at the end of the mix period
- `tail-cut` - after the end of the mix period the outgoing track volume is faded to zero over 2 bars, the incoming is set to 100% at the end of the mix period
- `bass-switch` - the incommong track is faded in over 2 bar before the mix period start but with no bass, half way thought the mix period the bass is switched cutting the bass on the outgoing and set to 100% the bass on the incomming track.

