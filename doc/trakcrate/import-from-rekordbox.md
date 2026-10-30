# Importing from a real rekordbox pendrive (study)

A user with a Windows/Mac rekordbox library can export it to one pendrive (rekordbox's own "export to device") and import that into our master copy (`trakcrate-db-format.md`). Real rekordbox output is the best data we can get, so **imported values win over anything we generate**, and we record where each piece came from.

## What the import can read today

| Source in the pendrive                         | Reader we already have                                      | Gives us                                                    |
|------------------------------------------------|-------------------------------------------------------------|-------------------------------------------------------------|
| `PIONEER/rekordbox/export.pdb`                  | `pdb.c` (tracks, artists, albums, genres, labels, keys, colors, playlists, playlist entries, artwork) | metadata, playlist tree, ids              |
| `PIONEER/USBANLZ/Pxxx/id/ANLZ0000.DAT`, `.EXT`  | `libcdj/src/anlz` (`anlz_track_*`)                          | beat grid, hot cues, loops, waveforms (PWAV, PWV2, PWV3, PWV4 raw, PWV5) |
| `PIONEER/Artwork/NNNNN/aID.jpg`                 | plain files                                                 | artwork                                                     |
| `Contents/...`                                  | plain files                                                 | the audio, at the path stored in the track row              |
| `DEVSETTING.DAT`, `MYSETTING.DAT`, `djprofile.nxs`, `RBFLTR.DAT`, `exportExt.pdb`, `USBMNG.DAT` | none / raw copy | see `settings-files.md`; keep raw copies |

Not readable yet (no real sample): PSSI phrases, `.2EX` files, memory-point/comment cue fields, history tables (11, 12), `exportExt.pdb` tags. The importer must **keep these raw** (store the original bytes next to the decoded form) rather than drop them, so a later parser can use them.

## Mapping into the master copy

One folder per track under `track-master-copy/<folder>/<track>/`:

```
<track>/
├── track.mp3|wav          copied from Contents/
├── meta.txt               title, artist, album, genre, label, key, bpm, year, rating, colour, comment, ...
├── artwork.jpg            from PIONEER/Artwork (the full-size file)
├── beatgrid.txt           beats from PQTZ + PQT2 (ms and us)
├── cues.txt               hot cues, loops, memory points
├── waveform-*.png         rendered from the imported waveform tags
├── anlz/ANLZ0000.DAT      the original ANLZ files, byte for byte
├── anlz/ANLZ0000.EXT
└── source.txt             provenance record (below)
```

- The original ANLZ files are kept verbatim. Re-export copies them unchanged, which is the safest path: bytes we do not understand (PQT2 `0x2c`, PVBR, PVB2, PCP2 `0x1d-0x1f`) are preserved exactly, and each such file is a free sample for `UNKNOWN_BYTES.txt`.
- Likewise keep the track's original **PDB row bytes** (`pdb-row.bin`) if the row writer needs unknown fields; we then regenerate only the fields we understand.
- Playlists: each rekordbox playlist becomes a folder in `pendrives/<name>/` of `000_` prefixed symlinks, in the exported order. Folder playlists keep their nesting.
- Folder placement in `track-master-copy`: derive from the `Contents/` path (e.g. `Contents/Intapol/UK Dembow/x.mp3` becomes `Intapol/UK Dembow`).

## Provenance and precedence

Every track gets `source.txt` with machine readable lines, e.g.:

```
origin: rekordbox            # rekordbox | generated | edited
imported_from: <pendrive label / path>
imported_at: 2026-10-27T10:15:00Z
rekordbox_version: 4.5.0     # from DEVSETTING.DAT / header, when present
pdb_track_id: 2096
fields: beatgrid=rekordbox cues=rekordbox waveform=rekordbox artwork=rekordbox meta=rekordbox
```

Rules:

1. **Real data is preferred.** A field marked `rekordbox` is never overwritten by analysis. Re-running our analyser (beat grid, waveforms) skips such tracks unless the user explicitly asks to regenerate; the request changes that field's origin to `generated`.
2. **User edits are respected.** If the user edits `meta.txt` or `artwork.jpg` after import, mark the field `edited`; export uses the edited value, a later re-import of the same drive must not clobber it (report the conflict instead).
3. **Per field, not per track.** Origin is tracked per field group (meta, beatgrid, cues, waveform, artwork) because we may generate a waveform for a track that arrived with a rekordbox grid.
4. **Mixed drives are fine.** The exporter writes imported ANLZ files verbatim when all their parts are `rekordbox`, otherwise rebuilds the file from the parts (see `beat-grid.md`, `waveforms.md`).
5. Identity for re-import: use the file content hash of the audio (the rekordbox track id is only unique within that drive). A track already present is updated in place by field rules above.

## Importer checks and safety

- Read-only on the source pendrive; only open files, never write (the source is the user's backup too).
- Validate before importing: PDB page walk completes, every `analyze_path` exists, audio exists at `file_path` (report missing audio, import the metadata anyway, mark `audio: missing`; the 7-track sample in the repo is exactly that case).
- Report counts: tracks, playlists, art, ANLZ pairs, skipped items and why.
- Paths are untrusted: reject `..` and absolute escapes (as `safeRelativePath` does in the dbserver).
- Unicode: names come as UTF-16 or ASCII DeviceSQL strings; write UTF-8 files. Sanitise `/` and control characters for folder names but keep the original title in `meta.txt`.
- Large libraries (thousands of tracks): stream the copy, no loading of everything into RAM.

## Why this also helps the writer

An imported drive is a corpus of rows, ANLZ files and settings made by the real software: it feeds the byte-identical round trip test in `rekordbox-db-output.md` and fills `UNKNOWN_BYTES.txt` with actual values from different rekordbox versions and players. Contributors can attach their own exports (without audio) as test fixtures, as the repo already does for the 7-track drive.

## Open questions

- Do we import the whole library in one go, or per playlist? Real drives from "export playlists" only contain what was chosen, which is what a user expects.
- rekordbox versions differ in PDB details; record the version in `source.txt` and store it with unknown bytes.
- Where track ids must stay stable on re-export to the same set of players (they cache by id?). Test on a deck: reimport, re-export with new ids, does the cue memory or history on the player break?
- Windows/Mac drives are exFAT/FAT32 or HFS+; Linux read support of HFS+ is limited (mount read-only).

## Implemented: the "Import drive..." button

`src/trakcrate/rb_import.{h,cpp}` (`importRekordboxDrive`), dialog `src/ui/trakcrate_rbimport_dialog.*`, button in the trakcrate pane. Nothing watches for drives or prompts when
one is plugged in: the dialog only lists mounted drives (`/media/$USER`, `/run/media/$USER`, `/mnt`) when you open it.

What it takes from the drive (read only):

| Item | Where it goes | source.txt origin |
|---|---|---|
| title, artist, album, genre, label, key, year, track number, rating, colour, comment, remixer, mix name, dates, BPM tag, drive track id | `meta.txt` | `meta=rekordbox` |
| beat grid (first tempo and first beat, microsecond precision from PQT2) | `beatgrid.txt` | `beatgrid=rekordbox` |
| memory points, hot cues, loops | `cuemarks.txt` (hot cues 1-3 stay A-C, the rest are plain marks; loops get a `loop_end_ms=` column) | `cues=rekordbox` |
| PWAV, PWV2, PWV3, PWV5 waveforms | `waveform.bin` (PWV4 is still approximated at export) | `waveform=rekordbox` |
| `PIONEER/Artwork/.../a<id>.jpg` (80x80), else the picture inside the audio | `artwork.jpg` | `artwork=rekordbox` |
| the original `ANLZ0000.DAT/.EXT/.2EX` | `anlz/` byte for byte | n/a |
| playlists | `pendrives/<Folder - Playlist>/` in the drive's order; an existing playlist keeps its tracks and order, tracks it lacks are appended | n/a |
| `DEVSETTING.DAT`, `MYSETTING.DAT` (option, CRC checked) | `user-settings/`, used by later exports | n/a |
| `RBFLTR.DAT`, `exportExt.pdb`, `djprofile.nxs` (separate options, off) | `user-settings/` | n/a |

Master folders come from the path under `Contents/` (`Contents/Intapol/UK Dembow/x.mp3` becomes `Intapol - UK Dembow`). Audio already in the library (same SHA-256) is not stored again.
Tracks whose audio file is missing on the drive are skipped and listed in the report (the doc above suggested importing their metadata anyway; skipped is simpler and nothing can be exported without audio).
Analysis (`Analyse`) leaves `rekordbox` fields alone unless forced.

Checked on a real 2265 track drive (first 40 tracks): all had grids, waveforms and ANLZ files, 35 artwork, 13 cue lists.

Known gaps (not done yet):
- A variable tempo grid is stored as its first tempo only (a warning is listed); the exporter does not yet re-emit the kept `anlz/` files verbatim, it rebuilds ANLZ from `beatgrid.txt`, `cuemarks.txt` and `waveform.bin`.
- Hot cues D-H are kept as marks but not exported as hot cues (the exporter handles A-C).
- PSSI phrases, `.2EX` contents and history tables are kept raw at best (`anlz/`), not decoded.

### Folders: playlists become folders (default)

Importing by the drive's own `Contents/Artist/Album` folders gives hundreds of folders the DJ never made. Now the **playlists** are the folders:

- The dialog lists every playlist of the drive with its track count and two ticks: **Import as folder** and **Import as playlist**, both on by default.
- A track in several playlists is stored once, in the first playlist (natural name order) that is ticked as a folder. The database does not say which playlist is the DJ's master one, so
  untick "folder" for playlists that only repeat tracks of others; they still become trakcrate playlists with all their tracks (untick "playlist" to leave one out).
- A track in no folder playlist goes into one folder named `rekordbox`, or, with "use their folders on the drive", into the folder of its location under `Contents/` (the old behaviour).
- A drive **without playlists** (some DJs work that way) is imported into folders named after its folders on the drive, with no playlists.
- API: `RbImportOptions::playlistFolders`, `folderOff`, `playlistOff`, `looseByPath`; `listRekordboxPlaylists(root)` gives the ids and names for the dialog.
- Playlists (and folders) the library already has are listed with "(already in the library)" and start **unticked** in the GUI. Ticking one adds only the tracks the library lacks (appended at the end); the existing tracks and their order are not touched. The CLI adds new tracks to an existing playlist the same way.
- A repeated import skips tracks it already has (same drive path and size), wherever they were filed. To redo an import with different folder choices, remove the earlier import's folders (or use a new library) first.
