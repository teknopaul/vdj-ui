# The small tables of export.pdb (11, 12, 16, 17, 19)

Until 2026-11-07 trakcrate copied these tables from the template page by page without reading them. They are decoded now (`src/trakcrate/pdb_tables.{h,cpp}`,
test `tests/test_trakcrate_pdb_tables.cpp`). Names and the menu / history semantics come from rekordcrate (`src/pdb/mod.rs`); every layout below was
checked against rows in `tests/fixtures/trakcrate/pdb/export7.pdb`, rekordcrate's `complete_export` and the 2195 track export of libcdj.

| Table             | Type | Row layout (little endian)                                                                                                   | Notes                                                                                                                                                                   |
|-------------------|------|------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Columns           | 16   | `u16 id`, `u16 unknown` (0x80 + position), then a DeviceSQL string                                                           | the string is `0x90` UTF-16 between U+FFFA and U+FFFB ("interlinear annotation" marks), e.g. `GENRE`. 27 rows in every file seen. These are the browse category labels. |
| Menu              | 17   | `u16 category_id`, `u16 content_pointer`, `u8 unknown`, `u8 hidden`, `u16 sort_order` (8 bytes)                              | `category_id` is a column id. 21 rows in our LMMS exports, 22 in rekordbox 6 demo tracks (the set differs between versions).                                            |
| History           | 19   | `u16 0x0280`, `u16 index_shift`, `u32 num_tracks`, `u32 0`, date string, `u16 0x1E19`, version string ("1000"), label string | one row per change of the stick, see below                                                                                                                              |
| History playlists | 11   | `u32 id`, name string ("HISTORY 001")                                                                                        | sessions the player recorded; empty in a stick that was never played                                                                                                    |
| History entries   | 12   | `u32 track_id`, `u32 playlist_id`, `u32 entry_index` (12 bytes)                                                              | tracks of those sessions; the 2195 track export has 9 sessions with 280 entries                                                                                         |

## Menu (17): what the player shows

`hidden` 1 hides a menu, `sort_order` places it. rekordcrate tried this on a CDJ-350: a hidden menu (GENRE) appeared when `hidden` was set to 0, and
`sort_order` reordered the list; some menus never show on that model even when visible. **We have not tried it on any deck.**

`ExportPlan::menu` (a list of `MenuSetting { categoryId, visible, sortOrder }`) does the same in a new export: the template's menu rows are copied and
only `hidden` and `sort_order` of the named categories change (`applyMenuSettings`; same row size, so the page layout does not change). Unnamed menus
and an empty list keep the template. In the LMMS template GENRE (category 1) is hidden, ARTIST 2, ALBUM 3 and TRACK 4 are visible at sort order 1, 2, 3.
Nothing in the UI uses it yet: it is a hook for the exporter once a deck confirms it.

## History (19): what happened to a stick

rekordbox writes a new History row after every change of the library (tracks added or removed) and **marks the previous row deleted without
overwriting it**. The deleted rows are therefore a log of `num_tracks` and the date of each change:

- the 7 track LMMS stick: 8 rows, track counts 0, 1, 2 ... 7 (the tracks were added one by one), the last is live;
- the rekordbox 6 demo export: 13 rows, counts 0 2 3 4 5 4 3 2 1 0 1 2 (tracks added and removed while the demo library was made), the last is live;
- the 2195 track export: 96 rows, the live one says 2195.

`readHistory()` returns the deleted rows too (flag `deleted`), which makes "what did rekordbox or a deck do to my stick" answerable. Our own exports
write the template's single row (a copy of the LMMS stick's last row: the date is not updated). A deck mounting a stick only bumps the header
sequence number, so decks leave no trace here (see `deck-edits.md`).

## Index pages (resolved question)

rekordcrate describes the entries of an index page as a reference to every data page of the table; our notes found entries only for pages with
deleted rows. In the 2195 track export the tracks table has 267 data pages and its index page counts 82 entries; **every listed page has page flags
0x34** (a deleted row on the page), 82 pages have that flag, and the 185 pages with flag 0x24 are absent. (In the smaller tables one of the counted
entries reads as an empty `0x1ffffff8` slot, which this check did not explain.) rekordcrate's `index_page.bin` unit test (272 entries) comes from
a library with many deleted rows. So both observations describe the same rule: the index page lists the pages that contain deleted rows. The rule was
checked on the files above, not on a deck-edited stick.
