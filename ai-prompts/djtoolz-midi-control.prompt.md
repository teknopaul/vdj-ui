We need to be able to control the djtoolz with MIDI

I have a Kork nanoKONTROL studio

There are two separte concerns.

1. Controlling on/off volume and offset of all the djtoolz setup
2. controlling the DR-6S individually

The midi controller can only do one at a time.

The controlller has 8 control lines
Each line has
 
- mute
- solo
- red
- select
- a knob
- a fader/slider

## When controlling all the djtoolz

each line is a different djtool ordered from 1 to max 8

- mute - djtool on off
- solo - one shot (if applicable)
- rec/select forward back of the sample in the current directory (if applicable)
- a knob - offset middle position is no offset   0 = -40ms 127 = + 40m
- a fader/slider - volume from 0 to 127

## when controlling the drum machine

each line is a different on of the 6 samples from 1 to 6

- mute - sample on off
- solo - nothing
- a knob - offset middle position is no offset   0 = -40ms 127 = + 40m
- a fader/slider - volume from 0 to 127 of the sample
- track forward & back should select kits from the same directory as the currebnt kit
