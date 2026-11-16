# Issues

In Recording panel Selecting  for Capture `sysdefault:CARD:Xone43C,DEV=0` and others causes the UI to crash "Aborted (core dumped)",    `default` and `pipewire` work. 

The package `lame` is required, but not installed by the deb.
There is no option to choose a folder to store the recording (Default to ~/Music)
There should be a checkbox to add a LADSP compressor/.
The UI is a is left aligned panel with large spacing. 

## UI  
Instead, UI should be another status bar across the bottom layedout as follows...
    
```
Recording 🔴 | [Capture V] |  L,R VU meters |--------------------- (full width) | Monitor [x] | Compressor [x] | [Select Dir... V] | ( ⏺️ Record ) | ( ⏭️ Split) | ( ⏹️ Stop )
```
  
The status icon should be the UTF-8 grey dot . Or red dot if recording.
It should be warned against if user tries to close the GUI if it is recording.

