# Player enhancements

## Cut out acid loops

We need to be able to make 4 beat loops for acid from existing tracks with beat grids

the start of the loop sample should be on a zero crossing
The end of the loop should be exactly 4 beats per the BPM of the track
the tail of the sample whould be faded out over 30ms so it does not click
the sample should be saved as a WAV witht he BPM in the metadata for loading in acid.

The users hould be able to coose a folder to store the loop and from then on the app should remember that folder as the default

## Sync a track to the playing decks

Need so support Copy BPM ( from VDJ if playing or from MASTER if there is one)
Need to timestratch wthe whole track to the new BPM (it might take a while)
Need to support Quantized Play | start
Need to sypport a Sync button that aligns to the beat grid
Need to support the 4 nudge buttons that 
Need to support offset.
Ideally sypport looping for 1 2 or 4 or 8 bars but only if we can do it wihtout audio clicks.

## Making DJ edits

It would be good to support seleceting a range of bars in a track and deleting the range (again avoiding clicks)
The goal is to remove overly long breaks or parts of the track that are not liked by the DJ.
This app should never edit an existing track it should always create ne tracks with the suffic `_eit.`<ext>
If editingt an .mp3 it should create a new .mp3, if editing a .wav it should create a new .wav
The editing process should be done to a temp .wav file and then rendered only if the user is happy with the result. Sometime edits just dont soudn as good as you would have liked them too.

If a track has not got a perfectly regualr gbeat grid, making edits will not work very well we should be able to tell the user if a track has a perfect beat grid and edits will work.
We shoudl war n if not, but still alow the user to try the edit

- To avoid clicks, we need a fe ddifferent optiosnfor `edittype`
  - It should be possible to cross fade over a configurable number of millis at the cut point.
  - It should be possible to fade out the end afof thelast bar by 1 or 2 beats, so when the joinghappens the beat drops nicely.
    - beat dropping should be ona  zero crossing, but since we are fading out we have samples space to play with
  - Ofent when making an edit DJs want to cut out the break downs, The end of the cut point is often where the beat starts again.
	- Should be able to have 0, 1, 2, or 4 beats from the start of the range being cut out played over the top of the start of the beats that come aftewrwards.
      this is used when the start of the part you want to cut out is a reverb tail, so that tail can go over the following beats wihtout affecting the basss kicks.
  - Some times you want to cut out a part you dont like, e.g. a volcal part, to make the track a more clubby edit.
    when cuttin you wnat the track to stay in time so you wnat to cut out an exact number of bars but you jmnight want to start the cut 1 or 2 beats befor or after the bar change point
    naturally if you ccut 2 beats before the bar change you ahve to join 2 gbeats after so you still get 4 beat bars    
  
