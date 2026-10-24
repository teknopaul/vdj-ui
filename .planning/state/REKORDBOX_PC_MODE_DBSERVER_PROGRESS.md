# Rekordbox PC mode (dbserver) — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Wire codec and fixtures | complete |
| 2 | Library model from export | complete |
| 3 | Handlers: menus and metadata | complete |
| 4 | Handlers: art, waveforms, cues | complete |
| 5 | TCP servers 12523 and 1051 | complete |
| 6 | UDP presence as device 17 | complete |
| 7 | GUI panel, mode flag, NFS observer | complete |
| 8 | Live experiment with a deck | pending |
| 9 | Replay backwards and document | pending |

## Review notes

### Phase 1 — Wire codec and fixtures

⚠ Round-trip over captured streams is the correctness oracle; check the omitted-blob rule

⚠ Client stream is 61 messages (plan said 27, from the earlier broken parse); empty strings use a 0x0009 terminator in 4 places, kept via Arg::term

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/dbserver/db_codec.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_db_codec.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/fixtures/dbserver/`

### Phase 2 — Library model from export

⚠ Uses the installed libcdj (cdj/pdb.h, cdj/anlz.h; identical to the source tree) instead of compiling its sources in, to avoid duplicate symbols

⚠ Fixture export is built by tests/fixtures/dbserver/build_fixture_export.py from libcdj fixtures (7 real ANLZ pairs match tracks in that pdb)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/dbserver/rb_library.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_rb_library.cpp`

### Phase 3 — Handlers: menus and metadata

⚠ Replay of the captured deck session against export7: 49 requests identical to the deck's replies, 1 compared as a set (sort 0 track order is unexplained), 11 left for phase 4

⚠ Found: undocumented request 0x2102 returns the audio file path and size (item 5); see REPLY_LAYOUTS.md

⚠ Added tests/fixtures/dbserver/export7 (pdb + ANLZ of the user's 7 track LMMS pendrive, no audio): commit decision is yours

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/dbserver/db_handler.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_db_handler.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/fixtures/dbserver/REPLY_LAYOUTS.md`

### Phase 4 — Handlers: art, waveforms, cues

⚠ Beat grid, waveform preview/detail and extended cue blobs reproduce the deck's replies byte for byte, except heap-pointer / unknown fill bytes (GUESS, documented in db_blobs.cpp)

⚠ 2104 (basic cues) and 2c04 (analysis tags) are not implemented: they return empty replies, shapes are a GUESS

⚠ 2504 reply is 1604 zero bytes (undocumented request)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/dbserver/db_blobs.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/dbserver/db_handler.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_db_handler.cpp`

### Phase 5 — TCP servers 12523 and 1051

⚠ unknown-requests.log in the log dir is the main experiment output; ports 12523/1051 need no root

⚠ Server sends the 5-byte greeting once on connect and does not echo the client's (both sides greet immediately in the captures)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/dbserver/db_server.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_db_server.cpp`

### Phase 6 — UDP presence as device 17

⚠ Stage-3 claim shape for rekordbox is only known from deck traffic (GUESS); test on an isolated network first

⚠ Presence sends no 0x28/0x29/status traffic yet: only claims, keep-alives, the name packet and the slot-4 media answer

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/dbserver/rb_packets.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/dbserver/rb_presence.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_rb_packets.cpp`

### Phase 7 — GUI panel, mode flag, NFS observer

⚠ Rekordbox mode is exclusive with Network > Connect and with ADJ (UDP 50000-50002)

⚠ Small hooks in main_window.cpp/.h and main.cpp are shared with ongoing GUI work: check for merge conflicts

⚠ Portmap on UDP 111 needs root or: sudo setcap cap_net_bind_service=+ep <vdj-ui binary>; the NFS observer refuses every call and never serves file data

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/rekordbox_mode_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/dbserver/nfs_observer.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/main.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/rekordbox-mode.md`
