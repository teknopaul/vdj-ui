# Virtual pendrive switching — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Capture runbook and tools | pending |
| 2 | Analyse captures | complete |
| 3 | Protocol and runtime switching | complete |
| 4 | Verify on real decks | pending |
| 5 | Pendrive registry and scanner | complete |
| 6 | UI: manage, load, export | complete |
| 7 | End-to-end check and docs | pending |
| 8 | Anomaly testing: yanked stick | pending |

## Review notes

### Phase 2 — Analyse captures

⚠ Based on prolink-usb-extracts.pcap only; refused/succeeded classification is inferred from the packets and not yet confirmed by the human

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/references/adj/doc/usb-switching-protocol.md`

### Phase 3 — Protocol and runtime switching

⚠ Needs 'sudo make install' in libcdj before build/ compiles

⚠ Refusal timing and ignoring media queries while not ready are // GUESS; not tried on real decks

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/network/link_export.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/../libcdj/src/c/vdj.c`

### Phase 5 — Pendrive registry and scanner

⚠ Config section virtual_pendrives is new

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/export/pendrive_registry.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_pendrive_registry.cpp`

### Phase 6 — UI: manage, load, export

⚠ Not yet exercised on real decks or interactively in the GUI

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/virtual_pendrives_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp:560`
