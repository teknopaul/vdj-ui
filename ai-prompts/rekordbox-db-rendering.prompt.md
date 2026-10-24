We need to render rekordbox databases, There are a few different sources

1. Folders on this PC.
2. USB direves plugged into the PC
3. rekordbox dbs available over NFS on a prolink network.

We should start with rendering the local files.

I have an example rekordbox database at
```
/var/tmp/rekordbox-example
```

The UI should let me select a folder ib this computer and this folder should container `./Contents` and `./PIONEER`. 
The rekordbox db is at `/PIONEER/rekordbox/export.pdb`

Initially we just want to render the views.

There is a library written by us `../libcdj` that is to contain the code necessary to parse and read rekordbox databases in c `src/nfs`.
We want this project to use that library but we can extend the library if features are missing there.
The library can already read some of the database structure and print it to stdout.
We would like libcdj to be usable by other applications.

Currently we have no edit features but thats OK first step is to render fully the contents of a rekordbox database on disk and on USB drives.

There is no official spec so we are reverse engineering from data.

This page has doene alot of work already `https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/exports.html`

/home/teknopaul/bzr_workspace/libcdj/doc-internal/rekordbox-db-format.text has some initial db analysis we should keep this doc updated as we discover more
