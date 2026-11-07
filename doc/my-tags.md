# My Tags and `exportExt.pdb`

What is known about the rekordbox "My Tags" feature and how it is stored on a drive. Findings from the KIOXIA stick (a rekordbox-managed export with custom tags on tracks of the Bezier playlist) and from rekordcrate's `src/pdb/ext.rs`. Nothing here is implemented in trakcrate yet: the settings viewer still lists `exportExt.pdb` as "contents: not decoded" (`describePdb` in `src/trakcrate/settings_view.cpp`), and the file is copied from the settings set as it is.

## What the file is

`PIONEER/rekordbox/exportExt.pdb` is the *My Tags* database: tags and categories the user makes up to classify tracks beyond the standard genres. rekordcrate describes a tag row as "a tag or category that can be assigned to tracks for the purpose of categorization".

It is a DeviceSQL database like `export.pdb` (same page model, 4096 byte pages, same header: page size at 0x04, table count at 0x08, table pointers from 0x1c), but with its own tables. The shipped default (`data/trakcrate/user-settings/exportExt.pdb`) has 18 pages and 9 table entries:

| Table type | Contents                                  | First, last page (KIOXIA) |
|------------|-------------------------------------------|---------------------------|
| 3          | Tags and categories                       | 7 (index), 8 (data)       |
| 4          | Track to tag junction                     | 9 (index), 10 (data)      |
| 7          | one row of unknown meaning, the same bytes as in the default template (`78 f4 43 1e 03 22 23 24 25 26 03 03 03 03 03 00 ...`) | 15 (index), 16 (data) |
| 0, 1, 2, 5, 6, 8 | no rows in any file seen; purpose not known | one empty page each |

Page 7 and 9 are index pages (page flags `0x64`, no rows); 8 and 10 are data pages (flags `0x34` and `0x24`). The row group layout at the end of a data page is the one of `export.pdb`: groups of 16 row offsets (u16, counting back from the page end), a presence bit mask and an unknown u16, 0x24 bytes per group. The row count is the low 13 bits of the 24 bits at page offset 0x18. Row offsets are relative to the heap, which starts at page offset 0x28.

## The link to `export.pdb`

**The only link is the track id.** A TrackTag row names the id of a track row in `export.pdb` (the u32 at offset 0x48 of a track row). Verified on KIOXIA: the TrackTag rows name tracks 135, 137 and 140, which in `export.pdb` are the Bezier playlist's "Bezier batman wave", "Beztival" and "Bezierd distortion".

Things that were checked and are *not* a link:

- The fixed part of the track rows (the first 0x5e bytes) of the tagged tracks differs from the untagged ones only in the id, sizes and a few per-track values (0x14, and bytes 0x02 and 0x03, which vary across all tracks).
- The string fields of the tagged tracks (comment, mix name, autoload flag and the others) look like the untagged ones.
- The Columns table (type 16) of `export.pdb` lists 27 browse columns (GENRE, ARTIST, ..., FOLDER, MATCHING) with ids 128-170 and no My Tag column.

So a deck finds a track's tags by looking its id up in `exportExt.pdb`, and `export.pdb` does not say whether a track has any. Decks that do not read `exportExt.pdb` simply ignore tags. Consequence for trakcrate: tags only mean something while track ids are stable. An export that renumbers tracks would leave TrackTag rows pointing at the wrong tracks. On KIOXIA the ids survived an update of the drive; whether a full re-export keeps them is not checked.

## Row layouts

All integers little endian.

### Tag or category (table type 3)

| Offset | Size | Field                                                                                  |
|--------|------|----------------------------------------------------------------------------------------|
| 0x00   | 2    | subtype: `0x0680` = 8-bit string offsets (seen); `0x0684` = 16-bit offsets (not seen)  |
| 0x02   | 2    | index shift: 0x20 per row (rekordcrate: "tag_index")                                   |
| 0x04   | 4    | unknown, 0 in every sample                                                             |
| 0x08   | 4    | unknown, 0 in every sample                                                             |
| 0x0c   | 4    | parent id; 0 for a category (no parent)                                                |
| 0x10   | 4    | position: for a tag its place inside its category, for a category its place in the list |
| 0x14   | 4    | id of the tag or category                                                              |
| 0x18   | 4    | non-zero (`0x01000000`) for a category, 0 for a tag                                    |
| 0x1c   | 1    | a pad byte (`03`, `07`, ... varies; not understood)                                    |
| 0x1d   | 1    | offset of the **name** string, relative to the row                                     |
| 0x1e   | 1    | offset of the second string, usually empty (rekordcrate: "unknown purpose, often empty") |
| ...    |      | the two DeviceSQL strings                                                              |

**Caveat:** rekordcrate reads the offset array at 0x1c; on the KIOXIA file the useful offsets are the bytes at 0x1d and 0x1e (`03 1f 2a 17 "Acid House"`: 0x1f points at the name, 0x2a at the empty second string). Reading from 0x1c gives garbage for the name. The 16-bit variant (`0x0684`) is untested and may need the same one byte shift.

DeviceSQL strings as in `export.pdb`: a first byte with the low bit set is a short ASCII string of `(b >> 1) - 1` characters following it (`0x17` = 10 characters); `0x40` is a long ASCII string with a u16 length at +1 and the text at +4; `0x90` is UTF-16LE with the same header.

### Track tag (table type 4)

16 bytes, four u32:

| Offset | Field                                                   |
|--------|---------------------------------------------------------|
| 0x00   | magic, always 0                                         |
| 0x04   | track id (the id in `export.pdb`)                       |
| 0x08   | tag id (a non-category row of the tag table)            |
| 0x0c   | always 3 (meaning not known)                            |

Only tags are attached to tracks, never categories. A track can have several tags and a tag several tracks.

## What the default and the KIOXIA file contain

The default template has four categories and the stock tags (28 rows, ids 1-28, all live):

- **Genre** (1): Acid House, Deep House, Techno, Nu Disco, Electro House, Bass Music, Trap
- **Components** (9): Synth, Vocal, Beat, Sub Bass, Percussion, Piano, Dark, Upper
- **Situation** (18): Main Floor, Second Floor, Lounge, Mid Night, Morning, Build up, Peak Time, Build down
- **Untitled Column** (27): My Comment (28)

The KIOXIA file has 31 live tag rows (4 categories) and 45 rows that are deleted, and no default row is missing. What the user changed:

- Category 27 was renamed **Authorship**; its tag 28 became **Home Made**; a new tag 29 **Slowable** was added.
- New tags **Tech House** (30) and **UK Garage** (31) were added to Genre, at positions 7 and 8.
- The rows are in a different order from the default, and the old versions of the changed rows are still in the page with their presence bit cleared.
- Tag assignments (the TrackTag table has 6 live rows): track 135: Techno (4), Bass Music (7), Tech House (30), Home Made (28); track 137: Home Made; track 140: Home Made.

So rekordbox edits the file **in place**: a changed row is written again at the end of the used area and the old copy stays in the heap with its bit cleared in the group's presence mask. A reader must honour the presence bits or it shows the old names and tags as well (the dump of this page without the filter shows 76 rows).

## A fresh rekordbox export of the same tracks (KIOXIA, second sample)

The stick was exported again by rekordbox itself, with the Bezier playlist (7 tracks):

- `exportExt.pdb` is again 18 pages. The tag table holds all **31 live tags and categories**, the custom ones included (Authorship, Home Made, Slowable, Tech House, UK Garage), now packed in one go with no deleted rows, in a different order again (the two new Genre tags follow Trap, the categories keep their ids).
- The TrackTag table is **empty**: no tag is attached to any track, although the earlier file on this stick had links for three of the tracks.
- The track ids in `export.pdb` were **renumbered**: 1 to 7 now, where the earlier export used 135 to 144. Links from the old file would therefore point at nothing (or at other tracks) in the new database. rekordbox numbers the tracks of every export afresh and writes the TrackTag rows with the new numbers.
- Rekordbox has no option to export My Tags on their own. The tag definitions come along with every export; whether the links are written depends on the tags being assigned to those tracks in the collection and possibly on a rekordbox setting, which was not determined. A drive with the tag tree and no links is a state rekordbox itself produces, so it is a valid starting point for trakcrate to write.

## Show it in the viewer

What a first implementation needs, all read only:

1. Read the page chain of tables 3 and 4 from the table pointers; for each data page read the row count, the groups and presence bits as above.
2. Tag rows: build the tree of categories (rows with the category flag) and their tags ordered by `position`.
3. TrackTag rows: group by track id. The names of the tracks come from the `export.pdb` of the same drive (or the library), looking the id up in the track table; without it show the bare ids.
4. Show deleted rows (presence bit clear) only as a count, never as tags.
5. Tolerate subtype `0x0684` rows (show the raw row) and a file with no tag table.

The viewer's row model (`SettingsRow`: field, value, note, known) suits it: one row per category with its tags in the value, plus a "track tags" row per tagged track; unknown bytes as grey rows.

## Open questions

- What the pad byte at 0x1c, the two unknown u32s and the constant 3 in TrackTag mean.
- The 16-bit offset variant (`0x0684`).
- What table 7 (a single row, unchanged from the default) and the five empty tables are for, and whether decks write anything to them.
- Whether a deck changes `exportExt.pdb` itself (tags made on a player, if the player can do that), or only rekordbox does.
- Which decks read the file at all. Our export copies the template as it is.
- How rekordbox keeps track ids stable between exports, and so how trakcrate should assign them if it ever writes tags (the ids would need to survive a library rebuild, or the tag rows be remapped when `export.pdb` is rebuilt).
- Creating a tag or assigning one on a deck-tested drive: not tried, no sample of a file before and after one edit.

## Sources

- rekordcrate `src/pdb/ext.rs` (`TagOrCategory`, `TrackTag`, `ExtPageType`), and its `pdb/offset_array.rs` for the string offsets.
- Deep Symmetry, *rekordbox export analysis*, "Tag rows" and "Tag track rows": <https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/exports.html>
- `doc/trakcrate/settings-files.md` and `doc/trakcrate/UNKNOWN_BYTES.md` for how trakcrate treats `exportExt.pdb` today.

## In trakcrate today

- `src/trakcrate/ext_pdb.{h,cpp}`: `readExtTags` (the live categories and tags, a category followed by its tags), `extPdbStats` (tags, deleted rows, links) and `withoutTrackLinks` (empties the track to tag table the way rekordbox writes an empty table: the index page points at the blank page, the old data pages are blanked; drops the deleted tag rows and renumbers the slots in the rows that move up).
- The compiled in default `exportExt.pdb` is passed through `withoutTrackLinks` once, so a set made from the built in files starts with clean My Tags.
- The User Settings tree shows the categories and tags below `exportExt.pdb` (read only), and its context menu has **Remove track links**. Settings copied from a drive are not changed.
