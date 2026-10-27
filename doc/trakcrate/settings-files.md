# Settings and profile files (study)

Source: `references/libcdj/rekordbox/PIONEER/` (the 7-track export). The big pendrive `/media/teknopaul/TEKNOPAUL64` was not mounted when this was written, so there is only one sample of each file; plug it in and `cmp` to see which bytes are per-drive.

## Verdict

Copying them verbatim once from a real pendrive is reasonable and the safe default. We also know enough to generate the two `*SETTING.DAT` files, so we can stamp out settings later if wanted. `djprofile.nxs` and `RBFLTR.DAT` are different: see below.

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
- DEVSETTING: `01 00 00 00` then `01 01 01 01` then zeros. Probably a small number of player "device" options; the meaning of each byte is unknown.
- MYSETTING: `02 00 00 00` then 28 bytes of values such as `81 83 81 84 81 01 82 81 ...`. The values with the high bit set look like option codes (`0x80 + choice`); these are the user preferences that rekordbox exports to players (e.g. auto cue, jog mode, quantize, sync). Which byte is which setting is not decoded and not needed if we copy.

Players apply MYSETTING when the drive is loaded, so changing bytes changes player behaviour. The sample's values are the user's own rekordbox preferences, which is another reason to treat the copied file as a template rather than a default.

## djprofile.nxs (160 bytes)

Binary, starts `00 42 d7 c3 00 00 01 8b de 67 e7 e9` (looks like a 64-bit millisecond timestamp, ~Oct 2023), then bytes at 0x1c `a2 3f 2b 74`, then the user's name as text (`teknopaul teknopaul`, apparently first and last name from the rekordbox profile), then zeros.

- This is the profile for the NXS-era players (the player shows who exported the drive).
- It contains personal data from the machine that made it. Do **not** ship it verbatim in the repo or in generated pendrives for others. Generating it with our own name is likely fine; the 4 bytes at 0x1c might be a checksum or id and are undecoded.
- Probably optional; confirm by bisecting on a deck.

## RBFLTR.DAT (232 bytes, in `PIONEER/rekordbox/`)

Different container: starts `FMAI`, a header length, then three space padded 32 byte strings: `PIONEER DJ`, `XDJ-700`, `1.13` followed by four `FCND` records (u32 tag, header length, record length, then small numbers).

- The device model and firmware are recorded: it is written for a specific player (XDJ-700 firmware 1.13 here), so it is probably a cache or filter list produced when that player was linked. It may be per-device, and not safe to copy blindly. Needs a second sample from another drive.
- Probably optional.

## Suggested handling in the exporter

1. Ship a `templates/` directory in the repo with `DEVSETTING.DAT` and `MYSETTING.DAT` copied from a real drive. They contain no personal data (check the second drive first).
2. Write them to the output with a CRC check on load, so a damaged template is rejected rather than copied.
3. Generate `djprofile.nxs` only after confirming a player needs it; start without it.
4. Skip `RBFLTR.DAT` initially.

## To do

- Mount `TEKNOPAUL64` and diff all four files against the 7-track copies (what varies per drive?).
- Test on a deck: drive with only `export.pdb` and ANLZ, then add files back one at a time.
- Decode the MYSETTING bytes against the rekordbox preferences screen only if we want to change settings.
