# Add to rekordbox — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Study and rules | complete |
| 2 | Extract shared exporter pieces | complete |
| 3 | Load and sanity check existing db | complete |
| 4 | Edit the in-memory database | complete |
| 5 | addToDrive engine | complete |
| 6 | GUI menu and job integration | complete |
| 7 | Hardware test on decks | complete |
| 8 | CLI port | complete |
| 9 | Docs and recommendations | complete |

## Review notes

### Phase 1 — Study and rules

⚠ Index pages of real tables are left untouched: only hardware can prove this

⚠ Decisions recorded: refuse on playlist name clash and exportLibrary.db, reference existing tracks, never-deleted /var/tmp backups

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/pdb-append-notes.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/fixtures/trakcrate/pdb-drive`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_pdb_roundtrip.cpp`

### Phase 2 — Extract shared exporter pieces

⚠ Pure refactor: any behaviour change here is a bug (all 69 tests green)

⚠ Phase 4 needs a neutral track template: loadTrackTemplate copies the first row's extra strings

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/exporter.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/pdb_build.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/pdb_rows.h`

### Phase 3 — Load and sanity check existing db

⚠ Sanity rules are my reading of real files: tighten after hardware tests

⚠ Loads the real 1.7 MB TEKNOPAUL64 and cdj64 sticks when mounted (tests skip them otherwise)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/pdb_append.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/pdb_append.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_pdb_append.cpp`

### Phase 4 — Edit the in-memory database

⚠ Row/page placement rules are guesses until a deck loads the result (index pages untouched)

⚠ Verified against 2195-track and real rekordbox sticks: old rows byte-identical, flags rule holds

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/pdb_append.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_pdb_append.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/pdb-append-notes.md`

### Phase 5 — addToDrive engine

⚠ Two renames as in the brief: a pull between them leaves export.pdb.bak + export.pdb.new, no export.pdb (repair by renaming)

⚠ Orphan cleanup after an early eject is best effort; all 71 GUI tests green

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/exporter.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/exporter.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_drive_add.cpp`

### Phase 6 — GUI menu and job integration

⚠ Menu lists only mounted volumes with export.pdb; a stick mounted after the menu opened needs the menu reopened

⚠ Not driven by hand in the real GUI: the context-menu popup itself is untested (its pieces are tested)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/mounted_sticks.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_panel.cpp`

### Phase 7 — Hardware test on decks

⚠ Only the checks the author reported are marked passed; the rest of the checklist is still open

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/hardware-test-add-to-rekordbox.md`

### Phase 8 — CLI port

⚠ Script is trakcrate-add-to-drive (the command name), not trakcrate-add as the plan file list says

⚠ SHA256SUMS of an earlier trakcrate export goes stale after an add (same as the GUI); verify reports it

⚠ Cancel by signal is not covered by a test; --yes is accepted by the wrapper but has no effect

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/c/lib/rbappend.c`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/c/lib/rbadd.c`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/script/usr/lib/trakcrate/trakcrate-add-to-drive`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/test/test_add.sh`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/test/test_append_oracle.sh`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/test/test_add_oracle.sh`

### Phase 9 — Docs and recommendations

⚠ CLI section says GUI only until Phase 8 is done; hardware list names only what was reported

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/rekordbox-db-output.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/ai-output/refactor-recommendations.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/cli.md`
