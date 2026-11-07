# export.pdb: what the writer knows (notes from Phase 5)

Code: `src/trakcrate/pdb_writer.{h,cpp}`. Tests: `tests/test_trakcrate_pdb_roundtrip.cpp`. Data: `tests/fixtures/trakcrate/pdb/export7.pdb` (the 7 track
LMMS drive, 41 pages) and `exportExt7.pdb` (its `exportExt.pdb`, 18 pages); optionally libcdj's 2195 track `export.pdb`.

Everything here was checked against those files; "guess" marks what was not.

## Result of the phase

- `parsePdb` then `serialisePdb` reproduces **byte for byte**: `export7.pdb`, `exportExt7.pdb` and the 2195 track export (1.6 MB, 360 data pages).
  A fuzz test flips 300 random bytes and the round trip stays exact, because any page the model does not understand is kept verbatim ("raw").
- All data pages (flags `0x24` and `0x34`) of all three files are decoded into rows, and the **semantic re-emit** rebuilds them from rows alone: free
  space, heap size, row counters, offsets and present bits are computed, never copied. They equal what rekordbox wrote. What is still copied
  (unknown meaning): the page header words `u10 u14 u20 numLarge u24 u26` and the per-group "row flags" words.

## File

| Offset | Size | Field                                                                                    |
|--------|------|------------------------------------------------------------------------------------------|
| 0x00   | 4    | 0                                                                                        |
| 0x04   | 4    | page size, 4096 (only this size is supported)                                            |
| 0x08   | 4    | number of tables (20 in all samples)                                                     |
| 0x0c   | 4    | next unused page. **Can be larger than the file**: the 7 track file has 41 pages, the field says 51 |
| 0x10   | 4    | unknown, 5 in the small samples (ExtPdb: 6)                                              |
| 0x14   | 4    | sequence, a modification counter (26 in the 7 track file)                                |
| 0x18   | 4    | 0                                                                                        |
| 0x1c   | 16 x n | table entries: type, **empty candidate**, **first page**, **last page** (this order)   |
| ...    |      | zeros to the end of page 0                                                               |

Table entry in the 7 track file, tracks: `type 0, empty_candidate 49, first 1, last 2`. So `first` is the table's *index page*, `last` its last page
that holds rows, and `empty_candidate` the page number the next page would take (pages 41-50 are not in the file at all: rekordbox does not write
trailing pages that are still empty). The earlier note in `rekordbox-db-format.text` has the last two swapped.

## Pages

Common 40 byte header (all little endian): `0x00` zero, `0x04` the page's own number, `0x08` table type, `0x0c` next page, `0x10` u10 (counter? 1 in index
pages), `0x14` u14 (0), `0x18..0x1a` packed counters, `0x1b` flags, `0x1c` free bytes, `0x1e` used bytes, `0x20` u20, `0x22` numLarge, `0x24` u24, `0x26` u26.

Kinds of page seen:

| flags | what                                                                                                   |
|-------|--------------------------------------------------------------------------------------------------------|
| 0x64  | **index page**, the first page of every table. No rows (`used 0`). Body is an array of u32 (see below). Kept raw. |
| 0x24  | data page                                                                                              |
| 0x34  | data page that contains deleted rows (present bits not all set), or was changed after creation (guess) |
| 0x00  | blank page, all 4096 bytes zero (`index 0`). The tables' `first+1` slot when the table is empty        |

Index pages in the 7 track file: table type, then body `own-number, first-data-page, 0x03ffffff, 0, 0x1fff0002(?), ...` followed by `0x1ffffff8`
fillers up to byte 0xfec; header `u20 = 0x1fff`, `numLarge = 0x1fff`, `u24 = 0x3ec`. In the 2195 track file the tracks index page holds many more
entries (pairs like `0x110052`, offsets such as `0xa98`) that look like a per-row directory; **decoded later**: a count and `(page << 3) | 3` entries, one per page with deleted rows (`pdb-append-notes.md`). For a small export (one data page per
table) a writer can copy the small pattern and patch the page numbers (Phase 6 does this from a template).

### Data page (verified)

```
0x00-0x27  header
0x28       row heap: rows back to back in slot order, `used` bytes (the first row starts at offset 0 of the heap)
...        zeros (free space)
end        row index, growing downwards in groups of 16 slots
```

- **Counters** (bytes `0x18..0x1a` as a 24 bit little endian number): low 13 bits = number of row *slots* (including deleted rows), high 11 bits = number
  of *present* rows. Example: tracks page, 7 slots, 7 present = `7 | 7<<13`; the history-like page 40, 8 slots, 1 present = `8 | 1<<13`.
- **free** = `4096 - 40 - used - (2 x slots + 4 x groups)`. **used** = sum of the row lengths. Both verified on every data page of the three files.
- **Row index group g** (16 slots each) lives at `base = 4096 - 36 g`: the row flags word at `base-2`, the present bit mask at `base-4` (bit i = slot i is
  present), the offsets at `base-6-2i` (u16, relative to the start of the heap). Full groups take 36 bytes; the last group only its used slots plus 4 bytes
  (the unused offset slots are then part of the free gap, or already under the heap: **do not read them**; the first version of the decoder did, and 43 full
  pages of the large file failed).
- **Row flags word** (unknown): equal to the present mask on freshly written pages; in the tracks page of the 7 track file `present = 0x7f` but `flags = 0x40`
  (only the last row marked), in page 40 `present 0x80, flags 0xc0`. Probably "rows touched in the last transaction". Copied, not generated.
- Row lengths are not stored: a row ends where the next slot's offset begins (the last at `used`). Rows are always in ascending offset order in the files seen;
  a page that breaks this is kept raw.
- Deleted rows (`present` bit clear) keep their bytes in the heap (page 40 of the 7 track file has 7 of them).
- `u10` looks like a change counter (page 2 `0x15`, page 6 `0x7`), `u20` and `numLarge` are small counts whose meaning is unknown (see UNKNOWN_BYTES.md).

## Row lengths seen (7 track file)

tracks 380-440 (varies with strings), artists 24, albums 40, colors 12-16, playlist tree 40, playlist entries 12, columns/history style tables (16-19)
8-40. Row *contents* are decoded by libcdj's `pdb.c` and written in Phase 6.

## Not done / next

- Phase 6 builds rows and new pages and must choose the unknown header words (UNKNOWN_BYTES.md) and write index pages.
- Table `empty_candidate`, `next_unused_page` and `sequence` rules for a freshly built file (`next_unused` = pages + the pre-allocated ones?) are guesses until a
  deck accepts a built file.
