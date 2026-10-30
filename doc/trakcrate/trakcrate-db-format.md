# trakcrate: local DB format

UNIX style

i.e. a user editable file system in a base directory

```
trakcrate/
├── scripts/                   optional user scripts, run by hand only (scripts.md)
├── user-settings/             named sets of the player files, never optional on a pendrive (default.txt + <set>/ with the five files)
├── track-master-copy/
│   └── <folder_name>/
│       └── <track-name>/      contains 1 .mp3 or .wav and the meta data in readable formats .txt
│                              and common image format for wave forms (close to what PIONEED use as possible)
│                              and beatgrid analysis.
|                              and artwork (which by default is extracted from .mp3 and .wav when importing tracks
└── pendrives/
    └── <pendrive-name>/
        └── symlinks/              (a virtual pendrive under drives/<name>/ may also hold drive.txt with `settings: <set>`)          list of track symlinks with `000_` prefix to specify ordering
```

We should output, instead of to physical pendrives directly to folders in this PC that then can be cloned to multiple drive.
A real DJ always takes two of more copies to a gig in cases on pendrive breaks.
Support updates to existing pendrives using `rsync`
Support removing  orphaned tracks from Contents (with a warning before doing so)
Support and UNKNON_BYTES.txt file in the master directory for listing the bytes we are goign to have to make up be cause we dont know the protoocl yet,  this means otther users can contribute to these values.

> The official specification of this format is `TRAKCRATE-FORMAT.md` in the project root. This file keeps the original design notes.
