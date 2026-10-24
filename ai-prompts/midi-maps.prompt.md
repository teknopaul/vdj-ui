In order to control the application with external physical midi controllers we need a common API.

We should base it on the adj solution, and use a common file format for the controller files for each midi instrument.

e.g. `/etc/adj/xdj700.adjmm`

```
# adj midi map
#
# Pioneer XJD-700L uses channel 1, Controller 80 127 = keydown 0 = keyup
#
# name      ch ctrl  val
start       1  N    0  # play
stop        1  N    1  # cue

nudge_fwd   1  48    +  # platter
nudge_rew   1  48    -  # platter
nudge_ffwd   1  48    ++  # platter
nudge_frew   1  48    --  # platter

#nudge_fwd   1  N    2  # search >>++++++++++++++++++++++++++++++
#nudge_rew   1  N    3  # << search

#nudge_ffwd  1  N    4  # track search >>
#nudge_frew  1  N    5  # << track search

copy_master  1  N   16  # tempo
become_master  1  N  17 # master tempo
```
Code to read this is here

`/home/teknopaul/bzr_workspace/adj/src/adj_midiin.c`

N.B. its important that when we add new control options old files don't stop working.

By sharing the format we can uuse the C app for midi learn.  To configure new devices.


`adj` has a modules system we should we write in idiomatic C++/QT

/home/teknopaul/bzr_workspace/adj/src/adj_mod.c

 e.g.  `/home/teknopaul/bzr_workspace/adj/src/mod/adj_mod_xdj700.c`

we should port all the existing C modules in `/home/teknopaul/bzr_workspace/adj/src/mod/`


We should also port keyboard input using the same keybindings as `adj` in `/home/teknopaul/bzr_workspace/adj/src/adj_keyb.c` if possible.

and add support for minikeyboards, see `/home/teknopaul/bzr_workspace/adj/src/adj_numpad.c`
