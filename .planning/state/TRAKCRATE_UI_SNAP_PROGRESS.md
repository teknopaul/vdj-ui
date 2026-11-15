# trakcrate-ui Snap — Progress

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Real-home helper and default library | complete |
| 2 | Snap skeleton that builds | complete |
| 3 | Audio | complete |
| 4 | USB media | complete |
| 5 | Shared-data verification and finish | complete |

## Review notes

### Phase 1 — Real-home helper and default library

⚠ Default library is ~/trakcrate; existing remembered libraries are untouched

⚠ No modal first-run prompt to create ~/trakcrate (it would block offscreen tests); it opens silently if present, and the New library dialog starts there

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/real_home.h`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/gui.md`

### Phase 2 — Snap skeleton that builds

⚠ snapcraft is not installed: the YAML is UNBUILT and untested

⚠ libcdj/libvdj are not distro packages: a libcdj part builds them from references/libcdj (a symlink out of the repo), so --destructive-mode is required

⚠ Added root CMake option VDJ_BUILD_TESTS (default ON, no change for normal builds)

⚠ Stage package names (libavformat60, libasound2t64, ...) assume Ubuntu 24.04; unchecked

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/snap/snapcraft.yaml`
- `/home/teknopaul/bzr_workspace/vdj-ui/snap/gui/trakcrate-ui.desktop`
- `/home/teknopaul/bzr_workspace/vdj-ui/CMakeLists.txt`

### Phase 3 — Audio

⚠ No code change was needed: the library window only uses libpulse; no alsa plug

⚠ NOT verified on a running Snap (no snapcraft here): the manual test list in snap.md has not been run, AppArmor denials unknown

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/snap/snapcraft.yaml`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/snap.md`

### Phase 4 — USB media

⚠ removable-media needs a manual snap connect on a sideloaded Snap; the pendrive menu hint says so

⚠ No eject/unmount code exists in the library window, so nothing was disabled

⚠ Staging-directory choice (skip /dev/shm when SNAP is set) has no unit test: pickStagingBase is internal to exporter.cpp

⚠ Not verified on a running Snap: USB manual test in snap.md not run

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/src/export/pendrive_registry.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/trakcrate/exporter.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_panel.cpp`
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/snap.md`

### Phase 5 — Shared-data verification and finish

⚠ Snap has no working scripts for host tools, no external stretchers, no eject; vdj-ui stays a deb

⚠ The two-install check (Snap + deb on one library) and all manual audio/USB tests are written down but NOT run: snapcraft is not installed

⚠ Store submission (grade stable, name registration, interface approval) not started

Files for review:
- `/home/teknopaul/bzr_workspace/vdj-ui/doc/trakcrate/snap.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/snap/snapcraft.yaml`
- `/home/teknopaul/bzr_workspace/vdj-ui/README.md`
- `/home/teknopaul/bzr_workspace/vdj-ui/src/ui/trakcrate_scripts.cpp`
