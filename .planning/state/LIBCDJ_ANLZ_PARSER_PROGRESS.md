# libcdj ANLZ parser — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Format doc, survey tool and fixtures | complete |
| 2 | Container parser (PMAI + tags) | complete |
| 3 | Path, beat grid (PQTZ, PQT2) | complete |
| 4 | Cues, loops and memory points | complete |
| 5 | Waveforms (PWAV..PWV5) | complete |
| 6 | Song structure (PSSI) and raw tags | pending |
| 7 | Track-level API and path resolution | complete |
| 8 | Corpus validation, robustness, fuzzing | complete |
| 9 | vdj-ui integration | complete |

## Review notes

### Phase 1 — Format doc, survey tool and fixtures

⚠ Fixtures contain track paths/titles from the user's library

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/doc-internal/rekordbox-anlz-format.text`
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/bin/xdj_anlz.c`
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/test/fixtures/anlz/MANIFEST.text`

### Phase 2 — Container parser (PMAI + tags)

⚠ Needs sudo make install before vdj-ui can use the new header

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/anlz/anlz.c`
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/anlz/anlz.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/nfs/utf16.c`
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/test/anlz_container_test.c.snip`

### Phase 3 — Path, beat grid (PQTZ, PQT2)

⚠ PQT2 is a microsecond-precision beat grid (not a compact one as first guessed); variable-tempo storage is unverified because no sample exists

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/anlz/anlz.c`
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/doc-internal/rekordbox-anlz-format.text`
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/test/anlz_beatgrid_test.c.snip`

### Phase 4 — Cues, loops and memory points

⚠ No real cue comment or memory point exists in the fixtures; layouts for them come from the spec and synthetic tests until the pendrive data arrives

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/anlz/anlz.c`
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/test/anlz_cues_test.c.snip`
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/doc-internal/rekordbox-anlz-format.text`

### Phase 5 — Waveforms (PWAV..PWV5)

⚠ PWV4 (colour preview) is exposed as raw 6-byte columns: its layout is only partly understood

⚠ PWV6/PWV7 (3-band) are spec-only: no .2EX sample exists

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/anlz/anlz.c`
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/test/anlz_waveform_test.c.snip`
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/doc-internal/rekordbox-anlz-format.text`

### Phase 7 — Track-level API and path resolution

⚠ Tolerances are checked by make survey-tracks against the temporary export: 2179/2179 tempos and durations match

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/anlz/anlz.c`
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/bin/xdj_track.c`
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/test/anlz_track_test.c.snip`

### Phase 8 — Corpus validation, robustness, fuzzing

⚠ make test and make test-asan use only committed fixtures; make survey / survey-tracks need an export in ANLZ_TEST_DIR

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/Makefile`
- `/home/teknopaul/bzr_workspace/vdj-ui/libcdj/src/test/anlz_robust_test.c.snip`

### Phase 9 — vdj-ui integration

⚠ Cue comments and colours deliberately not shown (the decks do not support them)

⚠ The playlist export .txt is unchanged: it does not list cues

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/track_browser.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/waveform_delegate.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/waveform_widget.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/track_details.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_rekordbox.cpp`
