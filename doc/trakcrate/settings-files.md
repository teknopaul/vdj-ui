# Settings and profile files (study)

Source: `references/libcdj/rekordbox/PIONEER/` (the 7-track export). The big pendrive `/media/teknopaul/TEKNOPAUL64` was not mounted when this was written, so there is only one sample of each file; plug it in and `cmp` to see which bytes are per-drive.

## Verdict

Copying them verbatim once from a real pendrive is reasonable and the safe default. We also know enough to generate the two `*SETTING.DAT` files, so we can stamp out settings later if wanted. `djprofile.nxs` and `RBFLTR.DAT` are different: see below.

## Decision: always write all five, from a settings set (implemented)

A pendrive missing these files made the real decks complain and crash, so none of them is optional. The five files of a real export (`DEVSETTING.DAT`, `MYSETTING.DAT`, `djprofile.nxs`, `rekordbox/RBFLTR.DAT`, `rekordbox/exportExt.pdb`) are compiled into the programs as defaults (`data/trakcrate/user-settings/` for the GUI, `src-cli/c/data/user-settings/` for the CLI) and every export writes all five.

In the GUI library they live in **settings sets** (`user-settings/<set>/`, format in `TRAKCRATE-FORMAT.md` section 10):

- **New settings** creates a set from the built in files and asks for the name to put in `djprofile.nxs` (so nobody inherits the name of the person who made the built in profile).
- **Copy settings from a rekordbox pendrive** (and the rekordbox import) makes a named set from a real drive; any number of sets can be kept. One set is the default.
- An export takes a set from the export dialog's drop down; a virtual pendrive can be linked to a set (`drives/<name>/drive.txt`) and then preselects it. With no set at all the built in files are used and the first export creates a set called "Default".
- **Policy on a drive that already has an export** (update): the default *keeps the drive's own files and adds missing ones*; *update* overwrites them with the set's. `USBMNG.DAT` is only ever added when missing, because it holds that drive's own table.
- The **settings viewer** shows what is known about each file read only (grey rows are not understood), compares two sets, and edits only the name in `djprofile.nxs`.
- The CLI (`src-cli/`) does not know sets yet: it keeps using its compiled in files and `--settings-from`.

`USBMNG.DAT`, `MPJ/` and `CDJ/` are still not part of a set.

## DEVSETTING.DAT (140 bytes) and MYSETTING.DAT (148 bytes)

Same container. All integers little endian.

| Offset | Size | Content                                                                 |
|--------|------|-------------------------------------------------------------------------|
| 0x00   | 4    | `0x60`: length of the header (strings below)                            |
| 0x04   | 32   | brand: `PIONEER DJ` (DEVSETTING) or `PIONEER` (MYSETTING), NUL padded    |
| 0x24   | 32   | `rekordbox`                                                             |
| 0x44   | 32   | version: `4.5.0` (DEVSETTING) or `0.001` (MYSETTING)                    |
| 0x64   | 4    | payload length: `0x20` (DEVSETTING), `0x28` (MYSETTING)                 |
| 0x68   | 4    | magic `78 56 34 12` (0x12345678)                                        |
| 0x6c   | n    | payload (the two settings blocks differ; see below)                     |
| len-4  | 2    | CRC16 of bytes from 0x68 up to the CRC                                  |
| len-2  | 2    | `00 00` padding                                                         |

**CRC verified:** CRC-16/XMODEM (poly 0x1021, init 0, no reflection) over `[0x68, len-4)`, stored little endian. It matched both files (0x3bba and 0x2181). So a modified file can be regenerated correctly.

Payload:
- DEVSETTING: `01 00 00 00` (block id), then the options from 0x70: overview waveform type (0x71), waveform colour (0x72), key display format (0x74) and the current-position marker (0x75), with unknown bytes at 0x70, 0x73 and 0x76-0x87. The codes are those of rekordcrate's `setting.rs`. The shipped (rekordbox 4.5.0 era) file has `00` at 0x74 and 0x75 where rekordbox 6 writes `01`; the viewer shows them as "unknown option 0x00".
- MYSETTING: `02 00 00 00` (block id), then the player preferences from 0x70, one option code (`0x80 + choice`, brightness and language start at `0x81`) per byte: on air display, LCD brightness, quantize, auto cue level, language, jog ring, slip flashing, disc slot illumination, eject/load lock, sync, play mode, quantize beat value, hot cue auto load and colour, needle lock, time mode, jog mode, auto cue, master tempo, tempo range, phase meter. The decode tables are in `src/trakcrate/settings_view.cpp` (`kDevFields`, `kMyFields`); the viewer and the set comparison use them. The shipped file has `00` at 0x85 (needle lock), so that one shows as "unknown option 0x00". Unknown bytes: see `UNKNOWN_BYTES.md`.

Players apply MYSETTING when the drive is loaded, so changing bytes changes player behaviour. The sample's values are the user's own rekordbox preferences, which is another reason to treat the copied file as a template rather than a default.

## djprofile.nxs (160 bytes)

| Offset | Size | Content                                                                                                   | Status                          |
|--------|------|-----------------------------------------------------------------------------------------------------------|---------------------------------|
| 0x00   | 4    | `00 42 d7 c3`                                                                                             | unknown                         |
| 0x04   | 8    | big endian milliseconds since 1970 (`0x18bde67e7e9` = 2023-11-17 17:50:33 UTC), the profile's date        | plausible, not proven           |
| 0x0c   | 16   | zeros                                                                                                     |                                 |
| 0x1c   | 4    | `a2 3f 2b 74`                                                                                             | **unknown** (see below)         |
| 0x20   | 128  | user name, UTF-8, NUL terminated, zero padded (the sample reads `teknopaul teknopaul`)                    | name proven by the sample       |

- This is the profile for the NXS-era players (the player shows who exported the drive).
- Every copy on this machine (7 files, from `/transfer/cdj64`, `/var/tmp/rekordbox-*`, `libcdj`, `/var/vdj`) is byte identical (md5 `5eeea5ef...`): **there is one sample**, from one person. Without a second sample with another name the structure of the name field (one string? "login full name"? where the checksum goes) cannot be confirmed.
- **The 4 bytes at 0x1c:** tested as the value of CRC-32, CRC-32C, CRC-16/XMODEM, Adler-32, FNV-1/1a (32 bit), Jenkins one-at-a-time, djb2, sdbm, MurmurHash3 (seed 0), byte sum and XOR, big and little endian and inverted, over the name (with and without the NUL, the first word alone), bytes 0x20-0x9f, 0x00-0x1b, 0x04-0x0b, the whole file with the field zeroed, and combinations of header and name. **No match.** They may be a random id or a keyed hash; they are left untouched by an edit.
- It contains personal data from the machine that made it. The compiled in default (`data/trakcrate/user-settings/djprofile.nxs`) is that sample, so a fresh set must have the user name replaced (`setDjprofileUserName`, used by "New settings").
- **Name editing is untested on a real deck.** We only change the name bytes (UTF-8, 1-63 bytes, no control characters; the field is 128 bytes long but 63 is assumed safe). If a deck refuses a profile with a changed name, the 0x1c field is the first suspect. To settle this a `djprofile.nxs` from a drive made by a rekordbox user with a different name is needed (see `UNKNOWN_BYTES.md`).

## RBFLTR.DAT (232 bytes, in `PIONEER/rekordbox/`)

Different container: starts `FMAI`, a header length, then three space padded 32 byte strings: `PIONEER DJ`, `XDJ-700`, `1.13` followed by four `FCND` records (u32 tag, header length, record length, then small numbers).

- The device model and firmware are recorded: it is written for a specific player (XDJ-700 firmware 1.13 here), so it is probably a cache or filter list produced when that player was linked. It may be per-device, and not safe to copy blindly. Needs a second sample from another drive.
- Probably optional.

## To do

- Test on a deck: a drive with only `export.pdb` and ANLZ, then add the files back one at a time; a changed `djprofile.nxs` name; `Replace` against `AddMissing`.
- The MYSETTING and DEVSETTING options are decoded for display only; editing them (and fixing the CRC) is not offered.
- Find the meaning of the four bytes at 0x1c of `djprofile.nxs` (a sample with a different name is needed, see `UNKNOWN_BYTES.md`).
