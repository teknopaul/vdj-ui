# deck-vbr-loops

The ANLZ files a CDJ/XDJ wrote (2026-11-08) after loops were made on the deck for a variable bitrate mp3 (`DEBUT-Cobrah.mp3`, 128 kbps VBR with a Xing header, 185.9 s) that
trakcrate had exported. Hot cue A is a plain cue at 20810 ms, B and C are loops (25425..30041 ms and 32348..36964 ms, 8 beats each).

What it shows (`doc/trakcrate/cuemarks.md`):

- The `PCPT` entry of a loop on a VBR mp3 ends with two pairs: start and end in 1/75 s (`0x772`/`0x8cd` for 25425/30041 ms), then start and end as byte offsets
  into the mp3 (`0xd4160`/`0xfc480`). The plain cue has `0`/`0xffffffff` there.
- The deck's byte offsets are frame boundaries but not the ones `Mp3Index::offsetAtMs` gives (0xd4220/0xfc2a0, one to three frames off); the deck does not use the Xing TOC either.
- The `PCP2` entry of a loop is the 44 byte form with `0008 0001` (8 beats) after two zero words; no byte offsets there.
