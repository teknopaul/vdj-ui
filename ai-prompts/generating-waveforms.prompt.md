#Adding new tracks

This is abig job because rekordbox storea lot of meta data about music files.

We need to do analysis of existing dbs to see exactly what is needed

## waveforms

We need to write our own code to generate waverform images. We can borrow code from an opensource project like Audacity that arleady does something similar or write our own.

rekordbox supports various waveforms ( we nee dot verify this analysis is correct)

- a  very small one 
- a larger fixed size one
- one and one with all the detail used when playing deck.

We should try to make our waveforms similar to the rekordbox ones, but they dont need to be identical, we can tune the visually when we have code.

# Beat grids

For this we need to create the ANALZ files.

at least we need to create a simple beat grid.
We can presume from the BPM in an MP3 files meta data that the BPM is constant throughout the track.
So we just need to create a regular spaced grid without analysing the actual audio data.
If we can analyse the dtrack data to fine beat that would be good but its a hard job.
We can presume 4/4 music always, the only variations from a static grid for house and techno will be very slight
Beats fgrids mark 4.4. time but thats no necessarily when each bass drum happens so it can be complicated.




