# Player enhancements

Goal make new versions of tracks with loops and jumps that the DJ could do live on real but pre-prepared so the edits are smoother than can be achieved with XDJ/CDJ deckss live.
Tracks created this way are called "Edits", they are simple modifications of an existing track removing some parts and looping others.

## UI

We need to enhance the trakcrate track editor to support creating edits.

There must be a beat grid to create an edit.
UI cut & join points should follow the beatgrid exactly.
UI should support defining 1 or more sequential bars that are to be removed (highlight colour dark grey), marking the cut and join points, and choosing the click avoidance algo.
UI should support defining 1 or more sequential bars to loop (highlight color yellow), marking the cut and join points, choosing the click avoidance algo, and defining the number of times to loop.
UI should be similar to the way cues with loops are marked in the UI, with a 25% opacity area drawn, instead of just a letter each lops is marked with number and able to store parameters for the edit type listed below.
It should be possible to define any number of cuts or loops in a track, preview the whole track and then save it as a new .mp3 or .wav.
Bars can be marked for cut or loop but not both.
User should be able to select preffered output .wav or .mp3, and bitrate (use CBR for mp3), UI hsould remember the users preference.

## Making DJ edits

Support selecting range of bars in a track and deleting the range and joining/moving the rest of the track to the delete point (avoiding clicks) and also looping certain bars.
This app should never edit an existing audio file, it should always create new audio files with the suffix `_edit.`<ext>
The editing process should be done to a temp .wav file and then rendered only if the user is happy with the result.

Making edits should be done with the user selecting cut and join points and loop/edit parameters for each section, previewing the results, then saving.

To avoid clicks at the join point, we need a few different "edit algos" for how the cut and join is technically performed.  
It is never as simple as just removing or repeating samples because loops dont start and stop on zero crossings at exactly the beatgrid points in the audio. 
A naive implementation would results in clicks at the edit points.  We shold support various different edit techniques

### Edit Algos

- `cut` - As close to raw sample edits as possible but with just enough fade out and fade in at the start and end to avoid clicks. (support 5, 10, 20 or 50ms)
- `cross` - Cross fade over a configurable number of beats at the cut and join point. (support 1/2 1 and 2)
- `fadeout` - Fade out the end of the last bar by whole beats, so when the joining happens the volume is already zero. Thus the beat drops with more emphasis. (support 1/2 1 and 2 beats)
- `tailoverlap` - Often when making an edit DJs want to cut out the break downs, The end of the cut point is often where the beat starts again.
  - Should be able to have 0, 1, 2, or 4 beats from the start of the range being cut out played over the top of the start of the beats that come afterwards.
  - this is used when the start of the part you want to cut out is a reverb tail, so that tail can go over the joined beats without doubling bass kicks.
  - its DJs responsibility to use the edit type correctly, if DJ chowse areas with beatb beats would overalpa nd be too loud.
- `earlycut` Some times you want to cut out a part you don't like, e.g. a vocal part, to make the track a more clubby edit.
  - when cutting you want the track to stay in time so you want to cut out an exact number of bars but you might want to start the cut 1 or 2 beats before or after the bar change point, because the vocal starts before the bar start point.
  - naturally if you cut 2 beats before the bar change you have to join 2 beats after so you still get 4 beat bars.
  - earlyfade should supoprt 1,2 or 3 beats.
  - the edit algo is needed to define how to effect the cut at the early beat sepcified, support either `cut` or `cross`

### Loop algos
  
It should also be possible to loop 1 2 4 8 or 16 bars with these edit algos.
Loops must be exactly bars but can start on any beat.

Click avoidance algos to support

- `cut`
- `cross`
- `fadeout`

## Naming new edits

When rendering an edit a text file should be generated with aditional info, the name, beatgrid info, edit author, timestamp.
The file name should be the original file name plus the suffix  _<username>_edit.<ext>
,p3 or wav meta data should be editted.
  - if the title ends "(Original Mix)" that text should be removed.
  - then "(<User> Edit)" should be appended to the track name. N.B. Artist should not be changed.


## Cut out acid loops

We need to be able to make 4 beat loops for acid from existing tracks with beat grids.
Once we can define loops for edits user should be able to select a loop and export it as a .wav

Click avoidance algos to support

- `cut`'
- `cross`
- `fadeout`

Should be able to preview the loop before saving.
The sample should be saved as a .wav or .mp3 with the BPM in the metadata for loading in acid.
Idv3  data should be copied from the source track with the "(<name> <user> edit)" track name suffix, name prompted for when saving.
The user should be able to choose a folder to store the loop and from then on the app should remember that folder as the default.
