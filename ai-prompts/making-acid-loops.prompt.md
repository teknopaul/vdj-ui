# Player enhancements

## Cut out acid loops

We need to be able to make 4 beat loops for acid from existing tracks with beat grids

the start of the loop sample should be on a zero crossing
The end of the loop should be exactly 4 beats per the BPM of the track
the tail of the sample should be faded out over 30ms so it does not click
the sample should be saved as a WAV with the BPM in the metadata for loading in acid.

The user should be able to choose a folder to store the loop and from then on the app should remember that folder as the default

## Sync a track to the playing decks

Need so support Copy BPM (from VDJ if playing or from MASTER if there is one)
Need to time-stretch the whole track to the new BPM (it might take a while)
Need to support Quantized Play | start
Need to support a Sync button that aligns to the beat grid
Need to support the 4 nudge buttons that 
Need to support offset.
Ideally support looping for 1 2 or 4 or 8 bars but only if we can do it without audio clicks.

## Making DJ edits

It would be good to support selecting a range of bars in a track and deleting the range and joining the rest of the track at the delete point (again avoiding clicks) and also looping certain bars.
The goal is to remove overly long breaks or parts of the track that are not liked by the DJ.
This app should never edit an existing track it should always create new tracks with the suffix `_edit.`<ext>
If editing an .mp3 it should create a new .mp3, if editing a .wav it should create a new .wav
The editing process should be done to a temp .wav file and then rendered only if the user is happy with the result. Sometimes edits just don't sound as good as you would have licked them too.

Making edits should be done with the user selecting cut and join points at exact bar start and end points in the beat grid.  There must be a beat grid to create an edit.

To avoid clicks at the join point, we need a few different options for how the cut and join is technically performed.  
Its never as simple as just moving samples because loops never start and stop on zero crossings in the audio.

### Cut n Join types

- `crossfade` - It should be possible to cross fade over a configurable number of millis at the cut and join point.
- `fadeout` - It should be possible to fade out the end of the last bar by 1 or 2 whole beats, so when the joining happens the beat drops with more emphasis.
  - beat dropping should look for a zero crossing _before_ the join point, but since we are fading out we have samples space to find a zero crossing before the exact beat grid point. and join the sample early so that the samples between the zero crossing and the beatgrid are played in the empty space
- `tailoverlap` - Often when making an edit DJs want to cut out the break downs, The end of the cut point is often where the beat starts again.
  - Should be able to have 0, 1, 2, or 4 beats from the start of the range being cut out played over the top of the start of the beats that come afterwards.
  - this is used when the start of the part you want to cut out is a reverb tail, so that tail can go over the joined beats without doubling bass kicks.
- `earlycrossfade` Some times you want to cut out a part you don't like, e.g. a vocal part, to make the track a more clubby edit.
  when cutting you want the track to stay in time so you want to cut out an exact number of bars but you might want to start the cut 1 or 2 beats before or after the bar change point because the vocal starts before the bar start point.
  naturally if you cut 2 beats before the bar change you have to join 2 beats after so you still get 4 beat bars.
  earlyfade should supoprt 1,2 or 3 beats.
  
It should also be possible to loop 1 2 4 8 or 16 bars with same options as for cuts and join.

## Naming new edits

When rendering an edit a text file should be generated with aditional info, the beatgrid info, edit author, timestamp.
The file name should be the original file name plus the suffix  _<username>_edit.<ext>
MP# meta data should be editted.
  - if the tiel ends "(Original Mix)" that text should be removed
  - then "(<User> Edit)" should be appended to the track name, Artist list should not be changed.
