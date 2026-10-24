# libcdj ANLZ parser (rekordbox analysis files)

Parse rekordbox track-analysis files (`ANLZ0000.DAT`, `.EXT`, `.2EX`) in **libcdj** (C) so that
vdj-ui and other applications can render beat grids, cue points, loops, waveforms and phrase
structure for tracks in a local, USB or NFS-served rekordbox database.

Spec the community has so far: <https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/anlz.html>
(summarised in "Format reference" below, with the places where real files disagree with it).

## Ground rules (read before every phase)

- Work in `/home/teknopaul/bzr_workspace/libcdj` (symlinked in vdj-ui as `references/libcdj`).
  `libcdj/CLAUDE.md` says **DO NOT USE git COMMANDS**. Strictly local edits.
- `make install` writes to `/usr/lib` and `/usr/include/cdj` and needs sudo. Claude cannot run it:
  stop and ask the human to run `sudo make install` whenever vdj-ui needs the new library.
- Code is C (gcc, `-Wall -Werror -fPIC`). Follow `src/nfs/pdb.c` / `pdb.h` as the style reference:
  small static helpers, no external deps, callbacks or plain structs, `//SNIP_xxx` markers around
  code that unit tests pull in.
- Tests are `.c.snip` files run by `sniprun` from `make test` (see `src/test/pdb_parse_test.c.snip`
  for the exact pattern: `//SNIP_FILE` lines include source, `snip_equals` / `snip_assert`).
  Use the `/snip` skill if the framework is unclear.
- Keep `libcdj/doc-internal/rekordbox-db-format.text` style docs: the new format doc goes next to it.
- Do not read the whole corpus into context. Use the survey tool and the numbers recorded below.
- **No external dependencies in `make test`.** `/var/tmp/rekordbox-example` is a temporary directory. Every test that
  `make test` runs must use only files committed in the libcdj tree: the real ANLZ fixtures in
  `src/test/fixtures/anlz/` (already copied, see Phase 1) and synthetic files from `anlz_fake`. The full export is only an
  *optional* extra for the developer (`make survey`, `make test-corpus`) and must never be needed for a green build.
- New code goes into `libcdj.so` (add objects to `OBJS` in the Makefile, ship the header via
  `make install` and `deploy/build-deb.sh` exactly as was done for `pdb.h`).

## What we know from the real data

Surveyed in `/var/tmp/rekordbox-example/PIONEER/USBANLZ` (4369 files; this directory is temporary, hence the fixtures). This is the first
real-world check of the community spec; Phase 1 re-verifies it and stores it in the format doc.

| Fact                          | Value                                                                                                                                                                                                   |
|-------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Layout                        | `USBANLZ/P0xx/0001C3F7/ANLZ0000.{DAT,EXT}`, `P0xx` = 3 hex chars, then 8 hex chars                                                                                                                      |
| Files                         | 2184 `.DAT`, 2184 `.EXT`, 0 `.2EX` (no CDJ-3000 data in this export), plus `USBANLZ/USBMNG.DAT`                                                                                                         |
| Link from the pdb             | `pdb_track_t.analyze_path`, e.g. `/PIONEER/USBANLZ/P02E/0002F1FC/ANLZ0000.DAT` (always the `.DAT`; `.EXT`/`.2EX` are siblings with another extension)                                                   |
| `USBMNG.DAT`                  | **Not an ANLZ file**: magic `PMNG`, header 0x20, one `PTBL` tag (400 KB). Reject by magic, never parse                                                                                                  |
| Endianness                    | big-endian everywhere (opposite of `export.pdb`)                                                                                                                                                        |
| File header                   | `PMAI`, `len_header` = 0x1c in all 4368 files, `len_file` equals the real file size in all of them. EXT sample: u32 @0x0c = 1, @0x10 = 0x00010000, @0x14 = 0x00010000, @0x18 = 0                        |
| Tags tile the file exactly    | walking `len_tag` from `len_header` lands exactly on EOF in every file                                                                                                                                  |
| `.DAT` tags                   | `PPTH`(hdr 0x10) `PVBR`(0x10) `PQTZ`(0x18) `PWAV`(0x14) `PWV2`(0x14) `PCOB`x2 (0x18)                                                                                                                    |
| `.EXT` tags                   | `PPTH` `PWV3`(0x18) `PCOB`x2 `PCO2`x2 (0x14) `PQT2`(0x38) `PWV5`(0x18) `PWV4`(0x18); **`PQT2` is missing from 5 files**; `PVB2` (0x20) appears in 2 files                                               |
| Tags the community page lacks | `PQT2` (compact beat grid, EXT) and `PVB2` (EXT) are present in real files and **not in the spec text**. `PSSI`, `PWV6`, `PWV7` are in the spec but absent from this corpus (no phrase data, no `.2EX`) |
| Sizes                         | DAT 2.4 KB - 18 KB, EXT 12 KB - 426 KB (waveforms dominate)                                                                                                                                             |

### Real bytes worth keeping as test anchors (track at `P042/00015D16`)

```
PQTZ (DAT)  50515455 | hdr 00000018 | len 00001678 | unk1 00000000 | unk2 00080000 | len_beats 000002cc
            first entry: 0001 300c 00000033   = beat 1, 123.00 BPM, 51 ms         (716 beats)
PCOB (EXT)  5043 4f42 00000018 00000018 | type 00000001 | 00000000 | ffffffff     (empty hot-cue list, len_tag==len_header)
PCO2 (EXT)  hdr 00000014 len 00000040 | type 00000001 | lencues 0001 | 0000
            PCP2: hdr 00000010 len_entry 0000002c | hot_cue 1 | type 02 (loop) | 0003e8
                  time 00007a27 (31271 ms) | loop_time 000098a4 (39076 ms)       (entry ends at 0x2c: no comment, no colour)
PWV5 (EXT)  hdr 18 | len_tag 00019972 | entry_bytes 00000002 | entries 0000ccad (52397) | unk 00960305
            52397 / 150 = 349.3 s, consistent with the last beat at 0x552a0 = 349 856 ms
PWV4 (EXT)  hdr 18 | entry_bytes 00000006 | entries 000004b0 (1200) | unk 00000000
PQT2 (EXT)  hdr 00000038 | len 000005d0 (1488) | 00000000 | 01000002 | 00000000 | 0001300c 00000033 | 0004300c 000552a0 | 000002cc | ...
            hypothesis: two anchor beats (first, last) then beat count 0x2cc = 716 = PQTZ len_beats, then packed per-beat data
```

The `PQT2` reading above is a **hypothesis to be proved in Phase 3**, using the `.DAT` `PQTZ` of the same
track as ground truth (a free oracle: 2179 tracks have both).

## Format reference (from the Deep Symmetry page; verify each item against real files)

All integers big-endian. Offsets are from the start of the tag unless stated.

**File:** `PMAI` | `len_header` (0x1c) | `len_file` | 16 unknown bytes. Tags start at `len_header`.

**Tag envelope:** `fourcc` | `len_header` | `len_tag` (whole tag incl. header). Next tag = start + `len_tag`.
The page says `len_header` is unreliable: model the body from 0x0c and use `len_tag` only for skipping.
(Real files: `len_header` is consistent per tag type, e.g. PPTH 0x10, PQTZ 0x18, PWV5 0x18, PQT2 0x38.)

| Tag    | File                     | Hdr  | Body                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
|--------|--------------------------|------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `PPTH` | DAT, EXT                 | 0x10 | @0x0c `len_path`; @0x10 UTF-16BE path with trailing NUL (`/Contents/...`)                                                                                                                                                                                                                                                                                                                                                                                                                |
| `PVBR` | DAT                      | 0x10 | VBR seek index; body unknown (1604 zero-ish bytes in the sample). Keep raw                                                                                                                                                                                                                                                                                                                                                                                                               |
| `PVB2` | EXT                      | 0x20 | Not in spec. Keep raw (2 occurrences)                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| `PQTZ` | DAT                      | 0x18 | @0x0c unk1, @0x10 unk2 (0x00080000 seen; spec says 0x00800000), @0x14 `len_beats`; entries of 8 bytes: u16 `beat_number` (1-4), u16 `tempo` (BPM x100), u32 `time_ms`                                                                                                                                                                                                                                                                                                                    |
| `PQT2` | EXT                      | 0x38 | Not in spec. Compact beat grid, see Phase 3                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| `PCOB` | DAT, EXT                 | 0x18 | @0x0c type (0 memory points, 1 hot cues); @0x12 u16 `len_cues`; @0x14 u32 memory_count / `ffffffff`; then `PCPT` entries (hdr 0x1c, entry 0x38): @0x0c hot_cue (0 = memory, 1 = A ...), @0x10 status (0 normal, 4 active loop), @0x18 u16 order_first, @0x1a u16 order_last, @0x1c u8 type (1 point, 2 loop), @0x20 u32 time_ms, @0x24 u32 loop_time_ms. Two PCOB per file (memory + hot). In `.EXT` both are empty (len_tag == len_header); in `.DAT` the hot-cue `PCOB` holds the same cues as `PCO2` (non-empty for the 227 tracks that have cues); the memory list is empty everywhere in this corpus                                                                      |
| `PCO2` | EXT                      | 0x14 | @0x0c type (0 memory, 1 hot), @0x10 u16 `len_cues`, @0x12 u16 unknown; then `PCP2` entries (hdr 0x10, **variable** `len_entry` @0x08): @0x0c hot_cue, @0x10 u8 type (1 point, 2 loop), @0x14 u32 time_ms, @0x18 u32 loop_time_ms, @0x1c u8 color_id, @0x24 u16 loop_numerator, @0x26 u16 loop_denominator, @0x28 u32 `len_comment` (bytes), @0x2c UTF-16BE comment + NUL, then after the comment `color_code` u8 + R, G, B u8. Entries may end early (sample entry is exactly 0x2c long) |
| `PWAV` | DAT                      | 0x14 | @0x0c `len_preview` (400), @0x10 unk; 400 bytes: low 5 bits height 0-31, high 3 bits whiteness                                                                                                                                                                                                                                                                                                                                                                                           |
| `PWV2` | DAT                      | 0x14 | as PWAV, 100 bytes, low 4 bits height 0-15                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| `PWV3` | EXT                      | 0x18 | @0x0c `len_entry_bytes` (1), @0x10 `len_entries`, @0x14 unk; entries encoded as PWAV; 150 entries/s                                                                                                                                                                                                                                                                                                                                                                                      |
| `PWV4` | EXT                      | 0x18 | @0x0c entry bytes (6), @0x10 entries (1200), 6 bytes per column. Layout not on the page; see Phase 5                                                                                                                                                                                                                                                                                                                                                                                     |
| `PWV5` | EXT                      | 0x18 | entry bytes 2, `len_entries`, big-endian u16: bits 15-13 red, 12-10 green, 9-7 blue, 6-2 height, 1-0 unused (derived from the page's field widths; verify). 150 entries/s                                                                                                                                                                                                                                                                                                                |
| `PWV6` | 2EX                      | 0x14 | 3 bytes per column x 1200, order mid, high, low                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| `PWV7` | 2EX (page also says EXT) | 0x18 | 3 bytes per entry (mid, high, low), 150/s, non-linear scaling                                                                                                                                                                                                                                                                                                                                                                                                                            |
| `PSSI` | EXT                      | 0x20 | Song structure. Body after `len_entry_bytes`(0x18) and `len_entries` (u16 @0x10) is **XOR-masked**: mask byte i = base[i mod 19] + len_entries (mod 256), base = `CB E1 EE FA E5 EE AD EE E9 D2 E9 EB E1 E9 F3 E8 E9 F4 E1`. Then mood @0x12 (1 high, 2 mid, 3 low), end beat @0x1a, bank @0x1e (0-8; 0xf6/0xf9 seen elsewhere); 24-byte phrase entries: u16 index, u16 beat, u16 kind, k1/k2/k3 flags, `b` + extra beats, `fill` + `beat_fill`                                          |

Open questions the page itself lists: PQTZ unk1/unk2, PVBR body, PCPT bytes 0x14-0x17 / 0x1d-0x1f, PCPT order_first and order_last
share an offset in the text (typo), PCP2 colour offset arithmetic, PWV4 entry format, PWV3/PWV5 header constants,
PSSI header bytes and k2 offset, PWV7 in `.EXT` or `.2EX`.

## Design (shared by all phases)

New files in libcdj: `src/anlz/anlz.h`, `src/anlz/anlz.c` (container + tags), plus `src/anlz/anlz_fake.c/.h`
(synthetic file builder for tests, like `src/nfs/fake_pdb.c`), `src/bin/xdj_anlz.c` (dump tool).
Add a `utf16be_decode()` next to `utf16le_decode()` in `src/nfs/utf16.c` (UTF-16BE, surrogate pairs; the
existing `utf16le_to_utf8` is ASCII-only and must not be used).

```c
/* ownership: anlz_t owns the file bytes (anlz_open_file) or borrows them (anlz_open_mem);
 * every pointer in a view struct points into that buffer or into memory owned by anlz_t,
 * and stays valid until anlz_close(). */
typedef struct anlz anlz_t;
typedef enum { ANLZ_OK=0, ANLZ_ERR_IO, ANLZ_ERR_MAGIC, ANLZ_ERR_TRUNC, ANLZ_ERR_BAD_LEN, ANLZ_ERR_NOMEM, ANLZ_ERR_ABSENT } anlz_err_t;

anlz_err_t anlz_open_file(const char *path, anlz_t **out);
anlz_err_t anlz_open_mem(const uint8_t *data, uint32_t len, anlz_t **out);   /* borrowed */
void       anlz_close(anlz_t *a);

/* generic access: every tag, including ones we do not decode */
typedef struct { char fourcc[5]; uint32_t offset, len_header, len_tag; const uint8_t *data; } anlz_tag_t;
uint32_t   anlz_tag_count(const anlz_t *a);
const anlz_tag_t *anlz_tag_at(const anlz_t *a, uint32_t i);
const anlz_tag_t *anlz_find_tag(const anlz_t *a, const char fourcc[4], uint32_t nth);

/* typed views, all return ANLZ_ERR_ABSENT when the tag is not in this file */
anlz_err_t anlz_path(const anlz_t*, const char **utf8);
anlz_err_t anlz_beatgrid(const anlz_t*, anlz_beatgrid_t *out);      /* PQTZ, falls back to decoded PQT2 */
anlz_err_t anlz_cues(const anlz_t*, int hot, anlz_cue_list_t *out); /* PCO2 if present, else PCOB */
anlz_err_t anlz_waveform(const anlz_t*, anlz_wave_kind_t kind, anlz_waveform_t *out);
anlz_err_t anlz_phrases(const anlz_t*, anlz_phrases_t *out);
```

Robustness contract: **every** read is bounds-checked against the buffer, `len_tag` below 12 or past EOF ends
iteration with `ANLZ_ERR_TRUNC`/`ANLZ_ERR_BAD_LEN` (tags parsed before the damage stay usable), unknown fourccs are
skipped, no function may read out of bounds or loop forever for any input. Counts are validated against
`len_tag` before use (never trust a `len_entries` field to size an allocation).

Test layers (each phase adds to them):
1. **Synthetic** files from `anlz_fake` for every tag, including malformed variants (always run, no data needed).
2. **Fixtures**: 7 real DAT+EXT pairs plus a truncated `USBMNG` header, 824 KB, committed in `src/test/fixtures/anlz/` (always run;
   `MANIFEST.text` lists tags/checksums, `expected.tsv` the matching `export.pdb` values: BPM, duration, title, track id).
3. **Corpus** (optional, developer only): whole real export, directory from env `ANLZ_TEST_DIR`
   (default `/var/tmp/rekordbox-example/PIONEER`), skipped with a message when absent and **never part of `make test`**.
   Use it to discover facts and to cross-check, then turn every finding into a fixture/synthetic assertion.
4. **Robustness**: truncation at every byte, bit flips, run under `-fsanitize=address,undefined`.

---

## Phase 1: Format doc, survey tool and fixtures

Goal: make the format knowledge durable and give every later phase real files to test against. No parser yet.

**Status: task 4 (fixtures) is already DONE** (copied from the temporary `/var/tmp/rekordbox-example`): 14 files + `MANIFEST.text` +
`expected.tsv` in `libcdj/src/test/fixtures/anlz/`. Do not re-pick them; do tasks 1-3 and 5, and add a Makefile/test check that the
fixture checksums in the manifest still match.

Read first: this plan; `libcdj/doc-internal/rekordbox-db-format.text`; `libcdj/src/test/pdb_parse_test.c.snip` (test pattern, 160 lines).

Tasks
1. Create `libcdj/doc-internal/rekordbox-anlz-format.text`: copy the "What we know" table and "Format reference" above, in the same plain-text style as the pdb doc. Mark each statement `verified:` (seen in real data) or `spec:` (from the web page only).
2. Write `src/bin/xdj_anlz.c` as a **standalone byte-level scanner** (does not use the Phase 2 parser, so it can later cross-check it): `xdj-anlz <file>` prints file header fields and one line per tag (`fourcc hdr len offset`), exit 1 on a structural error. `xdj-anlz --survey <dir>` walks `USBANLZ/**/ANLZ*.{DAT,EXT,2EX}` (skipping `USBMNG.DAT`) and prints per-extension tag-count tables like the one in this plan. Add `target/xdj-anlz` to the Makefile (`CFLAGS` as for `xdj-pdb`; it is not part of `libcdj.so`).
3. If `/var/tmp/rekordbox-example/PIONEER` still exists, run the survey and confirm it reproduces the table above (4369 files, tags tile exactly, tag counts); otherwise rely on the table and the fixtures. Fix the doc where reality differs.
4. ~~Pick fixtures~~ **DONE.** Fixture set (all `P<dir>_<id>_ANLZ0000.{DAT,EXT}`):
   - `P02A_0000ECBE` smallest pair; unanalysed track: empty `PQTZ`, no `PQT2`, no cues
   - `P042_00015D16` anchor: 716 beats, 123.00 BPM, one hot-cue loop 31271..39076 ms (short 0x2c `PCP2`)
   - `P04B_000145BD` has a `PVB2` tag; unanalysed
   - `P035_0002765B` a `PCP2` longer than 0x2c with colour bytes
   - `P014_00001A7A` two hot cues; `P07D_0001A2E9` four cues, 859 beats; `P024_00023562` two loops
   - `USBMNG_head.DAT` first 256 bytes of `USBMNG.DAT` (negative test: magic `PMNG`)
   - `MANIFEST.text` (why, size, checksum, tag list from an independent scanner) and `expected.tsv` (pdb values per track)
   **Gaps in the real data** (cover with synthetic tests): no cue comments, no memory points, no tempo changes, no `.2EX`, no `PSSI`, no `PWV6/7`.
5. Add `make survey` to the Makefile (`target/xdj-anlz --survey $${ANLZ_TEST_DIR:-/var/tmp/rekordbox-example/PIONEER}`); optional developer target, fails politely when the directory is absent.

Tests: `make target/xdj-anlz` builds clean; `target/xdj-anlz` on each fixture exits 0; survey output matches the doc.
Acceptance: doc on disk, scanner builds and exits 0 on every fixture (and 1 on `USBMNG_head.DAT`), survey numbers match when the corpus is available, manifest checksums verified by a test.

```sh
geany-progress done 1 -r libcdj/doc-internal/rekordbox-anlz-format.text -r libcdj/src/bin/xdj_anlz.c -r libcdj/src/test/fixtures/anlz/MANIFEST.text -w "Fixtures contain track paths/titles from the user's library"
```

---

## Phase 2: Container parser (`PMAI` + tag iteration)

Goal: safely open any ANLZ file and iterate its tags; no tag decoding yet.

Read first: `libcdj/doc-internal/rekordbox-anlz-format.text`, `src/nfs/pdb.c` lines 1-60 (helpers), `src/nfs/utf16.c`.

Tasks
1. `src/nfs/utf16.c/.h`: add `utf16be_decode(const uint8_t*, uint32_t, char *dst, uint32_t dst_max)` (same contract and surrogate handling as `utf16le_decode`, byte order swapped, stops at a NUL code unit). Keep it between the `//SNIP_utf16` markers.
2. `src/anlz/anlz.h/.c`: `anlz_t`, `anlz_err_t`, `anlz_open_mem`, `anlz_open_file` (reuse the `pdb_read_file` idea; reject files over 64 MB), `anlz_close`, `anlz_tag_count/at/find_tag`, big-endian readers `rbe16/rbe32` (static, bounds-checked wrappers `anlz_u32(a, off, &v)`).
   - Validate: length >= 28, magic `PMAI`, `len_header` >= 0x1c and <= file length. `len_file` mismatching the real size is **not** an error (record it in a `file_len_ok` flag): USB copies and partial downloads happen.
   - Index all tags up front into a malloc'd array (count is bounded by `len/12`). Stop at the first tag whose `len_tag` < 12 or runs past EOF: keep the earlier tags, return `ANLZ_ERR_TRUNC` from open **only if no tag was parsed**, otherwise OK with `a->damaged = 1` exposed via `anlz_is_damaged()`.
3. Makefile: add `target/anlz/anlz.o` to `OBJS` (+ `target/anlz` dir rule), install `src/anlz/anlz.h` to `/usr/include/cdj`, update `deploy/build-deb.sh` header copy.
4. `src/anlz/anlz_fake.c/.h`: `anlz_fake_begin()`, `anlz_fake_add_tag(b, "PQTZ", hdr_len, body, body_len)`, `anlz_fake_finish(b, &len)` producing a valid PMAI file with correct `len_file`/`len_tag`. Later phases add typed helpers (`anlz_fake_add_ppth`, ...).
5. Port `xdj_anlz.c` to also offer `--parsed` mode using the real parser and compare its tag list to the byte scanner in a test.

Tests (`src/test/anlz_container_test.c.snip`, add to `make test`):
- synthetic: empty file (header only) -> 0 tags; three tags -> order, fourcc, offsets, `anlz_find_tag(.., 1)` returns the second of two PCOB; unknown fourcc `ZZZZ` is skipped but listed.
- errors: NULL/short buffer -> `ANLZ_ERR_TRUNC`; wrong magic (`PMNG`, i.e. `USBMNG.DAT`) -> `ANLZ_ERR_MAGIC`; `len_tag` = 0, = 11, = 0xffffffff, running past EOF -> earlier tags kept, `anlz_is_damaged`; `len_file` lying -> still OK, flag false.
- fixtures: every `P*_ANLZ0000.*` file in `src/test/fixtures/anlz/` opens, its tag list (fourcc, offset, header and tag length) equals the one in `MANIFEST.text`, tags tile the file exactly (`len_header` + sum of `len_tag` == size); `USBMNG_head.DAT` -> `ANLZ_ERR_MAGIC`.
- `utf16be_decode`: ASCII, `Rebūke` (U+016B), surrogate pair, odd byte count, no NUL, too-small dst.

Acceptance: `make test` passes; `make` builds with `-Werror`; `nm -D target/libcdj.so | grep anlz_` lists the API.

```sh
geany-progress done 2 -r libcdj/src/anlz/anlz.c -r libcdj/src/anlz/anlz.h -r libcdj/src/nfs/utf16.c -r libcdj/src/test/anlz_container_test.c.snip -w "Needs sudo make install before vdj-ui can use the new header"
```

---

## Phase 3: Path, beat grid (`PQTZ`) and the compact grid (`PQT2`)

Goal: beat grid for every track, including the 5 tracks in this corpus where `PQT2` is absent and the (many) where only `PQT2` is usable on newer exports.

Read first: the PQTZ/PQT2 rows of the format doc; `src/anlz/anlz.h`.

Tasks
1. `anlz_path()`: decode `PPTH` (`len_path` @0x0c, UTF-16BE from 0x10, trailing NUL stripped). Result owned by `anlz_t` (decoded once, cached).
2. `anlz_beatgrid_t { uint32_t count; const uint8_t *raw; }` plus accessor `anlz_beat_at(g, i, &beat)` returning `{uint16_t beat_number; uint16_t bpm_x100; uint32_t time_ms;}`; validate `count*8 <= len_tag - 0x18`, clamp and flag when `len_beats` disagrees with the tag size (use the smaller).
3. Helpers on top: `anlz_beat_for_time(g, ms)` (binary search, returns index of the beat at or before `ms`), `anlz_avg_bpm(g)`, `anlz_downbeats(...)` (beat_number == 1). Times must be monotonic non-decreasing; the parser reports `monotonic` in the struct rather than failing.
4. **Reverse-engineer `PQT2`** (the spec page has nothing). **Time-sensitive:** this needs the whole export, which currently lives in the
   temporary `/var/tmp/rekordbox-example`. Do this step while it still exists, and before finishing it copy any track that
   behaved unexpectedly (disagreement, odd header words, variable tempo) into `src/test/fixtures/anlz/` and update `MANIFEST.text` /
   `expected.tsv`, so the conclusions stay testable without the export. Procedure, using the oracle:
   a. Write a throwaway tool `src/bin/xdj_pqt2.c` that for every DAT/EXT pair in the corpus prints the PQT2 header words, the PQTZ beat count and the first/last PQTZ entries beside them. Check the hypothesis: @0x1c..0x23 first anchor, @0x24..0x2b last anchor, count @0x2c... (**offsets here are from the real bytes: first entry `0001 300c 00000033` at 0x18, `0004 300c 000552a0` at 0x20, count `000002cc` at 0x28; confirm**).
   b. Study the packed section after the count (`07f8 3c89 ... 03cc 0305 023d 0176 00ae 03cf 0307 023f ...`): look for per-beat deltas of the time, grouped per bar, and for tempo changes; compare against PQTZ time deltas (`time[i+1]-time[i]`) for the same track and for tracks with tempo changes (variable-BPM tracks have PQTZ tempo entries that differ).
   c. Once a decoder reproduces PQTZ for the sample track, run it over all 2179 pairs. Iterate until **max |time error| <= 1 ms and beat_number/tempo identical** for all, or write down precisely which tracks differ and why in the format doc.
   d. Implement `pqt2_decode()` in `anlz.c`; `anlz_beatgrid()` prefers `PQTZ`, falls back to decoded `PQT2`, and reports which source was used (`g->source`).
   e. If after a bounded effort (~2 hours of analysis) it is not solved, ship the raw bytes (`anlz_beatgrid` returns `ANLZ_ERR_ABSENT` for EXT-only files) and record the partial findings; do not block later phases.
5. Document everything learned in `rekordbox-anlz-format.text` (mark `verified:`).

Tests (`src/test/anlz_beatgrid_test.c.snip`):
- synthetic PQTZ: 4 beats, tempo change mid-track, `len_beats` larger than the tag (clamped, flagged), `len_beats` = 0, entry straddling EOF.
- fixture `P042/00015D16` DAT: path == `/Contents/...` (exact string from the file), 716 beats, first beat `(1, 12300, 51)`, bar numbering cycles 1-4, monotonic, last time == 349 856 ms in the EXT PQT2 anchor.
- fixtures `P02A_0000ECBE` / `P04B_000145BD` (no `PQT2`, empty `PQTZ`): path still decodes, `anlz_beatgrid` returns count 0 (not an error), matching `expected.tsv` bpm 0.
- `anlz_beat_for_time`: before first beat, exactly on a beat, between beats, after last.
- fixtures with a grid (`P042`, `P014`, `P024`, `P035`, `P07D`): decoded `PQT2` == `PQTZ` (count, beat numbers, tempo, time within 1 ms); first tempo equals `expected.tsv` bpm_x100.
- optional corpus pass (developer only, not in `make test`): same comparison for all 2179 pairs; copy any track that disagrees into the fixtures.

```sh
geany-progress done 3 -r libcdj/src/anlz/anlz.c -r libcdj/doc-internal/rekordbox-anlz-format.text -r libcdj/src/test/anlz_beatgrid_test.c.snip -w "PQT2 layout is reverse-engineered; check the format doc for any tracks where it still differs from PQTZ"
```

---

## Phase 4: Cues, loops and memory points (`PCOB`/`PCPT`, `PCO2`/`PCP2`)

Goal: hot cues A..H, memory points, loops with colours and comments.

Read first: cue rows of the format doc; fixtures `P042_00015D16_ANLZ0000.EXT` plus the comment/colour fixture; `anlz.h`.

Tasks
1. Public structs: `anlz_cue_t { uint8_t hot_cue; /*0 = memory*/ uint8_t is_loop; uint32_t time_ms; uint32_t loop_time_ms; uint8_t color_id; uint8_t color_code, r, g, b; uint16_t loop_num, loop_den; char *comment; /*utf8 or NULL*/ uint16_t order_first, order_last; uint8_t status; }` and `anlz_cue_list_t { uint32_t count; anlz_cue_t *cues; }` (cues + strings owned by `anlz_t`, freed in `anlz_close`).
2. `PCOB` + `PCPT` (DAT and EXT): fixed 0x38-byte entries starting at `len_header`; tolerate `len_cues` larger than the tag (clamp); skip entries whose fourcc is not `PCPT`.
3. `PCO2` + `PCP2` (EXT): variable-length: advance by each entry's own `len_entry`; entries may end before the comment length field or before the colour bytes (the sample entry is 0x2c bytes): missing parts read as 0/NULL. Comment length must be even, bounded by `len_entry`. Resolve the spec's ambiguous colour offset **empirically**: find real entries with colour bytes (scan the corpus) and fix the offset from data; record in the doc.
4. `anlz_cues(a, hot, &list)`: merge rules: use `PCO2` when present (it supersedes `PCOB`), else `PCOB`; `hot` selects type 1 vs 0; result sorted by `time_ms` for memory points and by `hot_cue` for hot cues; both lists available via a single call `anlz_all_cues(a, &list)` for the UI. Memory cue = `hot_cue == 0`.
5. Colour helper `anlz_cue_color_rgb(cue)` (returns the stored RGB; fallback table for `color_code` 0x01..0x3e is **not** invented: leave as a TODO documented in the doc because the palette lives in Beat Link; ask the human to supply it if needed).
6. Extend `anlz_fake` with `anlz_fake_add_pcob/pco2` builders.

Tests (`src/test/anlz_cues_test.c.snip`):
- synthetic PCOB with 3 hot cues + loop; PCO2 with comment ("Drop") and colour; PCO2 entry truncated after `len_cues` claims 2 but only 1 present; entry with odd comment length; `len_entry` smaller than header (must stop, not loop forever).
- fixture `P042/00015D16`: exactly 1 hot cue: hot_cue 1, loop, time 31 271, loop_time 39 076, no comment, no colour; `.EXT` `PCOB` lists empty, `.DAT` hot-cue `PCOB` holds the same loop (PCPT and PCP2 must agree).
- `P035_0002765B`: the colour bytes of its single long `PCP2` entry (dump them with the scanner and write the exact values into the test); `P014`: 2 hot cues non-loop; `P07D`: 4 cues; `P024`: 2 loops. No real fixture has a comment: comments are synthetic-only.
- fixtures: all cue times <= last beat time + 1 s, `loop_time >= time` for loops, hot_cue in 0..8.
- optional corpus pass (developer only): the same invariants over every EXT file.

```sh
geany-progress done 4 -r libcdj/src/anlz/anlz.c -r libcdj/src/test/anlz_cues_test.c.snip -r libcdj/doc-internal/rekordbox-anlz-format.text -w "Hot cue colour palette (color_code 0x01-0x3e) not mapped to RGB; only stored RGB bytes are exposed"
```

---

## Phase 5: Waveforms (`PWAV`, `PWV2`, `PWV3`, `PWV4`, `PWV5`)

Goal: zero-copy access to every waveform in `.DAT`/`.EXT`, decoded per column for drawing.

Read first: waveform rows of the format doc; the PWV4/PWV5 sample bytes above.

Tasks
1. `anlz_wave_kind_t { ANLZ_WAVE_PREVIEW (PWAV), ANLZ_WAVE_TINY (PWV2), ANLZ_WAVE_DETAIL (PWV3), ANLZ_WAVE_COLOR_PREVIEW (PWV4), ANLZ_WAVE_COLOR_DETAIL (PWV5), ANLZ_WAVE_3BAND_PREVIEW (PWV6), ANLZ_WAVE_3BAND_DETAIL (PWV7) }` and `anlz_waveform_t { kind, count, entry_bytes, const uint8_t *data, double entries_per_sec }`.
2. Validate `count * entry_bytes <= len_tag - len_header`; clamp and flag otherwise. PWAV expects 400, PWV2 100, PWV4/PWV6 1200 (do not enforce, report).
3. Decoders returning a plain struct per column: `anlz_wave_column_t { uint8_t height; uint8_t whiteness; uint8_t r, g, b; uint8_t low, mid, high; }`; one function per kind (`anlz_wave_decode(w, i, &col)`).
   - PWAV: height = byte & 0x1f, whiteness = byte >> 5. PWV2: height = byte & 0x0f. PWV3 as PWAV.
   - PWV5: u16: red = bits 15-13, green = 12-10, blue = 9-7, height = 6-2 (scale 3-bit colour channels to 0-255 with `v * 255 / 7`). **Verify** against the sample (`ff80 ff80 ... 1780 1380 e000`): `0xff80` should be bright/white at height 0 or max? Decide from comparing the waveform shape with the PWAV preview of the same track, record in the doc.
   - PWV4: 6 bytes per column, format unknown. Reverse-engineer: compare the 1200 columns to the PWV5/PWAV shape of the same track (downsample PWV5 150/s -> 1200 columns), hypothesise (e.g. height/whiteness/colour or low/mid/high pairs), confirm statistically across the corpus, document. If unresolved, expose raw 6-byte entries and say so in the doc.
   - PWV6/PWV7: 3 bytes (mid, high, low); synthetic tests only (no `.2EX` in this corpus). Mark `spec:` unverified.
4. Timing helper: `anlz_wave_index_for_time(w, ms)` for detail waveforms (150 entries/s: index = ms * 150 / 1000), and `anlz_wave_preview_x(w, ms, track_ms)` for previews (column = ms * count / track_ms).
5. Extend `anlz_fake` with `anlz_fake_add_waveform(kind, n)` filling deterministic patterns.

Tests (`src/test/anlz_waveform_test.c.snip`):
- synthetic: each kind decodes known byte -> known column values; `count` larger than data (clamped); zero entries; PWV5 bit-field round trip for every combination of the 4 fields (build u16 from fields, decode, compare).
- fixtures: PWAV count 400, PWV2 100, PWV4 1200, PWV3/PWV5 counts match `len_entries`; PWV5 `count/150` within 1 s of the last beat time; max height in PWAV >= 1 (not silent) and <= 31.
- fixtures: `count*entry_bytes + len_header == len_tag` for every waveform; PWV5 duration vs PQTZ last beat within 2 s for every fixture that has both.
- optional corpus pass (developer only): the same for all EXT files.

```sh
geany-progress done 5 -r libcdj/src/anlz/anlz.c -r libcdj/src/test/anlz_waveform_test.c.snip -r libcdj/doc-internal/rekordbox-anlz-format.text -w "PWV4 layout is reverse-engineered; PWV6/PWV7 are spec-only (no .2EX data to verify)"
```

---

## Phase 6: Song structure (`PSSI`) and `PVBR`/`PVB2` raw access

Goal: phrase analysis (intro/verse/chorus...) and honest access to the tags we do not understand.

Read first: PSSI row of the format doc.

Tasks
1. `PSSI`: unmask the body into a malloc'd copy owned by `anlz_t`: mask byte i = `(base[i % 19] + len_entries) & 0xff`, applied to bytes after the 0x12? **Confirm the exact starting offset** against the page's text and the Beat Link source description before coding: the spec says everything after `len_entry_bytes`/`len_entries`; test the hypothesis by checking the unmasked mood is 1-3 and the first entry `index == 1`.
2. `anlz_phrases_t { mood, bank, end_beat, count, anlz_phrase_t *p }`, `anlz_phrase_t { index, beat, kind, k1, k2, k3, b, beat2, beat3, beat4, fill, beat_fill }`; helpers `anlz_phrase_name(mood, phrase)` (table from the spec: low/mid/high labels) and `anlz_bank_name(bank)` (0 Default(=Cool), 1 Cool ... 8 Club 2, others "Unknown (0xNN)").
3. **No PSSI exists in this corpus** (older rekordbox export). So: tests are synthetic only, built by an `anlz_fake_add_pssi()` that applies the same XOR in the generator (write it independently of the decoder, from the spec, so a shared bug cannot hide). Record `spec:` unverified in the doc and ask the human for a track analysed by rekordbox 6+ to add a real fixture (note this in the done warning).
4. `PVBR` and `PVB2`: expose raw (`anlz_tag_raw(a, "PVBR", &data, &len)`); no decoding. Add to the format doc the observed sizes (`PVBR` len_tag 0x654 in the sample) and that the sample body is mostly zeros.

Tests (`src/test/anlz_phrases_test.c.snip`): synthetic round trip for each mood (1/2/3) and bank (0..8, 0xf6); `len_entries` = 0; `len_entries` larger than tag (clamped); `b` = 0 and 1 variants (beat2..4); phrase names for each mood; raw tag access returns the exact bytes for `PVBR`/`PVB2` fixtures and `ANLZ_ERR_ABSENT` otherwise.

```sh
geany-progress done 6 -r libcdj/src/anlz/anlz.c -r libcdj/src/test/anlz_phrases_test.c.snip -w "No real PSSI sample available: decoder is verified against a synthetic generator only; ask for a rekordbox 6+ export"
```

---

## Phase 7: Track-level API and path resolution

Goal: one call that gives an application everything about a track, hiding DAT/EXT/2EX.

Read first: `src/nfs/pdb.h` (for `analyze_path`), `anlz.h`.

Tasks
1. `anlz_track_t anlz_track_load(const char *root, const char *analyze_path, anlz_err_t *err)` (name: `anlz_track_open`): `analyze_path` is the DB path of the `.DAT` (`/PIONEER/USBANLZ/P02E/0002F1FC/ANLZ0000.DAT`); open `root + analyze_path`, then the sibling `.EXT` and `.2EX` (same name, other extension; **case-insensitive on the extension** because USB sticks vary), each optional. Reject `analyze_path` containing `..` or a missing leading `/` (it comes from an untrusted database).
2. `anlz_track_t` exposes: `path`, `beatgrid` (best source), `cues` (all, merged), `waveforms[]` (best available per kind), `phrases`, `has_dat/has_ext/has_2ex`, and `anlz_track_close()`.
3. A pure function `anlz_resolve_path(root, analyze_path, ext, out, outlen)` also exported (vdj-ui needs paths for NFS-served databases where it fetches bytes itself and then calls `anlz_open_mem`).
4. `xdj-anlz --track <root> <analyze_path>` prints a human-readable summary (beats, BPM range, cues, waveform sizes, phrases). `xdj_pdb` gains `--anlz` to print the same summary for each track using the new API (cross-check: PDB `bpm_x100` vs first PQTZ tempo, PDB duration vs last beat).

Tests (`src/test/anlz_track_test.c.snip`): path traversal rejected (`/PIONEER/../etc/passwd`, no leading slash, embedded NUL); sibling lookup with `.ext` lowercase (copy fixtures into a temp dir tree `PIONEER/USBANLZ/P042/00015D16/` created by the test); DAT-only track (EXT missing) still returns beat grid + PWAV; unreadable EXT -> `has_ext == 0`, not an error; fixture end-to-end checks (716 beats, 1 loop, PWV5 present). Cross-check against `src/test/fixtures/anlz/expected.tsv` (no `export.pdb` needed): for every row, `analyze_path` resolves to the fixture DAT, first beat tempo equals `bpm_x100` (0 bpm rows have an empty grid) and the last beat time is within 10 s of `duration_secs`. Optional developer pass over the full export with `export.pdb`: same checks for >= 95 % of tracks (BPM within 1.0), print the outliers.

```sh
geany-progress done 7 -r libcdj/src/anlz/anlz.h -r libcdj/src/anlz/anlz.c -r libcdj/src/bin/xdj_pdb.c -w "Tolerances (95%, 1 BPM, 10 s) are heuristics; inspect the printed outliers"
```

---

## Phase 8: Robustness, fuzzing and optional corpus validation

Goal: prove the parser survives real and hostile data, and make that repeatable. Everything in `make test` / `make test-asan` runs on the committed fixtures and synthetic files only; the full-export pass is a separate optional target.

Tasks
1. `src/bin/anlz_corpus_check.c` (a small C program run by the **optional** `make test-corpus`, not by `make test`): walk `ANLZ_TEST_DIR` (default `/var/tmp/rekordbox-example/PIONEER`, skip with `[skip]` when absent), open every `.DAT/.EXT/.2EX`, and assert the invariants from phases 2-7 in one pass: tags tile the file; no `damaged`; every typed accessor returns OK or ABSENT (never an error) for the tags the survey says exist; per-extension tag-set expectations; total counts printed (files, tracks with cues, with loops, with comments, with PQT2-only). Runtime must stay under ~30 s for 4369 files. The checks it makes must also be asserted on the fixtures by `make test` (so nothing depends on the export being present).
2. `make test-asan`: rebuild the parser + tests with `-fsanitize=address,undefined -g -O1` and run the container, beatgrid, cue, waveform, phrase and track tests, plus the truncation and mutation tests below, on the fixtures (and the corpus pass only when `ANLZ_TEST_DIR` exists).
3. Truncation test: for each fixture, parse the first `n` bytes for **every** `n` from 0 to size (stride 1 for files < 20 KB, stride 7 above); must return without crash, with `ANLZ_OK` or an error, and typed accessors on the truncated file must not read past `n` (ASAN proves this).
4. Mutation test: deterministic PRNG (seed printed, overridable via `ANLZ_FUZZ_SEED`): 2000 iterations per fixture flipping 1-8 random bytes (bias toward bytes in tag headers and length fields), run every accessor; no crash, no hang (guard with `alarm(5)`).
5. Optionally a libFuzzer/AFL entry point `src/test/anlz_fuzz.c` (not part of `make test`; documented in the format doc with the command to run).
6. Record the corpus statistics (file counts, tag counts, cue/loop/colour counts) in the format doc under "Corpus statistics", noting they came from the temporary export, so regressions are visible and the numbers survive its deletion.

Acceptance: `make test` and `make test-asan` pass on a machine without `/var/tmp/rekordbox-example`; `make test-corpus` passes where the export exists; no sanitizer reports.

```sh
geany-progress done 8 -r libcdj/Makefile -r libcdj/src/bin/anlz_corpus_check.c -w "make test must pass without /var/tmp/rekordbox-example; only make test-corpus uses it (override with ANLZ_TEST_DIR)"
```

---

## Phase 9: vdj-ui integration (follow-up, separate session)

Goal: show analysis data in the track details pane. Only start after the human has run `sudo make install` for libcdj.

Read first: `vdj-ui/src/ui/track_browser.cpp` (`loadPdb`, `buildInfo`), `vdj-ui/src/ui/track_details.cpp`, `vdj-ui/tests/test_rekordbox.cpp`.

Tasks
1. `TrackInfo` gains `analyzePath`, resolved to the real file at load time (`root + analyze_path`).
2. Selecting a track calls `anlz_track_open` lazily (not for all 2184 tracks at load) and fills: beat count, BPM range, first beat offset, hot cues (letter, time `m:ss.mmm`, comment, colour swatch), memory points, loops (with length in beats), phrases (mood, bank, list).
3. New `WaveformWidget` (QWidget, `paintEvent`) drawing the PWV5 colour preview (or PWAV when missing) under the artwork with cue markers on top; clicking a cue selects it. Rendering code stays in vdj-ui; libcdj only provides decoded columns.
4. Playlist export (existing feature) optionally writes hot cues/memory points into the `.txt` as comment lines: **ask the human first** - the current `.txt` is meant to stay a plain ordered file list.
5. Tests in `tests/test_rekordbox.cpp`: selecting `P042/00015D16`'s track shows 716 beats and the loop cue; selecting a track without an `.EXT` shows the DAT data and no error; selecting a group row clears the analysis section; waveform widget renders without crashing offscreen (`grab()` returns a non-empty image).

```sh
geany-progress done 9 -r vdj-ui/src/ui/track_details.cpp -r vdj-ui/src/ui/track_browser.cpp -r vdj-ui/tests/test_rekordbox.cpp -w "Requires the installed libcdj from phase 7; loads analysis lazily per selected track"
```

---

## Risks and open items

| Risk                                                                                 | Mitigation                                                                                                                               |
|--------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------|
| `PQT2` and `PWV4` are undocumented                                                   | Oracle-driven reverse engineering (PQTZ, PWV5/PWAV of the same track); bounded effort; raw-bytes fallback; findings go in the format doc |
| No `PSSI`, `.2EX`, `PWV6/7` in the corpus                                            | Synthetic generator written independently of the decoder; request a rekordbox 6+ / CDJ-3000 export for fixtures                          |
| Hostile or truncated files (USB corruption is the use case for the recovery feature) | Bounds-checked reads everywhere, damaged-file mode keeps earlier tags, ASAN + truncation + mutation tests                                |
| `analyze_path` comes from an untrusted database                                      | Phase 7 path validation                                                                                                                  |
| Library install needs sudo                                                           | Human step called out in every phase that needs it                                                                                       |
| Spec inconsistencies (PWV7 file, PCPT order offsets, PCP2 colour offset)             | Settle each from real data; mark `verified:` / `spec:` in the format doc                                                                 |
