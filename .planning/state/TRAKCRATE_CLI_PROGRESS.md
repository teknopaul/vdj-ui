# TRAKCRATE-CLI — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Phase 0: documents (the contract) | complete |
| 2 | Phase 1: skeleton (src-cli, wrapper, deb) | complete |
| 3 | Phase 2: database commands (bash) | complete |
| 4 | Phase 3: C library of the format | complete |
| 5 | Phase 4: analysis in C | complete |
| 6 | Phase 5: rekordbox output in C | complete |
| 7 | Phase 6: rekordbox import and inspection | complete |
| 8 | Phase 7: docs, completion, release | pending |

## Review notes

### Phase 1 — Phase 0: documents (the contract)

⚠ Section 16 of TRAKCRATE-FORMAT.md (hidden entries, .trakcrate-lock, exit status 4 for busy) is new; the C++ code only takes the lock for script runs, GUI edits do not

⚠ gui.md rewritten to the independent-implementations decision (gui.md had uncommitted edits: check the diff)

⚠ cli-interface.md field names and progress events are a first draft, not yet exercised by any code

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/cli.md:1`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/cli-interface.md:1`
- `/home/teknopaul/bzr_workspace/vdj-ui/TRAKCRATE-FORMAT.md:249`

### Phase 2 — Phase 1: skeleton (src-cli, wrapper, deb)

⚠ shellcheck is not installed here: the wrapper and build-deb.sh were only checked with bash -n and the tests

⚠ The .deb Depends has no libcdj or ffmpeg yet (the helper links neither); add them when Phase 4/5 link them. A real install in a clean container was not run: the test unpacks with dpkg-deb -x

⚠ src-cli/c/test/snip_core.h is a copy of libcdj's (logged in ai-output/refactor-recomendations.md)

⚠ Wrapper has two test-only environment variables not in the original spec: TRAKCRATE_LOCAL_DIR; documented in cli.md

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/script/usr/bin/trakcrate:1`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/c/helper/dispatch.c:1`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/deploy/build-deb.sh:1`

### Phase 3 — Phase 2: database commands (bash)

⚠ ls --sort and 'add' (single files) are not done; import refuses --analyse until the analysers exist (Phase 4)

⚠ The key/value rules now exist in bash (awk), C (print only) and C++: a third copy, logged in ai-output/refactor-recommendations.md

⚠ cover extraction was only tested if this ffmpeg can make an mp3 with a picture (see test_helper.sh output); shellcheck still not installed

⚠ import takes the library lock for the whole run; the lock is bash-only and the GUI does not take it for edits

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/script/usr/lib/trakcrate/trakcrate-import:1`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/script/usr/lib/trakcrate/common.sh:1`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/c/helper/cmd_probe.c:1`

### Phase 4 — Phase 3: C library of the format

⚠ The bash commands still use their own awk key/value code (tested equal to the C code); nothing in the commands calls the C kv yet

⚠ waveform.bin and vocals.txt are not in the C library yet (waveform arrives with the analysers in Phase 4)

⚠ No cross-check against the C++ KvDoc/Cuemark code, only bash vs C

⚠ tc_path_within treats a dangling symlink as outside (conservative)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/c/lib/fsutil.c:1`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/c/lib/kv.c:1`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/c/lib/cuemarks.c:1`

### Phase 5 — Phase 4: analysis in C

⚠ Byte identical to the C++ analyser on 8 synthetic click tracks only: not yet compared on real music or with rekordbox's own beat grids and waveforms

⚠ Not done: waveform --png, beatgrid --bpm/--first-beat, analyse --report/--cache, key and vocals

⚠ test_oracle.sh needs build/src/libvdj-trakcrate.a (skipped otherwise) and python3 for the synthetic audio

⚠ analyse has a sliding window of parallel jobs: a slow track at the head of the queue limits the speed-up

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/c/lib/beats.c:1`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/c/helper/cmd_analyse.c:1`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/script/usr/lib/trakcrate/trakcrate-analyse:1`

### Phase 6 — Phase 5: rekordbox output in C

⚠ Byte identical to the C++ exporter on a 3 track synthetic library (JPEGs aside); nothing here has been tried on a real CDJ/XDJ, and the unknown bytes are the template's or guessed, as in the C++ exporter

⚠ sync links the audio and rsyncs (size+mtime for audio, content for metadata); it does not read the audio, so it removes SHA256SUMS; --delete-orphans is confined to Contents/ and PIONEER/

⚠ Not done: user defined pendrive entries as export sources, --keep-tmp, files copied from another drive; the .deb now depends on libcdj (>= 0.1) and libswscale; the template export.pdb is a third copy under src-cli/script/usr/share/trakcrate/

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/c/lib/rbexport.c:1`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/c/lib/rbpdb.c:1`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/script/usr/lib/trakcrate/trakcrate-sync:1`

### Phase 7 — Phase 6: rekordbox import and inspection

⚠ Tested only on libcdj's real 7 track drive (and the 2195 track database, which has no audio); not on a large real drive

⚠ Not done: RBFLTR.DAT/exportExt.pdb/djprofile.nxs copies, per playlist folder choice, skip by drive path and size, keys/colours in pdb dump; folder spelling comes from bash tc_sanitize_name, not the GUI's sanitizeName

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/c/helper/cmd_rbimport.c:1`
- `/home/teknopaul/bzr_workspace/vdj-ui/src-cli/script/usr/lib/trakcrate/trakcrate-import-rekordbox:1`
