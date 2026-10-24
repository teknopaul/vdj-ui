There is what seems to me to be an intrinsic problem with nudge.

If one deck is playing at a reported BMP of 120.00, but is actually drifting a little (.0.0 is not very precices.
to keep another deck in sync 
- first you have to set it to 120.00 bpm
- then you ahve to sstart it at as close to the correct time as possible to start it in sync
- then you ahve to nudge it into sync.

bass drum beats often have a few milliseconds of attack befory they reach peak, to sync drum beart you need to sync the peaks not the sample start.
A human can do this easily and naturally.

After two decks are in sync 

1. either one might drift, either clock might be out.
2. You might also need to nudge the deck if the track itself has millisecond changes in _apparent_ beat point.

in case 1. you need to catch up and make a micro adjustment to the BPM to get the real bpms in sync.
in case 2. the human just need to nudge abit wihtout changing the BPM.

Pioneer decks seem to presume that the BPM is precices, with two decks with matching hardware thats probably a fair assumption.
But when mixing deck types it no longer is.
Pioneer decks are very lowpered CPUs they run on router hardware and busysbox linux inside,  I suspect they use the normal unix clock to timeing and not the monotomic clock.

For our software CDJ we need different modes of operation.

When we are master its the physical player's job to keep sync, and there is nothing we can do to change how they work.  The generally work quite well.
When we are master a human asking for a nudge does NOT want a BPM change, we are master and have a BPM slider to change BPM if we want to.  

When we are not master we are trying to keepin sync, so we need to keep track of a few different things

The reported BPM
The start poinf of the beat
The offset (that the human has decided is neaded to keep the peaks in sync
We also should track the REAL BPM according to our computer.  We can do this by timing BEAT packet arrivals, ther is some network latency but it should be the same for each packet.

If we KNOW that a real decks that is master, is reporting a different BPM to the calcualted BPM
AND the use nudges.
We need the nudges to be presumed correct for timeing (we have to trust the DJs ability)
But if the DJ nudges in the direction of the BPM difference we have detected, we can persume the DJ is compensating for this difference and make a tiny BPM adjustment to match real and reported BPM.
When a DJ nudges we dont know they do it perfectly, but if we know the real and reported BPMs are different we do know they need help with aligning the BPM and the Beat.



