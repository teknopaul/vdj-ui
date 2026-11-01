# trakcrate: local DB format

UNIX style

i.e. a user editable file system in a base directory

```
<library>/
├── scripts/                            optional user scripts (track-script-*, crate-script-*), run only by hand; see doc/trakcrate/scripts.md
├── user-settings/                      named sets of the player support files (section 10)
│   ├── default.txt                     set: <name>, the set exports use unless told otherwise
│   └── <set>/                          set.txt and DEVSETTING.DAT, MYSETTING.DAT, djprofile.nxs, RBFLTR.DAT, exportExt.pdb
├── track-master-copy/
│   └── <folder>/                       a folder name: any name that is a legal single path component
│       └── <track>/                    one directory per track
│           ├── track.mp3 | .wav | .flac the audio: exactly one .mp3, .wav or .flac (case-insensitive). May be a symlink.
│           ├── artwork.jpg | .png      cover image
│           └── anlz/                   optional: original rekordbox analysis files, byte for byte (section 11)
├── playlists/                          the library's playlists
│   └── <playlist>/
│       └── symlinks/
│           └── 000_<track>             symlinks to track directories; the numeric prefix is the play order
└── drives/                             user defined pendrives (section 2.4)
    └── <pendrive>/
        ├── drive.txt                   optional: settings: <set>, the settings set this pendrive is exported with (section 2.4)
        └── playlists/
            └── 000_<entry>/            one entry = one playlist on the exported drive; the prefix is the entry order
                ├── entry.txt           kind: folder|playlist, source: the folder or playlist it was made from
                └── symlinks/000_<track>
```

`track-master-copy/` marks a directory as a library. All files except the audio are optional: a directory holding only an audio file is a valid track.


We should output, instead of to physical pendrives directly to folders in this PC that then can be cloned to multiple drive.
A real DJ always takes two of more copies to a gig in cases on pendrive breaks.
Support updates to existing pendrives using `rsync`
Support removing  orphaned tracks from Contents (with a warning before doing so)
Support and UNKNON_BYTES.txt file in the master directory for listing the bytes we are goign to have to make up be cause we dont know the protoocl yet,  this means otther users can contribute to these values.

> The official specification of this format is `TRAKCRATE-FORMAT.md` in the project root. This file keeps the original design notes.
