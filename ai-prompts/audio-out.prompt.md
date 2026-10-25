# Audio output features.

Currently we can preview track, audio output works.

We need some simple live sampler features,  adj projects supports some algorythms for "bombomatics" there are simple repeated sample player s in time to the beat grid. We wwant somethign similar
The most simple is a bass drum played on every beat, this is useful for DJs to keep beat going during musical break downs.

Sampler feature should have its own panlel that lives where Local Track Player does, and has a simialr toggole for the view
Features we should support

Selecting a folder, and viewing a tree structure of sample files, we can support .wav only

## Sampler type features

Use should be able to add  one or many of these
Each should have a name and on off switch
We should be able to save DJ preferred combinations of these and all setting in the UI

- **Bombomatic** - play a sample on each beat (typcially a bass drum but could be any sample)
  - User selects sample UI shows selected sample name, button to navigate to choose another
  - User selects offset +/- to the beat
  - one shot button - when clicked the stabs play for one bar only
  - There should be an up/down chooser, Within a sample directory this selects the next or previous sample (in asciibetical order)
    - change to this is immediate on the next beat so that user can select through options to fine on ehtey like
- **Stab player**  - play a sample on the beat
  - UI beat chooser, use can select any of 4 posistions, the first, second, third, or fourth beat
  - We have svg buttons for the chooser in ./img red=on yellow=off  grey=disabled, we will need to conver to png of appropriate size
  - User selects sample
  - User selects offset +/- to the beat
  - up/down chooser
  - one shot button - when clicked the stabs play for one bar only
  - reverse sync- the wample ends ont eh beat ins teaqd of starting on the beat
- **Acid loops** - this is a sample that is already a 4 beat loop
  - the sample need timestreching to match the current BPM
  - it needs quantized start time to the beat
  - one shot button
  - User selects sample
  - User selects offset +/- to the beat
  - up/down chooser
- **One shots** - plays a single sample to the end
  - optional quantized start to the beat
  - optional timestrech if bpm is known from the samples meta data
  - User selects offset +/- to the beat
  in the sample chooser there should be a way to preview samples before selecting it
- **Midimatic** - play out a midi note on the beat
  - choose device midi channel and note
  - UI beat chooser
  - User selects offset +/- to the beat

- **Drum machine** - in the UI called the "DR-6S"
  - Drum machine is a stab player for 1-6 samples each individually controlable.
  - Whole module has a mute like all others
  - It should support loading 1 to 6 samples called BD, SD, SY, CL, HH, OH  (there are svg buttons created for these in on and off posisitions) these should be diplsayed horrizontally
  - When a button is set to on that sample is editable (only one sample editable at a time)
    - user can load a wav
    - volume set per sample, using mouse wheel as before
    - offset per sample
    - updown buttons like the stab Bombomatick 
  - The UI beat chooser, instead of 4 buttons has 16, still over a bar, so positioning notes within the beat is possible. these are displayed horizontall onn thieri own line.
    - each sample has its own pattern
    - ech not has 3 posistions off (yellow), on(red), and half volume(pink). To make a note half volume use should use Ctrl+left click
  - Drum machine also needs a Swing amount
    - Swing a timing algorythm that adds subtle delay to some notes within a BEAT (N.B. changes are per beat not per bar)
    - Where any note falls BETWEEN the on-beat and the half-beat, it is delayed slightly.
    - the delay amount is the a number from 0 - 127, where 0 is no delay and 127 is delay a 1/4 of a beat.
      - N.B. this means swing 127 shifts notes all the way from quaterbeats to half beats.
    - its not much help but there is a Swing algo for LMMs here /home/teknopaul/bzr_workspace/lmms/src/core/HydrogenSwing.cpp, nothing will be reusable but serves as an example. LMMS works in ticks we work in quaterbeats and samples
    01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16
    n  y  n  y  n  y  n  y  n  y  n  y  n  y  n  y  
  - Drum machines should have its on separate ability to save and load presets which we will call "drum-kits", the kit should save all the modules data samples setup and the patterns
  - Choke feature 
    - per instrument
    - a spinner for selecing 0,1,2,4,8,16
    - the numbers are quater beats, the sample is cut off at this point, zero is no choke
    - rather than a crude cut the sample volume is reduced over 100ms
