# Deck edits copy back — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Refresh from deck (root menu, remote only) | complete |
| 2 | Copy back menu items (playlist and track) | complete |
| 3 | Match, read and diff deck analysis | complete |
| 4 | Writer and confirmation dialog | complete |
| 5 | Docs and human acceptance test | complete |

## Review notes

### Phase 1 — Refresh from deck (root menu, remote only)

⚠ refresh re-reads export.pdb and drops the cached USBANLZ folder; openFolder re-emits databaseLoaded; human test in vdj-ui: edit on deck, right click top item, Refresh from deck

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/nfs/remote_deck.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/track_browser.cpp`

### Phase 2 — Copy back menu items (playlist and track)

⚠ menu items work but the handler in main_window.cpp is a status-bar stub until phases 3-4

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/track_browser.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`

### Phase 3 — Match, read and diff deck analysis

⚠ decode in rb_import.cpp now shared via gridFromAnlz/cuesFromAnlz (rbimport/export tests pass); matching has no audio-hash or export-map strategy yet: the deck's audio is not mirrored, so it uses title+artist+file name

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/deck_edits.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/rb_import.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_deckedits.cpp`

### Phase 4 — Writer and confirmation dialog

⚠ copy back replaces the library's grid / whole cue list with the deck's; history snapshot in <track>/.trakcrate-history/*-deck-copy-back; no audio-hash matching yet

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/deck_edits.cpp`

### Phase 5 — Docs and human acceptance test

⚠ needs the real-deck human test in vdj-ui: edit on deck, Refresh from deck, Copy back beatgrid / cue points, check the track editor and the .trakcrate-history snapshot

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/deck-edits.md`
