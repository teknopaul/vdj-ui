# Artwork (study)

Cover art is stored outside the PDB as plain JPEG files; the PDB only holds an id and a path. That makes it the easiest part of the export to improve: on our side the artwork is an ordinary, editable image file in the master copy, and the exporter derives the player files from it.

## What rekordbox does (known)

- PDB table 13 (artwork): each row is `id u32` + path string, e.g. `/PIONEER/Artwork/00001/a1.jpg` (read by `pdb_parse_artwork`; fixture has 2087 rows, paths like `/PIONEER/Artwork/00104/a2061.jpg`).
- The track row carries `artwork_id` (`pdb_track_t.artwork_id`); the album/track "art" shown by the players is looked up through it.
- Directory rule from the paths: `Artwork/<NNNNN>/a<id>.jpg`, with the directory number = `(id-1) / 20 + 1`? **Not yet confirmed**: in the fixture ids 1-4 are in `00001`, id 2061 is in `00104`, which is consistent with 20 ids per directory (2061 -> 103.05 -> 104). Verify against all 2087 rows before relying on it.
- Players request art through the dbserver (`ReqArt`, `RbLibrary::artwork`), which reads the file at the path from the PDB, so the same files serve both USB and network playback. Our server caps a file at 16 MB and rejects unsafe relative paths.
- Real exports are known (from the public Deep Symmetry analysis, not checked against our samples) to hold a second, smaller version per id (`a<id>_m.jpg`, about 80x80) next to the full one (about 240x240?). Our fixtures contain no `_m` paths and the 7-track sample has no `Artwork/` directory at all. **Check the big pendrive for the file names and pixel sizes** before deciding what to generate; the sizes matter because XDJ screens are small and big files make browsing slow.
- Tracks without art have `artwork_id` 0 and no row.
- The test fixtures use fake placeholder files (`FAKE-ARTWORK-34`), not real images, so we have no image sample on disk yet apart from the big pendrive.

## Better than rekordbox: editable on the file system

In rekordbox the art is embedded in the tag, imported once into its database and effectively frozen. We keep it as files in the master copy (see `trakcrate-db-format.md`):

```
track-master-copy/<folder>/<track>/
├── track.mp3
├── artwork.jpg          the image the user edits; any common format accepted (jpg, png)
└── ...                  metadata .txt, waveform images, beatgrid
```

- **Import**: extract the embedded picture from ID3 (APIC) or the WAV tag to `artwork.jpg` once; never overwrite it afterwards, so edits survive a re-import.
- **Edit**: the user replaces `artwork.jpg` with any tool. If the file is missing, no art is exported (id 0).
- **Share by hash**: tracks from one album usually share a cover, and rekordbox shares one artwork id between them. At export, hash the final image bytes and assign one id per distinct image, so a 12 track album stores the file once. (Do not use the directory name for identity: two copies of the same cover must still collapse.)
- **Convert at export, not in the master**: the exporter resizes and re-encodes into the form the players want (baseline JPEG, no progressive, no CMYK, no EXIF rotation, sRGB, size limits to be measured). Players are fussy about JPEG variants, which is a reason not to copy arbitrary user images verbatim.
- **Export is deterministic**: same inputs give the same bytes, so `rsync` updates only change the changed art.
- **Preview**: vdj-ui can show the same file in the track list and detail pane; track_browser currently has no art column.
- A per-album or per-folder `cover.jpg` fallback is a cheap extra: if a track has no art of its own, use the folder's.

## Exporter tasks

1. For each track, resolve the art file (own, folder fallback, none).
2. Hash and de-duplicate; assign ids in first-seen order (stable across exports: persist the id map in the pendrive's local directory so ids do not move on re-export, which also keeps `rsync` diffs small).
3. Write the jpg(s) under `PIONEER/Artwork/<NNNNN>/` in the tmpfs build (see the main doc), then the table 13 rows and `artwork_id` in the track rows.
4. Remove orphaned art when tracks are removed (same warn-first rule as tracks).

## Open questions / data to gather

- The pixel sizes and file names (full and `_m`) in the big pendrive's `Artwork/` folder. Need: sample of a few images (and `identify` output).
- Whether decks fall over on a missing or unreadable art file, or on a non-JPEG under a `.jpg` name.
- Whether the directory numbering rule above holds; whether ids must be dense/ascending.
- Max size players decode over the network (the 16 MB cap in our server is ours, not theirs).
- PNG, WebP, or a very large input: accept, but always re-encode on export.
