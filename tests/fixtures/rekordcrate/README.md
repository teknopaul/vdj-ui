# Test data copied from rekordcrate

Copied byte for byte on 2026-11-07 from <https://github.com/Holzhaus/rekordcrate> (commit `68cd702`, `data/`). rekordcrate is licensed MPL-2.0
(`COPYING-MPL-2.0` here); these are its test data files, no code. They are real rekordbox 6 output.

| Path here                                                                                                            | From                                         | What for                                                                                                                                        |
|----------------------------------------------------------------------------------------------------------------------|----------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------|
| `complete_export/{empty,demo_tracks}/PIONEER/`                                                                       | `data/complete_export/`                      | whole exports: DEVSETTING, MYSETTING, MYSETTING2, DJMMYSETTING, export.pdb, exportExt.pdb, and for demo_tracks 2 tracks of ANLZ (DAT, EXT, 2EX) |
| `mysetting2/<option>/MYSETTING2.DAT`, `djmmysetting/<option>/DJMMYSETTING.DAT`, `devsetting/<option>/DEVSETTING.DAT` | `data/{mysetting2,djmmysetting,devsetting}/` | one file per option value: the offset of each option and the CRC rule are found by comparing them                                               |
| `pdb/unit_tests/*.bin`                                                                                               | `data/pdb/unit_tests/`                       | single pdb pages of every type (menu, columns, history, index ...)                                                                              |

The ANLZ `.2EX` / PSSI files used by the libcdj tests are in `libcdj/src/test/fixtures/anlz-2ex/` (same source, with a `MANIFEST.text` and checksums).
Used by `test_trakcrate_pdb_tables.cpp` and `test_trakcrate_settings.cpp`.
