# Problem statement

Pioneed rekordbox and the decks lookup ANALZ files on the pendrive, they have a full parh to the file but the DO NOT use it for anything except the file hname at the end.

ANALZ files are stored in paths like this.

/Content/ P0xx /000xxxxx / ANZL000001,DAT

This is clearly to avoid accidentally having 1000s of audio files in the same directory. Decks have a hard limit of 1000 entries per directory, due to memory constraints.

- We presume `P` = Partition, the first layer of directories.
- The values of `P` turnout to be simply a few bits from the hash. 
  - This makes sense. Once you have a nicely distributed 32bit hash, and you need a shorter number, you just take a few bits from the big number.
  - The bits chosen seem to be consistent but we did not find a pattern to why those bits are chosen.
- The next layer is a simple 32bit hash which we had to reverse engineer
  - We duduced the Hash input simply by changing shit and seeing what did not matter. The only input to the hash is the full path.
  - You can tell from the size of the numbers its `% 200003`.
  - You can tell from the distribution its not a crypto hash, it has observable patterns, we tried common hashes likd sha235 to no avail
  - So presuming it is simple, it runs fast on lowend hardware, we just needed the constants in the function, for even distributions they needed to be prime numbers.

## Hashing Magic Numbers

- **200003* - The first prime above 200,000. 
- K = 0x34F5501D is 888,492,061, which is two prime factors 23497 × 37813.
- W = 0x93B6 is 37,814, which is 37813 + 1. So K and W share 37813, with W one more than a factor of K.



**Terminology used below.** The analysis files live in `PIONEER/USBANLZ/<partition>/<slot>/ANLZ<n>.DAT|EXT`:

* **partition** = the first folder, `P000`..`P07F` (what the notes first called `P` / the first number / `Pxxx`).
* **slot** = the second folder, 8 hex digits, 0..200002 (what the notes first called `Y` / the second number / `yyyyyyyy`). It is the index into the 200003 slot table of `USBMNG.DAT`.
* **collision counter** = the `n` in `ANLZ<n>`, for tracks that land in the same slot.

The partition is seven bits of the slot (see "SOLVED" below), the slot is `H(path) mod 200003`. Older sections keep the first names (`P`, `Y`); read `P` as the partition and `Y` as the slot.


## The algorithm and its constants (SOLVED, read this first)

Found in `rekordbox.exe` (the loop and the division by 200003, pasted by the user) and checked against 871 of 871 real path/slot pairs (`usbanalz-data/horner_test.py`).

| Constant             | Value                                      | Meaning                                                                             |
|----------------------|--------------------------------------------|-------------------------------------------------------------------------------------|
| multiplier `K`       | `0x34F5501D` (888 492 061 = 23497 × 37813) | Horner multiplier, applied to the running hash before each character                |
| character weight `W` | `0x93B6` (37 814 = 37813 + 1 = 2·7·37·73)  | each character code is multiplied by this before it is added                        |
| start value          | `0`                                        | the hash starts at zero (a non-zero start does not fit the data)                    |
| word size            | 32 bit, wrapping                           | every multiply and add is mod 2^32                                                  |
| slot count `M`       | `200003` (`0x30D43`, prime)                | slot = `h mod M`; the same count as the `USBMNG.DAT` slot table                     |
| division magic       | `0xA7C5075B`, shift 17                     | the compiler's `h / 200003` (`ceil(2^49 / 200003)`), not a hash constant            |
| partition bits       | slot bits `0, 2, 6, 7, 9, 13, 16`          | partition bit k = slot bit listed k-th; result `0x00`..`0x7F`                       |
| character unit       | UTF-16 code unit                           | the exe walks 16 bit words; ASCII is the character code, others use surrogate pairs |
| terminator           | not hashed                                 | the zero word ends the loop and is not added                                        |

The input is the track's file path exactly as stored in the database row, `/Contents/<artist>/<album>/<file>.<ext>` (leading `/`, case and
separators count, names already truncated to 48 characters as described in `rekordbox-db-output.md`).

```
h = 0
for c in utf16_units(path):
    h = (h * 0x34F5501D + c * 0x93B6) mod 2^32
slot = h mod 200003
partition = sum(((slot >> b) & 1) << k  for k, b in enumerate([0, 2, 6, 7, 9, 13, 16]))
analysis file = /PIONEER/USBANLZ/P%03X/%08X/ANLZ%04X.DAT   (partition, slot, collision counter n = 0, 1, ...)
```

Consequences that explain everything measured earlier: the weight of a character is `W * K^distance_from_end` (geometric, end anchored, linear in the
character codes, which is why the earlier tests saw "weights that depend only on distance from the end"); `2^32 mod 200003 = 102874` is the wrap carry;
the hash is over the path, never the audio or tags. Implemented in `src/trakcrate/pdb_build.cpp`. The sections below record the search and are history.

Wehn rekordbok exports ANALZ files it puts them in a diretory strucuture

P0xx
  000xxxxx
  000xxxxy
  
If the structure is wrong it seems the decks cannot find the analysis and it does not show the beat  grid waverform or cue points
So when trakcrate exports it needs to know the hashing alog to put the files in the correct place.

We suspect this is to ensure that no directory has too many files, to keep memory usage down.

Wihtout this hash algo we can't export.

We have determined that the has is based on track name and path annd not audio data.

I haven't found the hash, but we know a lot more about it now....

What the new data shows
  - A 32-bit hash reduced modulo 200003. For every position in export3, the change from a b to a c is either exactly double the b change, or off by 102874 or 97129 (which add up to 200003). 2³² mod 200003 is exactly 102874. So
    the second number is H mod 200003, where H is a wrapping 32-bit hash. My earlier searches used 200000, so they could not have matched.
  - The audio isn't part of it. The different-audio stick got the same folder.
  - The first part (0–127) is not the low 7 bits of the same hash. Its changes don't double from b to c.

What didn't work with the correct modulus. I rechecked all the named hash families against the 49 clean points. I also searched every multiplier up to 2²⁴ for four multiply-and-add or multiply-and-xor forms, with 16 starting
values, in both directions. About 10,000 multipliers fit the first path by chance, as expected, and none fit more than 3 of the 49. So it is not one of those simple forms.

The hash is 32-bit wrapping arithmetic over the path, but not a plain multiply-and-add with a small multiplier. 
It might be a two-step mix (shifts and xors), which is common for short hashes, or it might feed more than the path into the state.

What the per-position analysis established
  - Exactly linear in the characters. The second number is H mod 200003, where H is a wrapping 32-bit sum with a fixed weight per character position. For all 24 positions, changing a letter from a to b to c moves H by one
    weight and then two. The only deviations are the wrap-around carries, and the carry size matches 2³² mod 200003 = 102874 exactly.
  - The weights are not a simple pattern. I searched all 2³² multipliers for a polynomial hash (forward and reverse, two exponent offsets, one byte per character) and found none. I also looked for weights that form a geometric
    progression, with a free starting value, and for weights that form an arithmetic progression. Neither fits beyond three positions. So it's not the usual multiply-and-add hash (djb2, FNV, sdbm, Java), and it's not a
    position-times-character sum.
  - The first part of the name is something else. It is not a linear function reduced modulo any m up to 70000, and it is not the high part of the same H.
  - The Pro DJ Link magic isn't involved, as far as I could test.


---

# Reference notes (added during the analysis)

Everything below was measured, not assumed. Scripts and data are in `doc/trakcrate/usbanalz-data/`.

## Hardware results on real decks (single track unless stated)

| Test                  | What was on the stick                                                                               | Stored waveform / cues                                              |
|-----------------------|-----------------------------------------------------------------------------------------------------|---------------------------------------------------------------------|
| Real rekordbox export | rekordbox's own files                                                                               | works                                                               |
| Our export (original) | our `export.pdb`, our ANLZ, our name `P007/000236CC`                                                | **no** (deck draws a waveform live only when played from the start) |
| B                     | real `export.pdb` + `exportExt.pdb`, our ANLZ in the real name `P049/00010C89`                      | works                                                               |
| E                     | all of ours, only the folder renamed to `P049/00010C89` (path in `export.pdb` patched, same length) | **works** (repeated on a freshly formatted stick)                   |
| F                     | the real databases + our ANLZ + four of our track-row fields                                        | works                                                               |
| C                     | our `export.pdb` with the real `exportExt.pdb`                                                      | no                                                                  |
| D                     | our `export.pdb` with the bitrate field set to 0 (the real value for VBR)                           | no                                                                  |
| A                     | our database, our new name, **rekordbox's own ANLZ files**                                          | no                                                                  |
| G / H                 | our files with only the first / only the second half of the real name                               | no / no                                                             |
| I                     | our ANLZ with the PMAI header word set to the real value                                            | no                                                                  |

Reading: with the name the deck expects, our ANLZ and our database are accepted (B, E, F). Giving it a name it does not expect fails even
when the ANLZ files are rekordbox's own (A). So the name is checked or recomputed by the deck, and the files themselves are not the problem.

## Properties of the name that are established

* Same exported path gives the same folder in independent exports (2073 of 2073 common tracks between two big exports), although their
  track ids differ in 59 cases. The track id is not the key.
* The audio is not part of the key: the same artist / album / file name with different audio gave the same folder (`P046/00018D74`).
* The playlist is not part of the key: a track in two playlists has one ANLZ folder.
* The same track imported in the PC library (rekordbox 5 `PIONEER/USBANLZ`, input path `C:/...`) has a different folder from its USB export.
  For the same-length tail of a path, the second number differs by a constant (11876 in `export1`): the prefix acts as an additive constant.
* Case matters: 76 paths exist in two spellings (`/Contents/Intapol/...`, `/Contents/intapol/...`), none share a folder.
* Ranges: the first part is 0..127 (`P000`..`P07F`), the second is below 200003 (observed maximum 199998, so 8 hex digits that always start `000`).

## Name-part tests on a real rekordbox stick (single track, rekordbox's own files)

The stick is rekordbox's own export of `/Contents/Intapol/Afrovox EP/01_afrovox.mp3`, real name `P049/00010C89/ANLZ0000.DAT`. One thing was changed
at a time, in the folder *and* in both copies of the track row in `export.pdb` (the string has the same length, so nothing else moves):

| Change                                               | Result on a real deck                                 |
|------------------------------------------------------|-------------------------------------------------------|
| first part `P049` -> `P04A`                          | **not found** (no stored waveform, beat grid or cues) |
| second part `00010C89` -> `00010C8A`                 | **not found**                                         |
| file name `ANLZ0000.DAT/.EXT` -> `ANLZ0001.DAT/.EXT` | **found, works**                                      |

So the deck takes the file name from the database string, but it does *not* follow the directory part of the string: it rebuilds `Pxxx/yyyyyyyy`
itself, and both parts must be exactly what rekordbox would produce. The directory string stored in `export.pdb` is not what the deck uses to open the file.
(Corollary: the input of the hash cannot be that analysis path string, since it contains the answer. It is some other field of the track row, almost
certainly the audio path `/Contents/<artist>/<album>/<file>`, which fits all the controlled experiments.)

## The second number (`yyyyyyyy`)

`Y = H mod 200003` where `H` is a wrapping 32-bit value that is linear in the character codes of the path:

* In `export3` (one 24 letter path, each letter changed to `b` and to `c`) the effect of `c` is exactly twice the effect of `b`, modulo 200003,
  except for multiples of `2^32 mod 200003 = 102874` (the carry of the 32-bit wrap). All 24 positions behave this way (`linear_check.py`).
  The implied `q = H div 200003` has second differences of only 0, +-1 and +-21475 (about `2^32 / 200003`).
* 200000 does not fit (it leaves unexplained +-3 differences), 200003 (a prime) explains them all.
* Position weights are therefore fixed numbers `w_p` (mod 2^32) per position and `H = const(length) + sum(char_p * w_p)`.

### What the weights are not

* Not a polynomial `h = h*B + c` (forward or reversed, one or two bytes per character, start offsets 0/1, any `B` in 1..2^32-1 checked against the first path
  and then all 24 positions).
* Not a geometric progression with a free start (`w_p = A*r^p`): the triples that fit `w21*w23 = w22^2` do not extend past three positions.
* Not an arithmetic progression (`w_p = A + p*d`).
* Not a hash of the file name alone, not of the directory alone, not case folded, not any of: CRC-16 (4 variants), CRC-32, Adler, Fletcher, BSD sum,
  PJW/ELF, Jenkins one-at-a-time, SuperFastHash, sdbm, DEK, RS, AP, BP, Knuth multiplicative, ror13, FNV 32/64, djb2, murmur3, md5/sha1/sha2/sha3/blake2 (hex
  crops, 32 bit windows, mod 200000 / 200003, with and without the Pro DJ Link magic `Qspt1WmJOL` as prefix, suffix or seed).

## The first number (`Pxxx`)

* It is **not** `H mod 128`, not any `H`-bits window, and not `(H div 200003) mod 128` (matches 1 of 24, chance level).
* It is not a linear function reduced by any modulus up to 70000 (the same second-difference test as for `Y` finds nothing).
* `a/c/x.mp3` and `c/a/x.mp3` have the same first number in both the PC library (`P005`) and the USB export (`P00F`), while `a/b/x` and `b/a/x` differ.
  That is a real coincidence in two independent schemes (about 1 in 16000 by chance).
* So it is a different, probably nonlinear function of the path (or of a different part of it).

## Ideas not tried yet

* A table of per-position constants, or weights from a pseudo-random generator (a linear congruential sequence would make the *differences* of the weights
  geometric; this is cheap to test once more positions are measured).
* Two or four bytes per step (weights `256^k * B^n`), which gives more structure than the one-byte forms tried.
* Longer paths: `export3` only measures weights for positions 5..30 from the end. A set with 100+ character paths gives the rest.
* Different lengths of the same text, to see how the length enters the constant.
* A closer look at what the 32-bit `H` could also be fed with (the `trakcrate` export path only, the artist, album and title tags separately).

## Experiment to prove the name (not the content) is what the deck checks

The stick `/tmp/ab-R` (not in the repo) is a copy of the real rekordbox export of the 49 `export3` tracks with only the names of three analysis folders
changed (the string in `export.pdb` is patched to match, same length). The files inside are rekordbox's own.

| Track (title) | What changed                              | If it shows waveform + cues                                           |
|---------------|-------------------------------------------|-----------------------------------------------------------------------|
| `aaaaaaaa`    | nothing (control)                         | the stick itself is fine                                              |
| `aaaaaaab`    | first part only `P00F` -> `P010`          | the deck ignores the first part                                       |
| `aaaaaaac`    | second part only `0001A890` -> `0001AB99` | the deck ignores the second part                                      |
| `aaaaaaba`    | both parts                                | the deck does not check the name at all (then the cause is elsewhere) |

Expected, if the deck recomputes the name: only the control works. Exact names (path `/Contents/aaaaaaaa/aaaaaaaa/<title>.mp3`):

* `aaaaaaaa`: `P025/00022051`, unchanged
* `aaaaaaab`: `P00F/00005DCF` -> `P010/00005DCF`
* `aaaaaaac`: `P068/0001A890` -> `P068/0001AB99`
* `aaaaaaba`: `P01A/000282A6` -> `P01B/000285AF`

All 49 clips are the same audio, so a waveform is either there or drawn live. A further control worth doing after this: swap the folders of two tracks
(each name is then a name rekordbox really produced, for the other track). If the deck recomputes the name, both swapped tracks fail even though
every folder is "valid".

## Questions for the rekordbox format community

* Does anyone know how `PIONEER/USBANLZ/Pxxx/yyyyyyyy` is derived, and what the P bucket count (128) and the second range (200003) mean?
* Is the input the exported `/Contents/...` path as stored in the track row (string 20), or something derived from it?

## Long-path survey (`export4`: 48 + 48 + 48 letters, one letter changed to `b` and `c` at each position)

Data: `usbanalz-data/export4-long-path-289.json` (every exported path with its `[P, Y]`), `export4-positions-140.json` (base value and the b / c results per position).

* Rekordbox cut the file names to 48 characters **including** `.mp3` (44 letter stems). Variants that differed only in the cut-off letters collided and were
  renamed `-1`, `-2`, ... so 140 of the 144 positions are usable. The folder names (artist, album) are cut to 48 as well. The hash therefore sees the
  shortened names rekordbox wrote, not the tag text.
* **Every one of the 140 positions changes both numbers**, including the first letter of the artist and the last letter of the stem. No position is
  ignored, so the hash is not over a prefix or over the file name alone (of the path pieces, all three are used).
* Case matters (76 real paths exist in two spellings, none share a folder).
* The extension and the length of the stem were not varied in `export4` (see `export5`: `.MP3 .wav .flac .aiff .m4a`, stems of 1 to 12 letters).
* The 140 weights (the effect of one letter at each position) show **no structure**:
  * no constant ratio between neighbours (checked all 139 adjacent pairs for a ratio `r`, including 29 which looked promising on one pair),
  * no polynomial in the position (finite differences up to degree 7),
  * no repeat with a period up to 99,
  * no two equal weights.
  They look like 140 unrelated 32-bit constants (or a long pseudo-random sequence).
* The `b` and `c` variants only give each weight modulo 200003 plus the wrap carry. To get the exact 32-bit weights, letters far from `a` (`d` .. `z`, digits)
  are needed so that the wrap count changes many times at each position.

## Which fields are the input?

The deck only takes the *file name* out of the analysis path in the database and rebuilds both folders itself, so the input may not be the path
at all, but the data of the track row that the path was built from. Facts that already narrow it down:

* Not the track id (59 tracks have different ids in two exports and the same folder).
* Not the audio, its size or its duration (same exported path, different audio: same folder).
* Not the playlist (the track is stored once whichever playlists contain it).
* The exported path is built from three things only: the artist tag (cut to 48), the album tag (cut to 48) and the file name (cut to 48 with the
  extension). Changing any of these changes the folder, and the title tag was the same as the file name in every set so far, so **the title is still undistinguishable
  from the file name**.

### Metadata probes (`export6`)

Each probe is the same track as in `export4` (same artist, album and file name, so it has a **known** `[P, Y]` there) with exactly one other
property changed. All probes can go in one export (each has a different path, so there is no collision); the result of each is compared with `export4`'s value for the same path. A difference means
that field is in the hash. The fields: title, artist tag longer than the 48 cut-off, album tag longer than the cut-off, genre, comment, year, BPM tag, key,
label, composer, track number, embedded artwork, all of them at once, and an unchanged control.

## The weights are anchored at the END of the path

Compare the last eight stem letters of `export3` (path `/Contents/aaaaaaaa/aaaaaaaa/aaaaaaaa.mp3`, 38 characters) with the same letters of `export4`
(160 characters): at the same distance `p` from the end (p = 5 for the letter before `.mp3`, up to p = 12) the effect of changing one letter on the
second number is **identical** modulo 200003 (up to the 102874 wrap carry): 8 of 8 positions agree (exact residues 84673, 142065, 172628, 107410 at p = 5, 8, 10, 12;
the others differ by exactly 102874). The first artist letter, at the same index from the *start* in both sets, has completely different effects
(33044 against 144468).

So `H = sum over characters of code * w[p]` where `p` is the distance from the end and **`w[p]` does not depend on the length of the path**. That is the
property of a hash computed by walking the string from the start with a state update (`h = f(h) + c`), as opposed to a table indexed by position from the start.
The weights `w[p]` still do not follow `B^p` (see above), so `f` is not a plain multiplication.

## Extension and length (`export5`, data in `usbanalz-data/export5-extension-and-length-16.json`)

Same artist and album (`aaaaaaaa`), stem `aaaaaaaa`:

| File                            | `[P, Y]`                                                                                                              |
|---------------------------------|-----------------------------------------------------------------------------------------------------------------------|
| `aaaaaaaa.mp3` (from `export3`) | `P025/00022051`                                                                                                       |
| `aaaaaaaa.MP3`                  | `P036/00006F4E`                                                                                                       |
| `aaaaaaaa.wav`                  | `P06C/000131FA`                                                                                                       |
| `aaaaaaaa.flac`                 | `P03E/00026BDE`                                                                                                       |
| `aaaaaaaa.m4a`                  | `P034/00006F72`                                                                                                       |
| `aaaaaaaa.aiff`                 | exported as `/Contents/UnknownArtist/UnknownAlbum/aaaaaaaa.aiff` (no tags were read from the AIFF), so not comparable |

**The extension is part of the hash, and so is its case** (`.mp3` and `.MP3` differ). Lengths 1..12 of the stem all have different folders too
(`a.mp3` is `P06B/0001E995`, `aa.mp3` is `P069/0001BC8B`, ...), see the data file.

## Practical consequence

Because the weights depend only on the distance from the end and the extension is included, the problem reduces to finding one sequence `w[0] .. w[~155]`
(plus whatever the first number uses). That sequence can in principle be estimated from the thousands of real paths.

## Metadata probes (`export6`) - result

All 14 probes exported to exactly the folder of their `export4` counterpart (same artist folder, album folder and file name), including the unchanged control, which
reproduced `[47, 12757]` (so the procedure is sound). Probes (list in `usbanalz-data/export6-probes.json`):

| Changed (path unchanged)                                          | Folder |
|-------------------------------------------------------------------|--------|
| title tag                                                         | same   |
| artist tag 50 letters (folder cut to 48)                          | same   |
| album tag 50 letters (folder cut to 48)                           | same   |
| genre, comment, year, BPM tag, key, label, composer, track number | same   |
| embedded artwork                                                  | same   |
| all of them at once                                               | same   |

Conclusion: **nothing in the metadata is hashed**. The two overflow probes are the strong ones: the artist and album *tag text* differs (50 letters against 48) while
the folder name is the same, and the result is unchanged, so the input is not the tag text but the **path as rekordbox wrote it**
(`/Contents/<artist folder>/<album folder>/<file name with extension>`, after the 48 character cut-offs), or a part of it. Together with the earlier facts
(every letter of the artist folder, album folder and file name matters; the extension and its case matter; the weights are anchored at the end), the input is almost certainly the exported file path string
(string 20 of the track row) or the bytes of it.

Still unknown: whether `/Contents/` and the separators are part of the input (nothing varied them), and the function itself.


Strangly the deck can play a path like this /Contents\Intapol/Afrovox EP/01_afrovox.mp3  but does not find the analysis, so this indicates `/` is part of the hash.

So the input is the whole string /Contents/<artist>/<album>/<file>.<ext>, with the case and the separators.

## What exactly is hashed: tests that change only the database string (single-track rekordbox stick)

Same stick as the name-part tests (real name `P049/00010C89/ANLZ0000.DAT`). Only the audio path string in `export.pdb` (string 20, both copies of the row)
was changed; nothing on the stick moved. FAT is case-insensitive, so the deck can still open the audio.

| Database string                                          | Audio | Analysis (waveform, cues) |
|----------------------------------------------------------|-------|---------------------------|
| `/Contents/Intapol/Afrovox EP/01_afrovox.mp3` (original) | plays | found                     |
| `/ContentS/Intapol/Afrovox EP/01_afrovox.mp3`            | plays | **not found**             |
| `/Contents\Intapol/Afrovox EP/01_afrovox.mp3`            | plays | **not found**             |

So the input of the hash includes the leading `/Contents/` (its case and the separators), and it is taken from the **database string**, not from the path the deck
actually opened (FAT would have accepted `ContentS`). Combined with the earlier results the input is the whole exported path string
`/Contents/<artist folder>/<album folder>/<file name>.<extension>`, case sensitive, and the deck rebuilds `P???/????????` from it.

## `PIONEER/USBANLZ/USBMNG.DAT` is a hash table of 200003 slots

`USBMNG.DAT` is 400058 bytes: a `PMNG` header (0x20 bytes), a `PTBL` section (0x14 bytes, containing the value `0x00030d43` = **200003**) and 200003 two-byte slots.
So the 200003 in the second folder number is the size of this table, and the second number is very probably the slot of the track (`hash mod 200003`).

* The table body is all zeros in every export, rekordbox's own included (also on the 2265 track KINGSTON drive).
* The deck writes to the stick: after the first use of a stick it set byte 31 of this file (the last byte of the `PMNG` header) from 0 to 1 and bumped a sequence
  byte in `exportExt.pdb`. Copy those two files back from the original export before repeating a test.

## Can the exact weights be recovered from single-letter variants? (no)

With `Y_k = ((H_a + k*w) mod 2^32) mod 200003` for several letter offsets `k` (letter code minus 97) at one position, the data gives `w mod 200003` exactly (up
to the wrap carry) and the wrap counts `floor((H_a + k*w) / 2^32)`. `usbanalz-data/weight_recovery_simulation.py` simulates random weights and solves
for them: with offsets {1, 2, 25, -49, -7} each weight is still ambiguous among ~100 to 440 values, and 770 base values survive. Even eleven offsets only reach
~20 to 400 candidates, because a wrap count cannot say more than `2^32 / (|k| * 200003)`. So to get the exact 32-bit weights, paths that change **many positions at once** are
needed (each gives a linear equation over all weights, an equation that is worth ~17 bits), and the system has to be solved as a lattice problem
(`sum k_i * w_i = Y + 200003*q (mod 2^32)`, with small unknown `q`). About 140 weights x 32 bits / 17 bits is a minimum of ~260 such paths, realistically ~500.

## The number in the file name is a collision counter, and P is a function of Y

In the big real exports (KINGSTON, 2265 tracks; `cdj64`; `rekordbox-clone-large`) there are **21 folders that hold two different tracks**, the second one with
`ANLZ0001.DAT` instead of `ANLZ0000.DAT` (e.g. `P052/00010F0E`: `Metallic Rain/Jacked_On_Faith_Rock_The_Beat.mp3` is `ANLZ0000`, `...Edition 2 (Mixed By/Bass.mp3` is `ANLZ0001`).
So the folder `P???/????????` is the *bucket* of a hash table and the file name number tells the entries of a bucket apart. That is why the deck only needs
the file name from the database: it rebuilds the bucket itself.

The same data says something about the first number:

* The slots shared by more than one track are 21 (about 12.8 are expected for a uniform 200003 slot table).
* In **all 21** of them the first number `P` is also the same. If `P` were an independent 7 bit value this would have a probability of about (1/128)^21.
  **So `P` is a function of the slot `Y`** (or of whatever `Y` is computed from), not an independent second hash.
* The function is smooth and wavy, not random: tracks with neighbouring slots have neighbouring `P` (Y 2146 and 2147 give 4 and 5, Y 3037 and 3039 give 31 and 31), and the top
  bits of `P` follow wide ranges of `Y`: `P` 96..127 only occurs for `Y` between about 74000 and 131000, `P` 64..95 for 65000..200000, `P` 32..63 from about 8000 and `P` 0..31 anywhere
  (see `usbanalz-data/` for the pair list; the text table was produced from the KINGSTON drive).
* A multiplicative hash of the slot, `((Y * K) mod 2^32) >> s`, is the natural candidate (results of the search for `K` are in the next section).

## SOLVED: the first number is seven bits of the second

Solving "which bits of `Y` is each bit of `P`" over GF(2) with all 2244 slots of the KINGSTON drive gave exactly one bit each, with 100% agreement:

| `P` bit | 0 | 1 | 2 | 3 | 4 | 5  | 6  |
|---------|---|---|---|---|---|----|----|
| `Y` bit | 0 | 2 | 6 | 7 | 9 | 13 | 16 |

`P = Y[0] | Y[2]<<1 | Y[6]<<2 | Y[7]<<3 | Y[9]<<4 | Y[13]<<5 | Y[16]<<6` (`usbanalz-data/p_from_y.py`). Checked on **7401 (path, folder) pairs** (2380 USB export pairs, the 2244
KINGSTON slots, `export3`, `export4` and the 2439 folders of the PC library): **0 mismatches.** `P049/00010C89` and `P025/00022051` follow it.

So the folder is `P<bits of Y>/<Y as 8 hex digits>/ANLZ<collision counter>.DAT`, and the only unknown left is **`Y = H(path) mod 200003`**, where `H` is
the 32-bit hash of the path string described above (linear modulo 2^32, weights anchored at the end, not a plain geometric / arithmetic / polynomial / periodic sequence).

## What the deck writes to the stick (`USBMNG.DAT`, sequence numbers)

Baseline for the single-track rekordbox stick: `USBMNG.DAT` header word at offset 28 = 0, `export.pdb` sequence (offset 20) = 17, `exportExt.pdb` sequence = 6.

| Test                                                                  | Result on the stick                 |
|-----------------------------------------------------------------------|-------------------------------------|
| successful load + play (original export)                              | sequences +2 (19 / 8), flag stays 0 |
| `/ContentS/` (failing lookup), played from the start (first two runs) | sequences +2, **flag 1**            |
| `/Contents\\` (failing lookup), played from the start                 | sequences +2, **flag 1**            |
| `/ContentS/`, loaded twice, short                                     | sequences +4 (21 / 10), flag 0      |
| `/ContentS/`, loaded once, short                                      | sequences +2, flag 0                |

* The two database sequence numbers go up by 2 for every load of a track, successful or not.
* A failed lookup does **not** by itself set the `USBMNG.DAT` flag. It was set in the first two failing runs and not in the later short ones. The difference between them
  is probably how long the track was played, so the hypothesis is that the flag means "the deck analysed a track itself" (when the stored analysis is not found, the deck builds the waveform
  while the track plays). Test: load once and let it play to the end.
* The 200003 slot table in `USBMNG.DAT` stays all zero in every case.

## The deck logs plays in `USBMNG.DAT` at the hash slot: an oracle for the hash

After the failing `/ContentS/Intapol/Afrovox EP/01_afrovox.mp3` stick was played right through (baseline: all 200003 slots zero), the deck had written **one non-zero slot** in the table of
`USBMNG.DAT`: slot **63953** (`0xF9D1`) with the 16-bit value 8 (big endian `00 08`), and had appended entries to the play-history tables of `export.pdb`.

63953 is not the slot of the original path (68745 = 0x10C89). It is the slot of the **changed** database string: the change `s` -> `S` at distance 35 from the end shifts the slot by
-32 x (the weight of that position) and the weight measured for that position in `export4` (position 109) reproduces it (`-32*w - k*K`, carry bit 0, k = 9, mod 200003).

So the deck computes the same hash for any string in the database and records the track as played at that slot. That is a hash **oracle that does not need rekordbox**: put any
string (that still opens the audio file, e.g. a different spelling of the case or the separators) into the track row, play the track, and read the slot from `USBMNG.DAT`.
Open questions: how long the track has to be played before the entry is written (2 short loads wrote nothing, a full play did), and what the value 8 means.

## When does the deck log a play? (`export7`) and the plan to recover the exact weights

* `export7` (10, 20, 30 and 60 second tracks, tone and music, all with their analysis found): the deck logged a play only for the two **60 second** tracks
  (`history entries: 2` in the history tables, one history session), not for the 10, 20 and 30 second ones; the content did not matter. A one second clip is never logged either (`export6`).
  The `USBMNG.DAT` table stayed **empty**: the slot write seen earlier happened for a track whose analysis was *not* found, so it is probably a "this track still needs analysing" record
  (guess). Using the deck as an oracle therefore costs about a minute of play for each string; collecting the data from rekordbox exports (no deck needed) is far cheaper.
* Plan to get the exact weights `w[p]`: write the unknown weight as `w[p] = u[p] + 200003 * t[p]` with `u[p] = w[p] mod 200003` (obtainable by linear algebra modulo the prime 200003 once the wrap counts
  are known) and `t[p]` in 0..21474. Each (path, slot) pair then gives `R <= sum(c[k] * t[k]) <= R + 21474` (a window as wide as the whole range of the unknown wrap), so a few thousand such windows
  are needed to pin the `t[p]` (set-membership estimation by linear programming; `usbanalz-data/feasibility_lp_simulation.py` tests how many). `export8` (500 random file names) is the data set for this.
* Tools used for the experiments (not part of the repository): `fpylll` (lattice reduction) and `scipy` installed with `pip install --target` into a scratch folder.

## Random file names (`export8`), "are the weights a known table?", and why exact recovery from wrap counts does not scale

* `export8` (`usbanalz-data/export8-random-500.json`): 500 random paths (letters, capitals, digits and spaces; lengths 26..147). Rekordbox wrote every path exactly as generated, no two share a slot,
  and the partition rule `P = Y[0,2,6,7,9,13,16]` holds for **500 of 500** (7901 pairs in total, all matching). This export has **no** `USBMNG.DAT` at all (126 partition folders), so that file is optional.
* `USBMNG.DAT` really is empty: in all five rekordbox exports (identical files) the only non-zero bytes are the 21 bytes of the header (tag names, lengths, 200003); the 400006 byte table is zero.
* The weights were compared with well-known sources (`usbanalz-data/known_weight_tables_test.py`): glibc `rand()`, `mt19937` (default seed 5489, 1, 0), minstd, ANSI C / Numerical Recipes / MSVC / Borland / Delphi LCGs, xorshift32, PCG32,
  the digits of pi, e, ln 2, sqrt 2, sqrt 3, phi, cbrt 2/3, the MD5 sine table, the SHA-256 constants, `sin(n^2)`, primes, cubes, and `hash(position)` for md5/sha1/sha256/crc32/adler/fnv/djb2 of seven encodings of the position; also
  the Pro DJ Link magic `Qspt1WmJOL` (and its 4 byte pieces, both byte orders) as seeds and as LCG constants (697 sequences). Test: the weight at distance `d` from the end must satisfy the measured residue and doubling
  conditions at the 44+ stem positions of `export4`; a true source would hit nearly all, chance is about 1e-5 per position. **No match** (one coincidence of 2 positions in ~560000 alignments).
* **Exact weights from wrap counts do not scale.** With the residues `u[p] = w[p] mod 200003` known, each pair gives only the wrap count, i.e. a window of width 21475 for `sum(c*t)`. Set-membership (linear programming) simulations
  with the real parameters (`feasibility_lp_simulation*.py`): 1000 / 3000 / 6000 random strings narrow the weight uncertainty from 21475 to a median of 247 / 89 / 38; starting from narrow windows (width 120 to 250, which large-offset single letter probes can give)
  and adding 3000 strings still leaves a median of ~60, none of the 150 weights fully determined. The information per equation falls as the uncertainty falls, so exact weights would need of the order of 10^5 strings.
  The slot reveals `H mod 200003` only; the missing information is `H div 200003`.

## What a look at `rekordbox.exe` (5.0 era, PE32, 50 MB, dated 2017) as plain data shows

Only reading the file as text and as data (no disassembly, nothing run), after the user decided that was acceptable.

* The folder name is built by the format string **`P%03X%s%08X`** (partition as three hex digits, a separator, the slot as eight hex digits). The neighbouring strings are `PIONEER/USBANLZ`, `/ANLZ`, `ANLZ0000.DAT` and `ANLZ0000.EXT`.
  No string names a hash algorithm or library for it (searched crc, hash, murmur, fnv, adler, md5, sha, jenkins, city, xxhash, siphash, checksum, digest, lookup3, spooky, farm in 8 bit and 16 bit text).
* A user interface column list contains the heading **`File Path Hash`** (next to `Original ID`, `File Path`, `Album`, `File Name`), and the database index names include `HashMasContent`, `HashMasArtist`, ... So the library
  database may store a hash of each track's file path in a column. If it holds the same value that names the analysis folder it would be the exact hash (not only `hash mod 200003`) for every track of the local library
  (2439 tracks in `rb-example`), which would let the weights be solved exactly by linear algebra modulo 2^32. **Not checked yet**: the library databases are Microsoft ESE files (`datafile.edb`, `ExtData.edb`).
* A scan of the whole file for a table of 32-bit constants (any alignment, 32 or 64 bit entries, either direction) whose entries fit the 140 measured weight residues found nothing (about the number of single
  coincidences expected by chance, no run of three). So the weights are not a plain constant table in this executable (they may be computed, or stored in another form or another file).
* The text `ICE CREAMSHAVED ICEICE CREAMDOUGHNUTCOOKIECHOCOLATE` (an emoji / icon name list, apparently) is not a source of the weights (857 ways of using it tested).


---

# STATE FOR CONTINUATION (read this first after a restart)

**Goal:** make trakcrate's exporter write `PIONEER/USBANLZ/P<partition>/<slot>/ANLZ<n>.DAT|EXT` where the deck finds it. The deck rebuilds the folder from the path, so the exporter needs the slot function.

**Solved:** folder format `P%03X/%08X/ANLZ%04X` (the executable has the format string `P%03X%s%08X`); partition = slot bits {0,2,6,7,9,13,16} (`p_from_y.py`, verified on 7901 pairs);
`ANLZ<n>`: n is a collision counter (21 real collisions); slot = `H mod 200003` with `H` a 32 bit wrapping sum linear in the characters of the full path string `/Contents/<artist>/<album>/<file>.<ext>`
(case, separators and extension all count; tags, title, audio, playlist, track id do not); weights depend only on the distance from the END of the string.

**Unknown:** the 150 or so weights `w[d]` (their residues mod 200003 are measured for d = 5..146 in `export4-positions-140.json`, up to a carry of 102874 per position).

**Ruled out (do not repeat):** geometric / arithmetic / polynomial(<=7) / periodic(<=99) weights; geometric at strides 1..4; two-base sums (all odd bases <=4097 and ~40 big ones); partial sums, `(p+1)B^p`;
second-order integer recurrences with small coefficients (partial: no hit before the session ended); CRC-32 (XOR-linear, we need additive); named hashes (djb2, FNV, sdbm, murmur, Jenkins, PJW, ...);
LCG relations for ~33 known constant pairs; weights from glibc rand / mt19937 / minstd / xorshift / PCG / digits of pi, e, sqrt / MD5 sine table / SHA constants / hash(position) / the Pro DJ Link magic / the
"ICE CREAM..." string; a table of 32 bit words in `rekordbox.exe` (strides 1..32, every alignment, two scans). Exact recovery by wrap-count set-membership does NOT scale (needs ~10^5 strings).

**Best remaining leads (in order):**
1. The executable has a column heading **`File Path Hash`** and db index names `HashMas*`. The local library database (`rb-example/rekordbox/datafile.edb`, `ExtData.edb`, Microsoft ESE) may store the exact hash of each track's file path. If that value is the
   same hash (not only mod 200003) then 2439 tracks give 2439 exact equations modulo 2^32 and the weights follow by linear algebra. Needs an ESE reader (`pip install dissect.esedb`, was blocked by the auto-mode check together with a file copy; ask the user) and the share
   `//dirtbox/transfer` (was returning I/O errors; rekordbox may lock the files).
2. `export10` (48/48/20 letters; generate with `gen_probe_set.py OUT 48 48 20`, 233 clips) tests whether one end-anchored sum covers artist and album letters too (compare with `export4` at equal distance from the end).
3. `export9-pilot` (3000 case-safe clips, on the share in `rb-experiment/export9-pilot`): importing a big folder into rekordbox writes the analysis folders to the local library without an export; a way to collect 10^5 pairs overnight.
4. Ask the rekordbox format community with this write-up.

**Tools (not in the repo):** `fpylll` and `scipy` were installed with `pip install --target <scratch>`; reinstall if needed (`python3 -m venv` is not available: `python3-venv` missing).

## SOLVED (found in rekordbox.exe, pasted by the user)

The slot is `H mod 200003` where, over the UTF-16 code units `c` of the path (`/Contents/...`), starting from `h = 0`:

    h = h * 0x34f5501d + c * 0x93b6      (wrapping 32 bit)

`usbanalz-data/horner_test.py` reproduces 871 of 871 real path/slot pairs (the one other key in the data is not a path).
 The partition is the seven slot bits listed above; `ANLZ%04X` counts tracks sharing a slot.
Implemented in `src/trakcrate/pdb_build.cpp`. The earlier sections of this file describe the search that led here and are history.

## Hashing Magic Numbers

- 200003 - It's prime (0x30D43): the first prime above 200,000. 
- K = 0x34F5501D is 888,492,061, which is 23497 × 37813. Both factors are prime. 
- W = 0x93B6 is 37,814, which is 37813 + 1. So K and W share 37813, with W one more than a factor of K. This could be a coincidence, since 37813 is a fairly small number. It could also mean the constants were generated together.
