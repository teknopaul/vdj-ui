When we read a rekordbox pendrive the rekordbox viewer knows that the file has loops for queue points and the details,  e.g. `A 0:14.351 loop 1 beats to 0:14,799`

When I copy back the cue points in the trakcrate track view it does no show the loop details.  curmarks.txt in the trakcrate file system has the loop details.

Neither the Local Track Player, nor the trakcrate track view render the loop,

The cue buttons play from the cue points correctly (with quantizing) but dont loop the audio yet.

## UI 

To render the loop we should show the start point as it is, and a shaded area to the end point.   Shaded area should be 25% opacity and the same colour as the cue mark lines.

Editing que marks should require that we have a beat grid.
When editing cuemarks right click has "add cue mark here"  this is correct.
If there is a cuemark that is not a loop yet, before the point in the audio clicked, menu should also have "end loop here". Clicking it should define the end point of the loop of the cue directly before the point.
Cue marks do not have to be on the beat, but loop end points should be fixed beat width per the beatgrid or BPM.
We should not permit a loop to be made unless it is  1/2, 1, 2, 4, 8, 16 or 32 beats long.  When adding a new loop end we should quantize the end point to one of the permitted sizes.
Right clicking in the middle of a loop should offer options to "double loop" and "half loop" cwhich chantes the loop size (staying within accepted lengths).

Right clicking a cue mark that is currently a loop should have the option to "remove loop", cue should become a jump loop and should now accept a new "loop end".

## Testing loops

Local Track Player's cue buttons should respect the loop end and loop the audio.
We may need a small 20ms fade out at the end of the loop to avoid an audible click if the loop points are not on zero crozssing. human should test how it sounds.
We may need a small 5ms fade in at the start of the loop to avoid an audible click. human should test how it sounds.

