# djtoolz (DJ Tools): a guide for agents

UI name **DJ Tools**, sanename `djtoolz` in code, files, config and docs (class `Djtoolz*`, constant `DJTOOLZ`, files `djtoolz_*`, config/dirs
`djtoolz-*`). A *tool* (`DjtoolzTool`) is one thing the DJ adds to the panel: Bombomatic, Stab player, Acid loop, One shot, MIDI matic,
Dembomatik, DR-6S. Never say "module" or "sampler" (see sanename.org rules; the UI name is the only place with a space and capitals).

## Layout

| Part | Where |
|---|---|
| Tool model: settings, beat planning, JSON | `src/audio/djtoolz_tool.{h,cpp}` (`DjtoolzTool`, `ToolSettings`, one subclass per type) |
| Scheduler (looks ~100 ms ahead of the VDJ clock) and engine (PulseAudio mixer) | `djtoolz_scheduler`, `djtoolz_engine` |
| Presets, working state | `djtoolz_presets` (`<configDir>/djtoolz-presets/*.json`, `djtoolz-current.json`) |
| Panel (tree, tools, presets, master) and one widget per tool | `src/ui/djtoolz_panel`, `djtoolz_tool_widget` |
| MIDI controller (strips, faders, pickup) | `src/midi/djtoolz_midi_control`, `djtoolz_ctl_map`, `src/ui/djtoolz_controller` |
| Tests | `tests/test_djtoolz_*.cpp` (UI: `test_djtoolz_ui`) |

## Adding a tool type

1. `DjtoolzTool::Type` enum, a subclass in `djtoolz_tool.h` (override `typeName`, `hitsBeat` / `collectHits` / `plan`, and the `supports*` flags).
2. `typeKey()` (the JSON `"type"` string, never change it once released), `create()` and the list in `fromVariant()` in `djtoolz_tool.cpp`.
3. The Add menu in `DjtoolzPanel` and, if it needs its own controls, `DjtoolzToolWidget`.
4. Settings that must survive a restart go through `toVariant` / `fromVariant`; give new keys defaults so old presets still load.
5. Tests: planning (`test_djtoolz_tools`), save/load (`test_djtoolz_presets`), widget (`test_djtoolz_ui`).

## Effects (FX tool)

`FxTool` is the one tool that makes no sound of its own (`isEffect()`; the scheduler skips it). It holds a LADSPA plugin key
(`<file>|<label>`, `LadspaHost` in `src/audio/ladspa_host.*`) and the knob values by control name. `FxRack` lists the FX tools; every audio output
(DJ Tools engine, local player) owns an `FxRunner` that keeps its own plugin instances and applies them to each block, so a new output only needs
`setFxRack()` and one `process()` call. `plugins/xone-ladspa/` is our own LADSPA plugin (the LMMS Xone filter); `tests/test_fx.cpp`
(`VDJ_FX_SMOKE=1` runs every installed plugin).

## Rules

- Everything follows the VDJ sequencer (`AlsaClock`); the panel is silent while it is stopped. Hiding the panel must not stop the tools.
- Latency is compensated once, in the engine (`setLatencyMs`), never per tool.
- Hardware events never reach the tools directly: the DJ Tools Input (Config > Instruments) feeds `DjtoolzMidiControl`, which is separate from the deck map.
  Faders and knobs use soft takeover (`Pickup`).
- Old files and config keys named `sampler*` / `modules` are migrated or read for compatibility (`ConfigManager`, `djtoolz_presets.cpp`); keep that when renaming anything persisted.
- Build only from `build/` (`cmake . && make -j8`), run `ctest`; the tests use `QT_QPA_PLATFORM=offscreen`.
- User docs live in `README.md` (section *DJ Tools*).
