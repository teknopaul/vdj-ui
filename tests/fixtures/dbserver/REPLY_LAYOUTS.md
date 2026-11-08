# Request/reply layouts seen from a real deck

Source: `/var/tmp/prolink-w-tcp.pcap`. Server = deck `169.254.231.210` holding the 7-track LMMS pendrive (same data as
`references/libcdj/rekordbox`, rebuilt here as `export7/`), client = deck `169.254.177.253` (player 4).
`client_stream.bin` / `server_stream.bin` are the two byte streams; `python3 dump_stream.py` prints the pairs.
Numbers below are hex unless noted. DMST = requester:menu:slot:type, e.g. `04 02 03 01`.

## Item (4101) layout, 12 args

`1 parent/aux`, `2 id`, `3 byte length of label1 incl. NUL`, `4 label1`, `5 byte length of label2`, `6 label2`, `7 item type`,
`8 flags`, `9 artwork id`, `10 playlist position`, `11 unknown`, `12 unknown`.
An "empty" label is the 1-char string `00 00` (length 2). With menu byte M=03 (the deck only wants ids) every label is the 1-char string
**`U+0009`** (length 2, term char 0x0009), also label2.
Track title items (type 4): arg8 = `01000000`, arg11 = `100`, others 0 (arg1 = 0 in track lists, = artist id in the 2002 title item).

## Requests

| Req | Args | Reply |
|-----|------|-------|
| `0000` | requester | `4000 (0, serverDeviceNumber)`, tx `fffffffe` |
| `3e03` **undocumented** | DMST (M=01) | `4b02 (3e03, 0, 2, "")` (arg 4 is a 1-char string, arg 3 its byte length) |
| `1000` root | DMST, sort, `00ffffff` | `4000 (1000, 8)`; render returns 7 items: (2 ARTIST type 81), (3 ALBUM 82), (4 TRACK 83), (c KEY 8b), (5 PLAYLIST 84), (16 HISTORY 95), (12 SEARCH 91). Labels are `U+FFFA name U+FFFB`; arg1 0. **count 8 but only 7 items are rendered** |
| `1002` artists | DMST, sort | `4000 (1002, n)`; items (0, artistId, name, "", 7, 0...) |
| `1003` albums | DMST, sort | `4000 (1003, n)`; items (0, albumId, name, "", 2, 0...) |
| `1004` tracks | DMST, sort | `4000 (1004, n)`; title items (0, trackId, title, "", 4, 01000000, 0, 0, 100, 0). The deck's own order for sort 0 is **not** title order (2,5,6,7,1,4,3 here). Sort 0 is the sort menu entry DEFAULT (pdb table 18 lists 25 DEFAULT, 26 ALPHABET, 2 ARTIST, 3 ALBUM, 5 BPM, 6 RATING, ... in sort-code order; table 16 holds the column names, table 17 the root menu), i.e. a separate sort from alphabetical. No single numeric field of the track row (every offset, widths 1/2/4, asc/desc, id tie-break) and no string field reproduces the order; physical row order is 1..7. Only BPM-desc then play-count-asc fits, probably by chance with 7 rows. Needs a pendrive with more tracks to pin down |
| `1014` keys | DMST, sort | `4000 (1014, 18)` (= all 24 rows of the key table, not only used keys); items (0, keyId, name, "", f, 0...) |
| `1105` playlists | DMST, sort, id, folder? | root (id 0, folder 1): `4000 (1105, 1)`, item (0, playlistId, name, "", 8, 0...). Playlist (id 1, folder 0): `4000 (1105, 7)`, title items like 1004 with arg10 = position 1..n |
| `3d03` **undocumented** | DMST (M=01), 4 | `4000 (3d03, 0)` |
| `3100` **undocumented** | DMST (M=03), trackId, 0, 0 | `4000 (3100, n)`, n = 2 and 1 for tracks 3 and 2; meaning unknown |
| `3000` render | DMST, offset, limit, 0, total, 0 | `4001 (1, offset)`, items offset..offset+limit-1, `4201 ()`. (arg6 was `c` once.) |
| `2002` metadata | DMST (M=01 or 05), trackId | `4000 (2002, 0b)`; render returns 11 items, below |
| `2102` **undocumented, carries the AUDIO FILE PATH** | DMST (M=08), trackId | `4000 (2102, 6)`; render returns 6 items, below |
| `2004` waveform preview | DMST M=08, 3, trackId, 0, (blob omitted) | `4402 (2004, 0, 384, blob900)` |
| `2204` beat grid | DMST M=08, trackId | `4602 (2204, 0, len, blob, shift)` (note the extra 5th arg: the grid shift, see below) |
| `2504` **undocumented** | DMST M=08, trackId | `4502 (2504, 0, 644, blob1604)` mostly zeros |
| `2904` waveform detail | DMST M=01, trackId, 0 | `4a02 (2904, 0, len, blob)` |
| `2b04` extended cues | DMST M=08, trackId, 0 | `4e02 (2b04, 0, len, blob, count)` |

## 2002 metadata items (track 3), in order

1. title `(artistId 1, trackId 3, "Whistling Diesel", "", 4, 01000000, art 0, 0, 100, 0)`
2. artist `(1, artistId, name, "", 7)`  (arg1 = 1)
3. album `(1, albumId, name, "", 2)`  (arg1 = 1)
4. duration `(0, seconds, "", "", b)`
5. tempo `(0, bpm*100, "", "", d)`
6. comment `(0, trackId, comment, "", 23)`
7. key `(1, keyId, name, "", f)`  (keyId 0 -> empty name)
8. rating `(0, rating, "", "", a)`
9. color `(0, colorId, "", "", 13)`  (arg2 0 = none)
10. genre `(0, genreId, name, "", 6)`
11. date added `(0, trackId, "yyyy-mm-dd", "", 2e)`

## 2102 items (track 3), in order

1. `(0, 1, "", "", 4)`  constant 1 in all samples
2. duration `(0, seconds, "", "", b)`
3. tempo `(0, bpm*100, "", "", d)`
4. comment `(0, trackId, comment, "", 23)`
5. **file `(fileSize, trackId, "/Contents/Intapol/UK Dembow/uk-dembo-whistling-diesel-track-06.mp3", "", 0)`** (arg1 = the pdb file size, e.g. 7682216 = 0x7538a8; label = pdb file_path)
6. `(0, 1, "", "", 2f)`  constant 1 in all samples (maybe the file type)

This is how a player learns where the audio file is: `2102` gives the path and size, and it then reads the file (the deck-to-deck
captures show the NFS side separately).

## Binary blobs (decoded by comparing with the ANLZ files of the same tracks; all reproduced byte for byte by `src/dbserver/db_blobs.cpp` except the noted bytes)

- Beat grid (`2204` -> `4602 (2204, 0, len, blob, shift)`): the 5th arg is the **grid shift** of the track's .DAT (PQTZ header 0x12, signed ms, sent sign
  extended): `ffffff5a` = -166 is the track P064/00013152 whose PQTZ header says `0xff5a`, 0 for the others. Found 2026-11-07 when libcdj learned the shift
  (this was called junk before). The blob holds the .DAT entries unshifted; the client adds the shift.
  Blob: head `00 00 08 00`, u32 LE beat count, u32 LE count*16, u32 LE `1`, u32 LE **heap pointer** (`0ceea950`, `0ceeeab4`; meaningless, we send 0),
  then per beat 16 bytes: u16 LE beat in bar, u16 LE bpm*100, u32 LE time ms (= the .DAT PQTZ entry), 8 x `ff`.
- Waveform preview (`2004` -> `4402`): 900 bytes: for each of the 400 PWAV bytes `b`: `b & 0x1f` (height) then `b >> 5` (whiteness); then the 100 PWV2 bytes.
- Waveform detail (`2904` -> `4a02`): head = u32 LE count, u32 LE `1` (entry size), u32 LE count, u32 LE `0x96` (150/s), u32 LE heap pointer (`0cf233a0`), then the PWV3 bytes unchanged.
- Extended cues (`2b04` -> `4e02 (2b04, 0, len, blob, count)`): 76 byte entries in PCO2 order (memory points first, then hot cues A..H):
  `0` u32 LE 76, `4` hot cue (0 memory, 1 = A), `6` type (1 cue, 2 loop), `10` u16 LE marker (150 / 1000), `12` u32 LE time ms, `16` u32 LE loop end ms,
  `24` u32 LE 1, `32..51` unknown fill (all `ff` for a loop; `ff*8, 0*4, ff*4, 0*4` for a plain cue; hot cue A showed `ff*4, 02 00 00 00, 0...`), `52` u32 LE `0x12`,
  `66` u16 LE loop length numerator, `68` u16 LE denominator (16/1 = 16 beats).
- `2504` -> `4502`: 1604 bytes, nearly all zero (undocumented; we send zeros).
