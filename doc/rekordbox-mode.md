# Rekordbox PC mode (experimental)

vdj-ui can pretend to be a **rekordbox PC** on the Pro DJ Link network: it claims device number 17, announces itself as
`rekordbox`, answers the decks' media-slot query and runs a **dbserver** (TCP 12523 for the port query, TCP 1051 for the
database protocol) that answers the decks' browse and track-metadata requests from a local rekordbox export. It was built
to find out what a deck asks for next when it loads a track from a PC: the audio over NFS, or a request nobody has
documented yet.

Plan and background: `.planning/plans/rekordbox-pc-mode_PLAN.md`, `references/libcdj/doc-internal/how-to-pose-as-rekordbox.md`,
`references/adj/doc/tcp-protocol-notes.md`, `tests/fixtures/dbserver/REPLY_LAYOUTS.md`.

## Safety

* Exclusive: the mode uses UDP 50000-50002 like the normal virtual CDJ. It refuses to start while Network > Connect is up
  or another program (ADJ, another vdj-ui) holds UDP 50000, and Network > Connect is disabled while it runs.
* The old rekordbox PC must be switched off, and no ADJ may run, while you test with real decks.
* Everything the server does is read-only. It sends nothing to a deck that a real device has not sent (the claim sequence,
  keep-alives, the computer name, the slot answer) apart from replies to the deck's own requests.
* Unknown requests are answered with an empty result by default (`--rb-unknown-policy empty`); `ignore` sends nothing,
  `close` drops the connection. Try them in this order if a deck hangs.

## Starting it

Network > Rekordbox PC Mode... opens the dock panel, or from the command line:

    vdj-ui --rekordbox-mode /var/tmp/rekordbox-example --rb-log-dir /var/tmp/rbmode-logs

The export folder is the one that contains `PIONEER/rekordbox/export.pdb` (and `PIONEER/USBANLZ`, `PIONEER/Artwork`).
Pick the interface that faces the decks (the default takes the one with a 169.254.x.x address). Options:
`--rb-iface IF`, `--rb-name NAME` (computer name shown on the deck), `--rb-device N` (default 17), `--rb-log-dir DIR`,
`--rb-unknown-policy ignore|empty|close`.

## The panel

* Status line: device number, address, interface, track count.
* Counters: TCP connections / requests / unknown / not implemented / port queries and NFS observer calls.
* Decks: every deck whose keep-alives were heard (IP, name, device number, last seen; `LOST` after 10 s of silence, which
  also closes that deck's TCP connections, as a real deck does).
* Unknown requests: type, arguments, double-click for the raw bytes; "Copy unknown requests" puts them on the clipboard.
* Log: everything the server and the presence layer do.

## NFS observer

When a deck wants the audio it may ask for NFS (portmap UDP 111, then mount and nfs). The observer decodes and logs those
calls (`GETPORT`, `MNT "<path>"`, `LOOKUP`, `READ`...) and refuses them (mount: access denied, nfs: NFSERR_ACCES). It never
serves file data. Port 111 needs root: either run `sudo setcap cap_net_bind_service=+ep $(readlink -f build/src/vdj-ui)` once
or accept that portmap is not observed (the panel log says so; mount and nfs ports still work if the deck knows them).

## Logs (in the log folder)

| File | Content |
|------|---------|
| `dbserver-<ip>-<time>.log` | every connection: decoded requests and replies, raw hex of what was received |
| `dbserver-<ip>-<time>.bin` | the exact inbound byte stream (ground truth for offline analysis) |
| `unknown-requests.log` | one entry per request of unknown type: the answer to "what did the deck ask for?" |
| `nfs-observer.log` | NFS observer events |

`grep -c . unknown-requests.log` is zero when the deck only used documented requests.

## What is implemented

Setup, the root menu, artist / album / track / key / playlist / genre / label / colour menus and the filtered menus,
search, `render menu` paging, track metadata (`0x2002`), the undocumented `0x2102` track-info request (it returns the audio
file's path and size), album art, beat grid, waveform preview and detail and extended cues from the ANLZ files. Not
implemented yet (empty answers, logged as "documented but not implemented"): bpm / rating / year / time / bitrate / filename
menus, key-distance menus, basic cues (`0x2104`) and analysis tags (`0x2c04`). Request `0x3100` is unknown.
