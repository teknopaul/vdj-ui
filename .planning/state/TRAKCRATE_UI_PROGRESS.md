# TRAKCRATE-UI — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Library model and file formats | complete |
| 2 | Import a folder of audio | complete |
| 3 | trakcrate pane (browser + import) | complete |
| 4 | Analysis: beat grid, waveforms | complete |
| 5 | PDB writer 1: byte-identical re-emit | complete |
| 6 | PDB writer 2: build from library | complete |
| 7 | ANLZ writer, settings, artwork | complete |
| 8 | Staged export and dialog | complete |
| 9 | Hardware gate (human-run) | complete |
| 10 | Load into player; rating, comment | complete |
| 11 | Cuemarks and hot cues | complete |
| 12 | Beat grid nudging | complete |
| 13 | Artwork editor integration | complete |
| 14 | Scripts menu | complete |

## Review notes

### Phase 1 — Library model and file formats

⚠ Qt-free module in src/trakcrate: confirm this is acceptable vs doc/trakcrate/gui.md (GUI calls CLI)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/library.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/track.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_library.cpp`

### Phase 2 — Import a folder of audio

⚠ audio_probe duplicates some of src/audio/audio_decoder.cpp's ffmpeg use on purpose (Qt-free)

⚠ nothing imported = no playlist created (and replace deletes nothing); recursive subfolders become master folders named '<base> - <sub path>'

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/importer.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/audio_probe.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_import.cpp`

### Phase 3 — trakcrate pane (browser + import)

⚠ dock follows the rbDock_ pattern (View > trakcrate Library); no editing yet

⚠ import failures/skips are shown as a status-line summary with a tooltip list, not a report dialog

⚠ GUI never run (offscreen tests only): look at the layout by eye before relying on it

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_import_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/main_window.cpp`

### Phase 4 — Analysis: beat grid, waveforms

⚠ height curve is linear peak (0 dBFS = 31), uncalibrated against rekordbox: needs the user's Beatport vs unnormalised sample data

⚠ PWV4 not generated; colour/whiteness mapping is a guess

⚠ beat detector prefers kicks (low band) over hats; only tested on synthetic audio, validate against real tracks and rekordbox grids

⚠ mono files get +3 dB (rematrix_volume) so swresample's -3 dB upmix does not make them look quieter

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/analysis_beats.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/analysis_wave.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_analysis.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_panel.cpp`

### Phase 5 — PDB writer 1: byte-identical re-emit

⚠ byte-identical round trip reached for export7.pdb, exportExt7.pdb and the 2195 track export, and every data page is rebuilt from rows alone: no need to stop before Phase 6

⚠ index pages (flags 0x64) are kept raw and NOT decoded: Phase 6 must copy a template pattern for small exports; the big export's tracks index page holds an undecoded per-row directory

⚠ table entry order is type, empty_candidate, first, last (rekordbox-db-format.text had the last two swapped)

⚠ page header words u10/u14/u20/numLarge/u24/u26 and the row flags words are copied, not understood: see UNKNOWN_BYTES.md

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/pdb_writer.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/pdb-write-notes.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_pdb_roundtrip.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/UNKNOWN_BYTES.md`

### Phase 6 — PDB writer 2: build from library

⚠ ANLZ directory naming (hash of the audio), index page bodies (no per-row directory), the file layout with a blank page per table, and several track-row fields are invented from templates: see UNKNOWN_BYTES.md

⚠ built from the 7-track template only; never tried on a player

⚠ strings are capped at 500 bytes; a row that cannot fit in a page is an error

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/pdb_build.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/pdb_writer.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/UNKNOWN_BYTES.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_pdb_build.cpp`

### Phase 7 — ANLZ writer, settings, artwork

⚠ ANLZ writer matches the 7 real file pairs tag by tag, but PVBR body, PQT2 word 0x2c and the PWV4 contents are guessed/zero: see UNKNOWN_BYTES.md

⚠ artwork pixel sizes (240/80) and the _m copy are unverified; the directory rule id/20+1 IS verified on all 2087 rows

⚠ settings templates are the user's own MYSETTING.DAT preferences embedded verbatim; djprofile.nxs and RBFLTR.DAT are not written

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/anlz_writer.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/settings_files.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_artwork.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_anlz_roundtrip.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/UNKNOWN_BYTES.md`

### Phase 8 — Staged export and dialog

⚠ single write means sequential, each file once, export.pdb last; not literally one write(2)

⚠ audio is streamed, never staged; the template database is embedded from data/trakcrate/template-export.pdb (a copy of the 7-track drive's export.pdb)

⚠ nothing has been tried on a player yet: the export dialog is only tested offscreen; Phase 9 is the hardware gate

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/exporter.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_export_dialog.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_export.cpp`

### Phase 9 — Hardware gate (human-run)

⚠ Gate passed per the user (works on real hardware); player model and file system not recorded

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/hardware-test.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/UNKNOWN_BYTES.md`

### Phase 10 — Load into player; rating, comment

⚠ GUI untested: Load into player (context menu, double click), the Track dock and star clicks were only exercised offscreen

⚠ only one local player exists, so 'Load into deck N' is 'Load into player'

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_track_editor.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/star_rating_widget.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_analysis.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/player_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_editor.cpp`

### Phase 11 — Cuemarks and hot cues

⚠ needs a deck test for how the spike looks (hardware-test.md)

⚠ player deck still shows cues from load time; editing marks does not refresh it until the track is reloaded

⚠ no CLI/GUI option yet for green spikes or width: ExportOptions::cuemarks only

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/waveform_widget.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/cuemark_render.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/cuemark_edit.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_cuemarks.cpp`

### Phase 12 — Beat grid nudging

⚠ BPM nudging, tap tempo and snap-to-onset are not done (offset nudging only)

⚠ the editor's waveform has no playhead, so 'Set beat 1' uses the pointer (B key / context menu) rather than the deck position

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_track_editor.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/waveform_widget.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/grid_edit.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_nudge.cpp`

### Phase 13 — Artwork editor integration

⚠ no real editor launched (by design); try Open in editor once by hand

⚠ tests/test_trakcrate_artwork.cpp (the encoder test) was overwritten by mistake and restored from the session transcript: check it still matches your copy; the new tests are in test_trakcrate_artwork_edit.cpp

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/artwork_launcher.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/artwork_view.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_artwork_edit.cpp`

### Phase 14 — Scripts menu

⚠ never tried with the GUI: menus, review dialog and editor launch are untested by hand

⚠ deck number/position, crate selection files, spinner, example scripts and CLI equivalents are not done

⚠ review state lives in the per-user app config, not in the library (deliberate, see scripts.md)

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_scripts.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/scripts.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/tests/test_trakcrate_scripts.cpp`
