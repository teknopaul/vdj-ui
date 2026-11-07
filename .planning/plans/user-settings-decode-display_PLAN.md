# User settings: decode DEVSETTING.DAT and MYSETTING.DAT in the viewer

## Goal

The Settings viewer currently renders the two `*SETTING.DAT` payloads as one grey
`"not decoded"` row per byte. Decode them into the rekordbox preferences they actually
hold (on air display, quantize, tempo range, waveform colour, ...), while keeping every
byte whose meaning is unknown visible as a grey row.

The decode is already known: `rekordbox/src/setting.rs` defines the container and every
option code. This plan ports that knowledge into
`src/trakcrate/settings_view.cpp`, with no new files and no Qt changes.

Scope:

- `PIONEER/DEVSETTING.DAT` (140 bytes, 32-byte data section)
- `PIONEER/MYSETTING.DAT` (148 bytes, 40-byte data section)

Out of scope: `MYSETTING2.DAT`, `DJMMYSETTING.DAT`, editing values, rewriting CRCs from
the viewer. The table design should make those files easy to add later, but do not add
them now.

## Ground truth to read before starting

Read these files rather than relying on this plan alone:

- `/home/teknopaul/github_workspace/rekordcrate/src/setting.rs`
  - `Setting` (around lines 40-99): container layout.
  - `SettingType::expected_data_size` (around lines 204-214): `0x20` / `0x28`.
  - `SettingData`, `DevSetting`, `MySetting` (around lines 216-541): field order.
  - Every enum below (around lines 543-1442): the option codes.
- `/home/teknopaul/github_workspace/rekordcrate/src/device.rs` (lines 91-253):
  which fields belong to which file. It confirms that `DEVSETTING.DAT` holds four
  display settings and `MYSETTING.DAT` holds the player/mixer preferences.
- `src/trakcrate/settings_view.cpp`, especially:
  - `describeSetting` (currently around lines 67-92), the function being rewritten.
  - `describeSettingsFile` (around lines 148-160), unchanged.
  - `diffSettingsFiles` (around lines 162-185), unchanged but improves automatically.
- `src/trakcrate/settings_files.cpp` `verifySettingsFile` / `crcOffset`: do not change
  them. Reuse their error text if useful.

Do not consult unrelated plans or documentation while implementing this. Other work may
be in flight in the tree: touch only the files named in the phases below.

## Current behaviour to replace

`describeSetting(kind, b, rows)` currently:

1. emits `file check` from `verifySettingsFile`;
2. emits `header length`, `brand`, `program`, `version`, `payload length`, `magic`,
   `crc`, `block id`;
3. loops from payload offset 4 to `plen - 4` and emits
   `{"payload 0xNN", "0xNN", "not decoded", false}` for every byte.

The `payload 0xNN` labels are relative to file offset `0x6c`, so
`payload 0x04` is really file offset `0x70`. That is confusing next to the hex dump,
which uses absolute offsets. Any remaining raw row must use absolute file offsets.

## Container layout (already parsed correctly)

All integers little endian.

| Offset  | Size | Meaning                                                  |
|---------|------|----------------------------------------------------------|
| `0x00`  | 4    | header length, always `0x60`                             |
| `0x04`  | 32   | brand (`PIONEER DJ` / `PIONEER`)                         |
| `0x24`  | 32   | software (`rekordbox`)                                   |
| `0x44`  | 32   | version (`4.5.0` / `0.001`)                              |
| `0x64`  | 4    | data-section length: `0x20` DEVSETTING, `0x28` MYSETTING |
| `0x68`  | n    | data section; first 4 bytes are magic `0x12345678`       |
| `len-4` | 2    | CRC-16/XMODEM over `[0x68, len-4)`                       |
| `len-2` | 2    | zero padding                                             |

The data section includes the magic. The C++ code's
`crcAt = 0x6c + plen - 4` is equivalent to `0x68 + plen`, so that arithmetic is correct
and must keep working.

## Decode table: DEVSETTING.DAT

32-byte data section at `0x68`-`0x87`. File is 140 bytes.

| File offset   | Field name for the viewer   | Codes                                      |
|---------------|-----------------------------|--------------------------------------------|
| `0x68`        | `magic`                     | `0x12345678` (already shown)               |
| `0x6c`        | `block id`                  | `1` (already shown)                        |
| `0x70`        | `unknown 0x70`              | `0x01` in samples                          |
| `0x71`        | `Overview Waveform Type`    | `0x01` Half Waveform, `0x02` Full Waveform |
| `0x72`        | `Waveform Color`            | `0x01` Blue, `0x03` RGB, `0x04` 3Band      |
| `0x73`        | `unknown 0x73`              | `0x01` in samples                          |
| `0x74`        | `Key Display Format`        | `0x01` Classic, `0x02` Alphanumeric        |
| `0x75`        | `Waveform Current Position` | `0x01` Center, `0x02` Left                 |
| `0x76`-`0x87` | `unknown 0x76-0x87`         | 18 zero bytes                              |
| `0x88`        | `crc`                       | already shown                              |

### Caveat: the shipped DEVSETTING is an old 4.5.0 file

`data/trakcrate/user-settings/DEVSETTING.DAT` (the compiled-in default) has:

- `0x70`-`0x73` = `01 01 01 01`
- `0x74` = `00` (not `0x01`)
- `0x75` = `00` (not `0x01`)

The rekordcrate fixtures from rekordbox 6.6.1 have `0x01` at `0x74` and `0x75`.
Therefore:

- `0x00` must be shown as `unknown option 0x00` and marked `known == false`;
- do not silently treat `0x00` as Classic / Center;
- use a note like `0x00; rekordbox 6 writes 0x01 here` so the difference is visible.

Expected decoded output for the shipped file:

- Overview Waveform Type = `Half Waveform`
- Waveform Color = `Blue`
- Key Display Format = `unknown option 0x00` (grey)
- Waveform Current Position = `unknown option 0x00` (grey)
- unknowns at `0x70`, `0x73`, `0x76`-`0x87`
- CRC `0x3bba`

## Decode table: MYSETTING.DAT

40-byte data section at `0x68`-`0x8f`. File is 148 bytes.

| File offset   | Field name for the viewer    | Codes                                                                                                                                                                                                                                                                                |
|---------------|------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `0x68`        | `magic`                      | `0x12345678` (already shown)                                                                                                                                                                                                                                                         |
| `0x6c`        | `block id`                   | `2` (already shown)                                                                                                                                                                                                                                                                  |
| `0x70`        | `On Air Display`             | `0x80` Off, `0x81` On                                                                                                                                                                                                                                                                |
| `0x71`        | `LCD Brightness`             | `0x81` 1, `0x82` 2, `0x83` 3, `0x84` 4, `0x85` 5                                                                                                                                                                                                                                     |
| `0x72`        | `Quantize`                   | `0x80` Off, `0x81` On                                                                                                                                                                                                                                                                |
| `0x73`        | `Auto Cue Level`             | `0x80` -36dB, `0x81` -42dB, `0x82` -48dB, `0x83` -54dB, `0x84` -60dB, `0x85` -66dB, `0x86` -72dB, `0x87` -78dB, `0x88` Memory                                                                                                                                                        |
| `0x74`        | `Language`                   | `0x81` English, `0x82` Français, `0x83` Deutsch, `0x84` Italiano, `0x85` Nederlands, `0x86` Español, `0x87` Русский, `0x88` 한국어, `0x89` 简体中文, `0x8A` 繁體中文, `0x8B` 日本語, `0x8C` Português, `0x8D` Svenska, `0x8E` Čeština, `0x8F` Magyar, `0x90` Dansk, `0x91` Ελληνικά, `0x92` Türkçe |
| `0x75`        | `unknown 0x75`               | `0x01` in samples                                                                                                                                                                                                                                                                    |
| `0x76`        | `Jog Ring Brightness`        | `0x80` Off, `0x81` 1 (Dark), `0x82` 2 (Bright)                                                                                                                                                                                                                                       |
| `0x77`        | `Jog Ring Indicator`         | `0x80` Off, `0x81` On                                                                                                                                                                                                                                                                |
| `0x78`        | `Slip Flashing`              | `0x80` Off, `0x81` On                                                                                                                                                                                                                                                                |
| `0x79`-`0x7B` | `unknown 0x79-0x7B`          | `01 01 01` in samples                                                                                                                                                                                                                                                                |
| `0x7C`        | `Disc Slot Illumination`     | `0x80` Off, `0x81` 1 (Dark), `0x82` 2 (Bright)                                                                                                                                                                                                                                       |
| `0x7D`        | `Eject/Load Lock`            | `0x80` Unlock, `0x81` Lock                                                                                                                                                                                                                                                           |
| `0x7E`        | `Sync`                       | `0x80` Off, `0x81` On                                                                                                                                                                                                                                                                |
| `0x7F`        | `Play Mode / Auto Play Mode` | `0x80` Continue / On, `0x81` Single / Off                                                                                                                                                                                                                                            |
| `0x80`        | `Quantize Beat Value`        | `0x80` 1 Beat, `0x81` 1/2 Beat, `0x82` 1/4 Beat, `0x83` 1/8 Beat                                                                                                                                                                                                                     |
| `0x81`        | `Hot Cue Auto Load`          | `0x80` Off, `0x81` On, `0x82` rekordbox SETTING                                                                                                                                                                                                                                      |
| `0x82`        | `Hot Cue Color`              | `0x80` Off, `0x81` On                                                                                                                                                                                                                                                                |
| `0x83`-`0x84` | `unknown 0x83-0x84`          | `00 00`                                                                                                                                                                                                                                                                              |
| `0x85`        | `Needle Lock`                | `0x80` Unlock, `0x81` Lock                                                                                                                                                                                                                                                           |
| `0x86`-`0x87` | `unknown 0x86-0x87`          | `00 00`                                                                                                                                                                                                                                                                              |
| `0x88`        | `Time Mode`                  | `0x80` Elapsed, `0x81` Remain                                                                                                                                                                                                                                                        |
| `0x89`        | `Jog Mode`                   | `0x80` CDJ, `0x81` Vinyl                                                                                                                                                                                                                                                             |
| `0x8A`        | `Auto Cue`                   | `0x80` Off, `0x81` On                                                                                                                                                                                                                                                                |
| `0x8B`        | `Master Tempo`               | `0x80` Off, `0x81` On                                                                                                                                                                                                                                                                |
| `0x8C`        | `Tempo Range`                | `0x80` ±6%, `0x81` ±10%, `0x82` ±16%, `0x83` Wide                                                                                                                                                                                                                                    |
| `0x8D`        | `Phase Meter`                | `0x80` Type 1, `0x81` Type 2                                                                                                                                                                                                                                                         |
| `0x8E`-`0x8F` | `unknown 0x8E-0x8F`          | `00 00`                                                                                                                                                                                                                                                                              |
| `0x90`        | `crc`                        | already shown                                                                                                                                                                                                                                                                        |

### Expected decoded output for the shipped MYSETTING

`data/trakcrate/user-settings/MYSETTING.DAT` currently renders as 28 "not decoded" bytes. It must render as:

| Field                      | Value             |
|----------------------------|-------------------|
| On Air Display             | On                |
| LCD Brightness             | 3                 |
| Quantize                   | On                |
| Auto Cue Level             | -60dB             |
| Language                   | English           |
| Jog Ring Brightness        | 2 (Bright)        |
| Jog Ring Indicator         | On                |
| Slip Flashing              | On                |
| Disc Slot Illumination     | 2 (Bright)        |
| Eject/Load Lock            | Unlock            |
| Sync                       | Off               |
| Play Mode / Auto Play Mode | Continue / On     |
| Quantize Beat Value        | 1 Beat            |
| Hot Cue Auto Load          | rekordbox SETTING |
| Hot Cue Color              | Off               |
| Needle Lock                | Lock              |
| Time Mode                  | Remain            |
| Jog Mode                   | CDJ               |
| Auto Cue                   | On                |
| Master Tempo               | On                |
| Tempo Range                | ±6%               |
| Phase Meter                | Type 1            |
| unknown 0x75               | `0x01`            |
| unknown 0x79-0x7B          | `01 01 01`        |
| unknown 0x83-0x84          | `00 00`           |
| unknown 0x86-0x87          | `00 00`           |
| unknown 0x8E-0x8F          | `00 00`           |
| crc                        | `0x2181`          |

---

## Design

All changes are in `src/trakcrate/settings_view.cpp`. `settings_view.h` needs no API
change; if a comment is added, it is only to say that `known == false` also covers an
option code outside the known table.

### Decode tables

Add three file-local types above `describeSetting`:

```cpp
struct SettingOption { uint8_t code; const char* label; };
struct SettingField  { size_t at; const char* name;
                       const SettingOption* options; size_t count; };
struct UnknownSpan   { size_t at; size_t len; const char* note; };
```

Then add, as static data:

- shared option arrays for the repeated patterns:
  - Off/On (`0x80`/`0x81`)
  - Unlock/Lock (`0x80`/`0x81`)
  - brightness 1-5 (`0x81`-`0x85`)
  - brightness Off / 1 (Dark) / 2 (Bright) (`0x80`-`0x82`)
  - the language table (`0x81`-`0x92`)
- `kDevFields[]` and `kDevUnknown[]`
- `kMyFields[]` and `kMyUnknown[]`

Absolute file offsets are used throughout. Field names must be exactly the strings in
the decoded-output tables above, because `diffSettingsFiles` compares by field name.

### Option lookup

Add:

```cpp
static std::string optionLabel(const SettingField& f, uint8_t v, bool* known)
```

- Return the mapped label and set `*known = true`.
- Return `"unknown option 0xNN"` and set `*known = false` when the byte is not in the
  table (two lowercase hex digits).

This is the only place that decides what a code means. Do not add per-field switches.

### Rewriting `describeSetting`

Keep the structural rows exactly as they are:

- `file check` from `verifySettingsFile`
- `header length`, `brand`, `program`, `version`, `payload length`, `magic`
- `block id`
- `crc`

Replace the raw payload loop with:

1. **Shape check.** If the file is too short to hold the data section, or
   `le32(b, 0) != 0x60`, or `le32(b, 0x68) != 0x12345678`, or the payload length is not
   the value expected for this kind (`0x20` dev / `0x28` my), or
   `b.size() != 0x68 + plen + 4`, push one row
   `{"settings", "", "<reason>", false}` and stop decoding. The function must still
   never crash and must still return the structural rows it managed to read.
2. **Known fields.** For each field in the table for this kind:
   - read one byte at `field.at` after a bounds check;
   - look it up with `optionLabel`;
   - push `{field.name, label, "0xNN", known}` where `NN` is the raw byte.
3. **Unknown spans.** For each span, push one row:
   - field = e.g. `"unknown 0x79-0x7B"` (absolute range);
   - value = `hexBytes(b, span.at, span.len)`;
   - note = `span.note`;
   - `known = false`.
4. Do **not** emit a row per understood byte any more. The viewer's hex dump already
   shows every byte.

Decode values even when the CRC is wrong, as long as the shape is right. `file check`
already reports `damaged`; the values may still be useful.

### `diffSettingsFiles`

No code change. It maps rows by `field` and compares `value`, so improved names make
diffs meaningful automatically: changing `0x70` from `0x81` to `0x80` becomes
`On Air Display: On -> Off` instead of `payload 0x04: 0x81 -> 0x80`. Keep the existing
`raw bytes` row at the end.

### Qt viewer

No functional change is needed in `src/ui/trakcrate_settings_viewer.cpp`. It already
renders `known == false` rows grey and the status line already says grey rows are not
understood. Do not edit it unless a test reveals a display bug.

---

## Phase 1: decoder skeleton and DEVSETTING

Files: `src/trakcrate/settings_view.cpp`, `tests/test_trakcrate_settings.cpp`.

1. Add `SettingOption`, `SettingField`, `UnknownSpan`, the shared option arrays, the
   DEVSETTING field/unknown tables, and `optionLabel`.
2. Add the shape check and rewrite `describeSetting` as described above. It will look up
   the table by `kind`; MYSETTING can temporarily fall through to the old raw-byte loop
   only if needed to keep the build green, but prefer adding the empty MYSETTING tables
   in phase 2 immediately after.
3. Update `describeShippedFiles` in `tests/test_trakcrate_settings.cpp`:
   - delete the `"payload 0x04"` assertions;
   - assert `Overview Waveform Type == Half Waveform`;
   - assert `Waveform Color == Blue`;
   - find `Key Display Format` and `Waveform Current Position`, assert their value is
     `unknown option 0x00` and `known == false`;
   - assert the unknown spans exist and are `known == false`;
   - keep the CRC assertion `0x3bba`.
4. Build and run the settings test.

Acceptance: the DEVSETTING view shows four application rows and grey unknown spans; no
crashes on short/garbage input.

## Phase 2: MYSETTING table and shipped sample

Files: `src/trakcrate/settings_view.cpp`, `tests/test_trakcrate_settings.cpp`.

1. Add `kMyFields[]` and `kMyUnknown[]` with every offset and label from the MYSETTING
   table above. Reuse the shared option arrays.
2. Ensure `optionLabel` handles all codes, including the 18 language values.
3. Extend `describeShippedFiles`:
   - assert the full decoded shipped-sample table from this plan (at least On Air
     Display, LCD Brightness, Auto Cue Level, Language, Play Mode, Quantize Beat Value,
     Hot Cue Auto Load, Needle Lock, Tempo Range, Phase Meter);
   - assert `unknown 0x75`, `unknown 0x79-0x7B`, `unknown 0x83-0x84`,
     `unknown 0x86-0x87`, `unknown 0x8E-0x8F` are present and grey;
   - keep CRC `0x2181` and `block id == 2`.
4. Add a data-driven test that writes each known code into a copy of
   `defaultMySetting()` / `defaultDevSetting()`, calls `fixSettingsCrc`, and checks the
   label. Cover:
   - every code of every MYSETTING field (including all languages);
   - every code of every DEVSETTING field;
   - one unmapped code (e.g. `0x99`) in one field, asserting
     `unknown option 0x99` and `known == false`.
5. Build and run the settings test.

Acceptance: the shipped MYSETTING renders exactly as the table in this plan.

## Phase 3: robustness, diff test and damaged files

Files: `src/trakcrate/settings_view.cpp`, `tests/test_trakcrate_settings.cpp`.

1. Review the shape check for every short input. `describeSettingsFile` must be safe for
   the existing garbage cases: empty string, 1 byte, 200 bytes of `0xff`, 300 zero
   bytes. No reads outside the string.
2. Test a wrong payload length (e.g. copy a MYSETTING into a DEVSETTING kind): one
   `settings` row with a reason, no decoded rows, no crash.
3. Test a CRC-damaged but structurally valid file: `file check == damaged`, values are
   still decoded, CRC row says it does not match.
4. Update `diffFindsTheChangedByte`:
   - set byte `0x70` to `0x80` (On Air Display On -> Off), fix the CRC;
   - assert the diff contains a row `On Air Display` with `b == "Off"`, plus `crc` and
     `raw bytes`;
   - keep the existing djprofile diff assertion unchanged.
5. Build and run the settings test plus the full test suite.

Acceptance: all settings tests pass; no unrelated tests regress.

## Phase 4: documentation

Files: `doc/trakcrate/settings-files.md`, `doc/trakcrate/gui.md`,
`doc/trakcrate/UNKNOWN_BYTES.md`.

These files currently say the MYSETTING bytes are not decoded. Correct them:

1. `settings-files.md`: replace the "Which byte is which setting is not decoded" text in
   section "MYSETTING.DAT" with the location of the decode tables
   (`settings_view.cpp`) and a short list of what the bytes mean. Note the old 4.5.0
   DEVSETTING has `0x00` at `0x74`/`0x75` where rekordbox 6 writes `0x01`.
2. `gui.md`: change the viewer bullet from "each payload byte ... not decoded" to
   "the decoded player preferences" and keep the statement that grey rows are unknown.
3. `UNKNOWN_BYTES.md`: update the line saying the payload bytes are not decoded; list
   the remaining unknown fields (`0x70`, `0x73`, `0x76`-`0x87` in DEVSETTING; `0x75`,
   `0x79`-`0x7B`, `0x83`-`0x84`, `0x86`-`0x87`, `0x8E`-`0x8F` in MYSETTING).

Acceptance: docs no longer claim the settings bytes are undecoded.

---

## Verification commands

Use the existing build directory; do not create a new one:

```sh
cd build
cmake ..
make -j"$(nproc)"
./tests/vdj-ui-trakcrate-settings-test
ctest -R vdj-ui-trakcrate-settings-test --output-on-failure
```

Then run the full suite if the build is otherwise healthy:

```sh
ctest --output-on-failure
```

## Definition of done

- `describeSettingsFile(SettingsKind::DevSetting, defaultDevSetting())` returns the four
  application rows plus grey unknown spans, no per-byte "not decoded" rows.
- `describeSettingsFile(SettingsKind::MySetting, defaultMySetting())` returns the 22
  decoded preference rows plus the six unknown spans.
- Unknown option codes render as `unknown option 0xNN` with `known == false`.
- Malformed and truncated files still produce a non-empty row list and never crash.
- `diffSettingsFiles` reports changed settings by name.
- The settings test and the full `ctest` pass.
- Only `src/trakcrate/settings_view.cpp`, `tests/test_trakcrate_settings.cpp` and the
  three documentation files are changed. No other source file, build file or another
  agent's work is touched.
