# Studio mix

Studio mixes are pre-prepared whole sessions made of mixing multiple tracks by rather than mixing live, the user can make mixes in advance taking advantage of the click avoidance algo already developed for making edits.
Mixes generally are longer than the edit joins so some new features are needed.
Studio mixes should only be possible for analyzed tracks.

## UI

- We should open a new window for studio mixes with normal OS buttons for maximizing to full screen.
- User should be able to select a playlist to create a studio mix.
- UI should render these whole tracks with waveforms, each track should be on its own line across the screen.
  - track heights should be small(50px), medium(100px) or large(200px)
  - The timeline across the top should indicate time in `mins:secs`,  a 2 hour mix should be `120:00`
  - UI should support a play position that is moved by clicking the mouse, start stop preview with space bar.
  - The first track starts at time 0:00:00
  - Following tracks start below that starts at the end of the track above it.
  - There should be a cross over period (mix-period) where both tracks are playing, the top one is being mixed out the bottom mixed in. Use should be able to drag the start and end of the mix-period.
  - Since the time line is long it should be possible to zoom in and out, and since there may be many tracks vertical scrolling is needed.
  - Beat grid should be rendered.
  - It should be possible to make micro adjustments to the beat grid of each track, when creating a studio mix, this shold not affect the saved beatgrid info.
  - It should be possible to set the volume change points for the mixes
    - each volume point should be a number from 0% to 100%
    - between volume points the change should be gradual
    - it should be possible to make a volume change that is essentially vertical (but when rendered uses 5ms for click avoidance)
    - Volume changes should be rendered on the trackview as a yellow line with dots on each volume change point.
    - It should be possible to change the volume of the bass on each track, thus we need a simple EQ with one global bass crossover point and EQ slope.
    - It should be possible to change the volume of the high-frequencies on each track, EQ with one global midrange crossover point and EQ slope.
    - It should be possible to set the master volume of each track
- At the start the UI should setup automatic cross-fade mix type with a mix-period of 8 bars for each track, so when loading a playlist there is automatically a working mix.
- Use should then be able to individually edit how each pair of tracks is mixed in and out.
- User should be able to decide the start and end bar for the mix-period in the outgoing and incoming track.  
  - The mix-period must be the same for the outgoing and the incoming track. 
  - It should be rendered as an start line and end line and an area overlay at 10% opacity of both tracks.
  - The user can move the start point of the mix-period for the outgoing track.
  - The user can define the length of the mix-period.
  The outgoing track should be able to play past the end of the mix-period, if the volume markers indicate this so the mix can include the full tail of an outgoing track that may not end on a beat grid mark point.
- Within a track, user should be able to define cuts and loop using the same tools and options as we developed for making edits.
- All changes should support undo with Ctrl+Z
- UI should support opening saved mix files with a file chooser.
- UI should render a tree of mixes in the trakcrate db.
- UI should support defining one or more LADSP audio filters to the whole mix, by default we should add a hard knee compressor so we don't have peaks that distort.


## Rendering

- Each mix needs a name, author and optional Image for the mp3 metadata.
- We need a new file format to save the mix information as a text file. Internal format should be XML. file extension should `.mix`
  - It should be possible to save the mix without rendering it yet.  (Ctrl+S should save the .mix file while creating it)
  - Mix files should be saved within the trakcrate database, in a new folder called `mixes`.
  - the file should contain the source playlist name and the list of audio files used, relative to the trakcrate root
- Output should be rendered to an .mp3 at bitrate 256 VBR.

## Mix types

We should support some standard mix types, user should be able to apply a standard mix types to a mix-period which should render what the mix does, and then the user can edit it.

- `cross-fade` - over the mix-period, the outgoing track is faded out, the incoming track is faded in.
- `cross-fade-drop` - over the mix-period, the outgoing track is faded out, the incoming track is faded in, but the incoming track has bass volume set to zero for the mix-period and then the bass comes back to full volume when the mix-period is over.
- `bass-drop` - over the mix-period, the outgoing track' bass is faded out but main volume is not, the incoming track is faded in with no bass, at the end of the mix-period the outgoing track volume is set to zero, the bass comes back to full volume for the incoming track.
- `rough-cut` - at the end of the mix-period the outgoing track volume is set to zero and the incoming is set to 100% (with minimal click avoidance)
- `drop-cut` - at the end of the mix-period the outgoing track volume is faded to zero over one bar, and the incoming is set to 100% at the end of the mix-period
- `stop-cut` - at the end of the mix-period the outgoing track volume is faded to zero one beat before the end of the mix-period, and the incoming is set to 100% at the end of the mix-period
- `tail-cut` - after the end of the mix-period the outgoing track volume is faded to zero over 2 bars, the incoming is set to 100% at the end of the mix-period
- `bass-switch` - the incoming track is faded in over 2 bar before the mix-period start but with no bass, half way though the mix-period (on a bar boundry) the bass is switched cutting the bass on the outgoing and set to 100% the bass on the incoming track.

