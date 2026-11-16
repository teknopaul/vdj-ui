# Link Access Error — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Observability: log every request and our reply (+ raw TCP) | complete |
| 2 | Decode logged writes offline, protocol note | complete |
| 3 | USER STEP: capture a real deck answering writes | complete |
| 4 | Answer the write requests like a real deck | complete |
| 5 | Audit NFS write path (libcdj) | pending |
| 6 | DISCOVERY ip=? and end-to-end check | pending |

## Review notes

### Phase 1 — Observability: log every request and our reply (+ raw TCP)

⚠ No behaviour change: replies to the deck are exactly as before; raw TCP logging is off unless switched on (View > Log Raw TCP of the dbserver, --link-tcp-raw)

⚠ The discovery-socket replies we send from libcdj are still not logged (libcdj change not agreed)

⚠ dbserver .bin renamed .rx.bin (+ .tx.bin); dbserver lines already reached the LINK debug category, so only kind tags + DB-TCP lines were added

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/network/network_manager.cpp:131`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/dbserver/db_server.cpp:186`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/dbserver/raw_tcp_log.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/network/discovery_line.cpp`

### Phase 2 — Decode logged writes offline, protocol note

⚠ Meanings of 0x2705/0x3001/0x2005/0x2605/0x2804/0x0001 are inferences until phase 3's capture of a real deck answering them (the one hard fact: 0x2005's blob equals our own 0x2004 reply)

⚠ Phase 3 needs the human with two real decks

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/references/adj/doc/link-media-writes.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/references/adj/tools/dbserver-requests-summary.py`
- `/home/teknopaul/bzr_workspace/vdj-ui/references/adj/tools/dbmsg.py`

### Phase 3 — USER STEP: capture a real deck answering writes

⚠ Real replies came from /var/tmp/prolink-edit-real.pcap; cue DELETE and history (0x3001) were not captured

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/references/adj/doc/link-media-writes.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/references/adj/tools/dbserver-pcap-requests.py`

### Phase 4 — Answer the write requests like a real deck

⚠ Edits are kept in memory only: the exported pendrive stays read-only

⚠ Deck retest still needed: USB FULL gone? cue delete / 0x3001 / 0x2005 untested

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/dbserver/db_handler.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_db_handler.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/link-export.md`
