# trakcrate library format

Specification of the on-disk library written and read by trakcrate (the part of vdj-ui that keeps a master music library and prepares
rekordbox-format pendrives). This is the format of the **master library**, not of the pendrive: the pendrive layout is the third-party rekordbox
format and is described in `doc/trakcrate/rekordbox-db-output.md`.

Status: version 1, as implemented by `src/trakcrate/` (the reference implementation). Where this document and the code disagree, that is a bug in one of them: please report it.

The words MUST, SHOULD and MAY are used as in RFC 2119.

## 1. Principles

1. **It is a directory tree of ordinary files.** Everything can be read, edited, copied, versioned and synchronised with ordinary tools (`ls`, a text editor, `rsync`, `git`).
2. **Human readable first.** Metadata is UTF-8 text. Binary data is limited to the audio, images and one waveform container.
3. **Tolerant readers.** A reader MUST ignore keys, columns, comment lines and files it does not know, and a writer MUST keep them when it rewrites a file it did not fully understand.
4. **Provenance is recorded.** Every group of data says whether it was generated, imported from real rekordbox or edited by the user (section 7). Real and user data win over generated data.
5. **Atomic writes.** A writer replaces a file by writing a temporary file in the same directory and renaming it over the old one. Readers never see a half-written file.
6. **The master is never changed by an export.** Exporting reads the library and writes somewhere else.

## 2. Layout

```
<library>/
├── user-settings/                      optional files copied from a real player/drive (section 10)
├── track-master-copy/
│   └── <folder>/                       a folder name: any name that is a legal single path component
│       └── <track>/                    one directory per track
│           ├── track.mp3 | track.wav   the audio: exactly one .mp3 or .wav (case-insensitive). May be a symlink.
│           ├── meta.txt                metadata                     (section 4)
│           ├── source.txt              provenance                   (section 7)
│           ├── beatgrid.txt            beat grid                    (section 5)
│           ├── cuemarks.txt            cue marks                    (section 6)
│           ├── vocals.txt              vocal regions                (section 8)
│           ├── waveform.bin            waveforms                    (section 9)
│           ├── artwork.jpg | .png      cover image
│           └── anlz/                   optional: original rekordbox analysis files, byte for byte (section 11)
├── pendrives/                          the library's playlists (the directory keeps its historic name)
│   └── <playlist>/
│       └── symlinks/
│           └── 000_<track>             symlinks to track directories; the numeric prefix is the play order
└── drives/                             user defined pendrives (section 2.4)
    └── <pendrive>/
        └── pendrives/
            └── 000_<entry>/            one entry = one playlist on the exported drive; the prefix is the entry order
                ├── entry.txt           kind: folder|playlist, source: the folder or playlist it was made from
                └── symlinks/000_<track>
```

`track-master-copy/` marks a directory as a library. All files except the audio are optional: a directory holding only an audio file is a valid track.

### 2.1 Names

- A folder, track or playlist name is one path component: not empty, not `.` or `..`, with no `/` and no NUL byte.
- Writers SHOULD replace control characters and `/` by `_`, trim leading dots and spaces and trailing spaces, and keep names at 200 bytes or fewer (never cutting inside a UTF-8 sequence).
- A track directory is named after the audio file's name without its extension. If the name is taken, ` 2`, ` 3`, ... is appended.
- Names are Unicode (UTF-8). Sorting for display and for generated playlists uses a case-insensitive *natural* order: runs of digits compare as numbers (`track2` before `track10`), independent of the locale.

### 2.2 Folders and tracks

A folder is a directory in `track-master-copy/`; a track is a directory inside a folder. Importing a folder of audio files creates a master folder named after
the source folder (a subfolder of the source becomes `<folder> - <sub path>`). Importing a rekordbox drive uses the path below `Contents/` (`Contents/A/B/x.mp3` becomes folder `A - B`).

### 2.3 Playlists

A playlist is a directory `pendrives/<name>/` holding `symlinks/`. Each entry in `symlinks/` is a symbolic link to a track directory, named `<prefix>_<track name>`.

- The prefix is a decimal number, zero padded to at least 3 digits (more when the playlist has more than 999 entries), and defines the order: `000_`, `001_`, ...
- Writers SHOULD create relative links, so the library can be moved or synchronised.
- Entries without a numeric prefix sort after the numbered ones. Dangling links are legal; tools report them and do not delete them.
- Moving or inserting a track renumbers the prefixes so they are consecutive.
- A track may be in any number of playlists. A playlist name is the pendrive/export set name; nesting is expressed in the name (`Folder - Playlist`).

### 2.4 Pendrives

`drives/<name>/` describes what goes onto one pendrive. It is a small library of its own: its `pendrives/` directory holds the **entries**, numbered `000_`, `001_`, ... in the order they appear on the drive.
An entry is a snapshot, taken when it was added, of a master folder (`kind: folder`, tracks in natural name order) or of a playlist (`kind: playlist`, its order), made of the same
`symlinks/NNN_<track>` links as a playlist. Reordering or removing tracks or entries of a pendrive never changes the library's folders and playlists, and deleting a pendrive removes only links.
When a pendrive is exported each entry becomes one playlist of the rekordbox drive, named after the entry; a track in several entries is stored once.
Deleting a master folder removes the links to its tracks from every playlist and pendrive entry (tools MUST warn and list them first).

## 3. Text file conventions

All `.txt` files are UTF-8 without a byte order mark, with `\n` line ends (readers accept `\r\n`).

- A line whose first non-blank character is `#` is a comment. Blank lines are ignored.
- **Key/value files** (`meta.txt`, `source.txt`, `beatgrid.txt`): one `key: value` per line. The key is everything before the first `:`, trimmed; the value is the rest, trimmed.
  In a value, a backslash is written `\\` and a newline `\n`. Keys are lower case ASCII with `_` and `.`. A repeated key: the last one wins. Order is preserved on rewrite.
- **Table files** (`cuemarks.txt`, `vocals.txt`): one record per line, columns separated by a TAB. A line without a TAB is split on blanks (for hand written files). A missing value is `-`.
  A line that is not a valid record is ignored. Extra columns MUST be preserved.
- Numbers use `.` as the decimal separator, no thousands separator.

## 4. `meta.txt`

Free-form key/value metadata. The keys below are defined; others are kept untouched.

| key | value |
|---|---|
| `title`, `artist`, `album`, `genre`, `label`, `key`, `remixer`, `mix_name`, `comment` | text. `key` is the musical key as the user writes it (`Fmaj`, `8A`). |
| `year`, `track_number` | integer |
| `rating` | integer 0..5 (stars) |
| `color` | colour name |
| `bpm_tag` | the BPM found in the file's own tags (decimal). A hint only; the beat grid decides. |
| `date_added`, `release_date` | ISO 8601 date |
| `duration_ms`, `sample_rate`, `bit_depth`, `bitrate` (bits per second), `channels`, `file_size` | integers describing the audio |
| `container` | `mp3`, `wav` |
| `original_filename` | the file name before import |
| `audio_sha256` | lower case hex SHA-256 of the audio file. The identity of a track: the same hash means the same audio. |
| `peak_dbfs`, `rms_dbfs` | measured loudness, one decimal. Waveform heights are NOT normalised with these: they are information only. |
| `rekordbox_track_id` | track id in the drive this track was imported from (informational) |

A missing `title` is displayed as the track directory's name.

## 5. `beatgrid.txt`

A constant-tempo grid.

```
# trakcrate beatgrid
bpm: 128.00
first_beat_us: 28027
confidence: 0.84
analyser: trakcrate-beats 1
```

| key | value |
|---|---|
| `bpm` | beats per minute, decimal. `0` (or absent) means "analysed, no usable grid". |
| `first_beat_us` | time of beat 1 from the start of the audio, in microseconds. May be negative or exceed one beat. |
| `confidence` | 0..1, written by analysers that have one |
| `analyser` | who made the grid (`trakcrate-beats 1`, `aubio`, `rekordbox`) |
| `variable_tempo` | `1` when the source grid had changing tempo and only the first tempo was kept |

Beat *n* (counting from 0) is at `first_beat_us + n * 60e6 / bpm` microseconds. A missing file means the track has not been analysed.
Beat number 1 of a bar is not recorded: players count in groups of four from the first beat.

## 6. `cuemarks.txt`

An unlimited list of marks on the track. Most are only visible in the waveform of an exported drive (a vertical line); up to three may be assigned to real hot cues.

```
# cuemarks
33.0	15051.8	breakdown	A	-
97.0	44200.5	drop	-	#00ff00
```

Columns, TAB separated:

| # | name | value |
|---|---|---|
| 1 | `beat` | beat position counted from the first beat of the grid (0.0 = the first beat), decimals allowed; `-` when the track has no grid |
| 2 | `ms` | position in milliseconds from the start, 0.1 ms precision |
| 3 | `label` | free text without TABs; may be empty |
| 4 | `hot` | `A`, `B` or `C` to make this a hot cue on export; `-` for none |
| 5 | `colour` | `#rrggbb` or `-` |
| 6.. | extra | kept as they are (for example `loop_end_ms=14000` on a loop imported from rekordbox) |

If `beat` and `ms` disagree (the grid changed afterwards), `ms` wins. Marks are written sorted by position. Leading `#` lines are kept.
More than three marks with `hot` set is an error at export time.

## 7. `source.txt`: provenance

```
# trakcrate source
origin.meta: rekordbox
origin.beatgrid: edited
origin.cues: generated
origin.waveform: rekordbox
origin.artwork: generated
imported_from: KINGSTON:/Contents/A/x.mp3
imported_at: 2026-10-27T14:52:34Z
pdb_track_id: 2390
```

`origin.<group>` is one of `generated` (made by trakcrate's own analysis or by import of a bare file), `rekordbox` (taken from a real rekordbox drive) or `edited` (changed by the user). The groups are
`meta`, `beatgrid`, `cues`, `waveform` and `artwork`. A missing entry means `generated`. `imported_from` and `imported_at` (ISO 8601, UTC) record where and when a track came in; `pdb_track_id` is the track id in the source drive's database.

Rules:
1. Analysis MUST NOT replace a group whose origin is `rekordbox` or `edited` unless the user explicitly asks; asking changes the origin to `generated`.
2. A user edit sets the group's origin to `edited`.
3. A re-import of the same audio (same `audio_sha256`) MUST NOT overwrite `edited` groups.

## 8. `vocals.txt`

Regions where vocals are present. The format is defined; no analyser writes it yet.

Columns: `start_beat  end_beat  start_ms  end_ms  kind  confidence`, `kind` defaults to `vocal`, `confidence` to 1. Beat columns are `-` without a grid; the millisecond columns are authoritative.

## 9. `waveform.bin`

The track's full-resolution waveform, at the same resolutions players use, kept in one file. Binary; integers are little endian except where stated.

```
offset 0   "TCWV"
offset 4   u32 version        (1)
offset 8   u32 columns        number of detail columns
offset 12  sections, repeated until the end of the file:
             4 bytes ASCII tag, u32 byte length, <length> bytes
```

Sections (unknown tags MUST be skipped):

| tag | content |
|---|---|
| `PWAV` | 400 bytes, preview over the whole track. Bits 0..4 height 0..31, bits 5..7 whiteness 0..7. |
| `PWV2` | 100 bytes, tiny preview. Bits 0..3 height 0..15. |
| `PWV3` | `columns` bytes, detail at 150 columns per second, encoded like `PWAV`. |
| `PWV5` | `columns` entries of 2 bytes, **big endian**: bits 15..13 red, 12..10 green, 9..7 blue (each 0..7), bits 6..2 height 0..31, bits 1..0 unused. Same rate as `PWV3`. |

`PWV3` and `PWV5` MUST have the same number of entries as `columns`.

**Heights are absolute.** A column's height is derived from the sample peak on a fixed scale (0 dBFS = 31) and is never normalised per track. A quiet track must look quiet: a DJ needs to see tracks that are not loudness normalised.

The colour preview (rekordbox `PWV4`) is not stored: it is derived at export.

## 10. `user-settings/`

Optional files copied from a real player or drive, used by exports instead of built-in defaults: `DEVSETTING.DAT`, `MYSETTING.DAT` (verified before use: their layout and CRC-16/XMODEM are described in `doc/trakcrate/settings-files.md`).
Device specific or personal files (`RBFLTR.DAT`, `exportExt.pdb`, `USBMNG.DAT`, `djprofile.nxs`) may also be kept here; they are never written to an export unless the user asks.

## 11. `anlz/`

When a track is imported from a real rekordbox drive, its analysis files MAY be kept byte for byte as `anlz/ANLZ0000.DAT`, `ANLZ0000.EXT` and `ANLZ0000.2EX`. They preserve data this format does not decode yet. Tools MUST NOT edit them.

## 12. Artwork

`artwork.jpg` or `artwork.png` in the track directory is the cover. It is extracted from the audio on import (or taken from a rekordbox drive), is never overwritten by a re-import, and may be replaced by the user with any image editor.
On export it is scaled to 80x80 JPEG (what rekordbox itself writes). Tracks with identical image bytes share one image on the drive.

## 13. Export identity

Tracks are identified across imports by `audio_sha256`. Track ids and file names on an exported drive are generated at export time and are not stable identifiers.
The export order is: playlists chosen by the user, each in its symlink order; a track in several playlists is stored once.

## 14. Versioning and extension

- There is no version marker file yet; the format described here is version 1. A future incompatible change will add one and this document will describe both.
- New keys and files may be added in later versions. Unknown ones MUST be preserved, never deleted.
- Tools that add their own data SHOULD use their own file name prefix in the track directory (for example `x-mytool.txt`) and the key prefix `x-` in key/value files.

## 15. Validity checklist

A library is valid when:
- `track-master-copy/` exists;
- every track directory has at most one audio file with extension `.mp3` or `.wav`;
- every playlist entry's prefix is numeric and unique within its playlist (otherwise the order is undefined);
- `waveform.bin`, if present, starts with `TCWV` version 1 and its `PWV3`/`PWV5` lengths agree.

Everything else being absent, malformed or unknown is tolerated and reported, not fatal.
