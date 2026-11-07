# Hardware test: Add to rekordbox

The feature: right click a playlist, **Add to rekordbox ›**, choose a mounted rekordbox pendrive. Design and rules: `pdb-append-notes.md`; plan: `.planning/plans/add-to-rekordbox_PLAN.md`.

Before touching a deck, keep a copy of the stick (or use `/var/tmp/rekordbox-edit-backup-export.<stamp>.pdb`, made on every run, and `PIONEER/rekordbox/export.pdb.bak`). If a pull happens between the two renames, `export.pdb.bak` and `export.pdb.new` are both on the stick: rename `export.pdb.new` to `export.pdb`.

## Results (2026-11-07, tested by the author)

| Check | Result |
|-------|--------|
| The popup menu lists the mounted rekordbox pendrive and starts the add with no further dialog | pass |
| A playlist added to a stick plays on the decks | pass |
| A second add or export to the same stick is refused until the first has finished | pass |
| The jobs strip shows progress and the add runs next to other jobs | seen working |

Deck model(s) used: *(fill in)*.

## Still to try (not yet reported)

1. A 30 track playlist (forces new database pages) and a playlist with tracks that are already on the stick (no files written).
2. A stick a deck has already played from (play history rows in the database), and a stick last written by real rekordbox.
3. Tracks with non-ASCII artist or album names (UTF-16 alignment).
4. Pull the stick during the audio copy, plug it into a deck (the old library must be intact), then run the add again.
5. Open the edited stick in rekordbox on a computer.
6. Compare before and after with `cmp`: `DEVSETTING.DAT`, `MYSETTING.DAT`, `djprofile.nxs`, `RBFLTR.DAT`, `exportExt.pdb`, `USBMNG.DAT` must be identical.
7. Does a deck need rows in `exportExt.pdb` for the new tracks? (Not so far: the added tracks play.)
