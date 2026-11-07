# Bytes trakcrate has to make up

Every byte the writer produces without knowing its meaning is listed here: where it is, what value is used and why. Contributors who can fill in the
real rules (or have rekordbox files that show other values) are very welcome. Values come from the real exports in `tests/fixtures/trakcrate/` unless stated.

Status: **Phase 7** (ANLZ, settings and artwork writers added; see the last section). Before that, Phase 6: (`src/trakcrate/pdb_build.cpp` builds a whole export.pdb from a track list; nothing has been tried on a player yet).

| File / structure | Offset | Name | Seen in samples | What we plan to write | Notes |
|---|---|---|---|---|---|
| export.pdb header | 0x10 | unknown10 | 5 (export.pdb), 6 (exportExt.pdb) | copy from the template file | maybe a format version |
| export.pdb header | 0x14 | sequence | 26 (7 track), 14783 (2195 tracks) | any value, probably increasing per write | modification counter |
| export.pdb header | 0x0c | next unused page | 51 in a 41 page file | pages + pre-allocated empty candidates | the file is shorter than this field says |
| table entry | +4 | empty candidate | 49 (tracks, 7 track file); pages beyond the file | copy the template pattern | page number the table would grow into |
| data page | 0x10 | u10 | 0x15, 0x07, 0x08 ... (counter?) | the template page's value | grows with edits? |
| data page | 0x14 | u14 | 0 | 0 | |
| data page | 0x20 | u20 | 1, 8, 27, 21, 17, 2 | template value | not equal to the row count in tracks, playlist entries |
| data page | 0x22 | numLarge | 0 or 6 | template value | 6 on pages with 7-8 slots written in one go |
| data page | 0x24 | u24 | 0 | 0 | |
| data page | 0x26 | u26 | 0 | 0 | 1 in one index page |
| data page | group flags | row flags word | = present mask, or 0x40 / 0xc0 | present mask | "rows touched in the last transaction"? |
| index page (0x64) | body | whole body | see pdb-write-notes.md | copied from a template page with page numbers patched | undecoded array of u32 |
| index page | 0x24 | u24 | 0x3ec | 0x3ec | |
| index page | 0x20 / 0x22 | u20 / numLarge | 0x1fff / 0x1fff | same | |

## Added in Phase 6 (the builder)

| File / structure | Offset | Name | What the builder writes | Basis |
|---|---|---|---|---|
| file layout | - | page order | per table: index page, data pages, one blank page; `empty_candidate` = that blank page; `next_unused_page` = number of pages | the real files leave the pre-allocated pages out of the file (41 pages, header says 51); a complete file is the cautious choice. **Test on a deck.** |
| data pages | 0x0c | next | each data page points at the next one; the last at the blank page | copied from how the template chains |
| data pages | 0x10 | u10 | a running counter 1, 2, 3 ... over the data pages (static tables keep the template's value) | looks like a write counter; header `sequence` = last counter + 1 |
| data pages | 0x20 / 0x22 | u20 / numLarge | 1 / (slots - 1) | the value on every freshly written page of the 2195 track export |
| data pages | row flags | per group | the present mask | equal to it on fresh pages |
| index pages | 0x28.. | body | the template's index page of that type, with own number at +0x28 and first data page at +0x2c (0x03ffffff when empty) | the 2195 track export's tracks index page has an undecoded per-row directory after these words; **not written** |
| track row | 0 | subtype | 0x24 | all 2391 track rows seen |
| track row | 2 | index shift | slot x 0x20 (also artists 0x60 and albums 0x80) | verified on the 7 and 2195 track files |
| track row | 4 | unknown | copied from the template row (0x0700) | |
| track row | 12, 24, 36 | unknown | copied (0, 0x1e43f478, 0) | constant in all 7 rows |
| track row | 20 | unknown | 2406 + track id | counts up with the id in the template; meaning unknown |
| track row | 86, 90..93 | unknown | copied (`29 00`, `01 00 03 00`) | |
| track row | 32 | key id | 0 (no key) | the template has an empty keys table; musical keys are not written |
| track row | strings 0-9, 13, 18 | unknown strings | copied from the template's first track (`""`, `"6"`, `"\x01"`, `"ON"` ...) | meaning unknown |
| track row | string 15 | | the date added again | equals slot 10 in the template |
| track row | end | padding | 40 zero bytes after the last string, then to a multiple of 4 | real rows end 38-70 bytes after their last string |
| analyze path | - | Pxxx/yyyyyyyy | partition + slot from rekordbox's path hash (`h*0x34F5501D + c*0x93B6`, mod 200003), `ANLZ<n>` counter on collision | solved, see usbanalz-hashing.md; awaiting a test on real decks |
| artists, albums | - | far form | not used: names always start at offset 10 (artist) / 22 (album), any string kind | `pdb.c` reads a u8 offset |
| static tables | - | colours (6), 16, 17, 18, 19 | the template's pages, row by row | fixed lists in every export seen |
| keys (5), 9, 10, 11, 12, 14, 15 | - | | empty (index page + blank page) | empty in the 7 track export |

## Added in Phase 7 (ANLZ files, settings files, artwork)

`anlz_writer.cpp` reproduces the 7 real file pairs of `tests/fixtures/dbserver/export7` tag by tag (same tags, order, header words and bodies), except for these:

| File | Tag / offset | Name | What the writer does | Basis |
|---|---|---|---|---|
| ANLZ0000.DAT | PVBR body (1604 bytes) | VBR seek index | **VBR mp3 only** (Xing header): 401 words, word i = byte offset from the first mpeg frame of frame `floor((i+1) x frames / 400) - 8` (i = 0..399), the last word = frames x samples per frame. Constant bitrate files stay all zero | decoded from a real rekordbox export of a 10564 frame file, all 401 words reproduced (`src/trakcrate/mp3_index.cpp`) |
| cue entries, VBR mp3 | PCPT / PCP2 | seek position | PCPT tail words = time in 1/75 s, 0, byte offset of frame `floor(ms / frame length) - 8` (0 before that), 0; status 0, then 0x00010000; cues listed from the highest hot cue down; PCP2 entries are 88 bytes (colour word 0x00010000, loop end ffffffff when none, word 0000ff00 at 44, then zeros) | same real export (cues at 53, 1943, 3832 ms), `vbrCuesMatchARealRekordboxExport` |
| ANLZ0000.DAT | PQTZ header word 2 | unknown2 | 0x00080000 | 12 of 14 real files; two have 0x0008ff1a and 0x0008ff5a |
| ANLZ0000.EXT | PQT2 0x2c | per file value | 0 | differs in every real file (36285013, 69320672 ...); not derivable from count, tempo or length |
| ANLZ0000.EXT | PWV4 body | colour preview, 1200 x 6 bytes | **approximated** from the detail waveform: byte0 = height x 127/31, byte1 = 255 - byte0, bytes 2,3 = byte0, bytes 4,5 = green and blue weight x 127/7 | `rekordbox-anlz-format.text` regression notes; the colour meaning is a guess. Can be left out |
| both | PMAI header words | | 0, 0x00010000, 0x00010000, 0 | real files |
| both | PWAV / PWV2 header 2nd word | | 0x00010000 | real files (the older doc said 0x00100000) |
| .DAT PCOB / .EXT PCO2 | cue entries | patterns | loops, plain cues and the "first cue, marker 150" pattern are written the way the real entries look; the words at 0x24-0x34 follow those three patterns | 7 real files; memory points and cues with comments or colours have no sample |
| both | file contents | | the .DAT lists the first three hot cues, the .EXT all of them in PCO2 and empty PCOB lists | real files |
| beat grid | times | | beat i = first + i x 60/bpm, whole ms plus a microsecond remainder | real grids deviate by at most 1 ms on about 11% of beats (their period is a hair below 60/rounded bpm) |

Settings files (`settings_files.cpp`, `settings_defaults.cpp`): `DEVSETTING.DAT` and `MYSETTING.DAT` are the real files of the 7 track drive, embedded verbatim. The CRC rule (CRC-16/XMODEM over the bytes from the magic at 0x68 up to the CRC) is checked on both. `MYSETTING.DAT` carries rekordbox preferences, decoded for display in `settings_view.cpp`. Still unknown: in `DEVSETTING.DAT` 0x70, 0x73 and 0x76-0x87; in `MYSETTING.DAT` 0x75, 0x79-0x7B, 0x83-0x84, 0x86-0x87 and 0x8E-0x8F. The shipped `MYSETTING.DAT` has `00` at 0x85 (needle lock) where rekordbox 6 writes a code. `djprofile.nxs` (a user name inside) and `RBFLTR.DAT` (device specific) are **not** written.

`djprofile.nxs` (`settings_files.cpp`): bytes 0x00-0x03 (`00 42 d7 c3`) and 0x1c-0x1f (`a2 3f 2b 74`) are not understood, and the 0x04 timestamp is a guess at the profile's creation date. A changed user name is written without touching them and has **not** been tried on a deck. **Help wanted:** a `djprofile.nxs` from a rekordbox drive made under a different user name (and ideally two from the same user at different times) would show whether 0x1c is a checksum, an id or per-user constant. Details in `settings-files.md`.

Artwork (`artwork_paths.cpp`, `src/ui/trakcrate_artwork.cpp`):

| What | Value | Basis |
|---|---|---|
| directory of artwork id | `id / 20 + 1`, five digits (ids 0-19 in 00001, 20-39 in 00002) | **verified on all 2087 artwork rows** of the 2195 track export (the first guess `(id-1)/20+1` was wrong for ids 20, 40, ...) |
| file name | `a<id>.jpg`; the small copy `a<id>_m.jpg` | the `_m` copy and the pixel sizes are **unverified** (public documentation; our samples have no image files) |
| sizes | 240 x 240 and 80 x 80 | a guess |
| encoding | baseline JPEG, RGB, quality 90, no metadata, centre crop to a square, alpha over white | conservative choice for hardware decoders |
