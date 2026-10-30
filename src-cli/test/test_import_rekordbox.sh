#!/bin/bash
#
# import-rekordbox, pdb, anlz, unknown-bytes on the real 7 track rekordbox drive of libcdj's fixtures (references/libcdj/rekordbox)
#
. "$(dirname "$0")/lib.sh"

drive=$TC_ROOT/../references/libcdj/rekordbox
[ -f "$drive/PIONEER/rekordbox/export.pdb" ] || { echo "test_import_rekordbox.sh: $drive is not there, skipped"; exit 0; }

tc() { "$TRAKCRATE" --library "$LIB" "$@"; }
M() { printf '%s/track-master-copy/%s' "$LIB" "$1"; }

setup()
{
    LIB=$T/lib
    tc init >/dev/null 2>&1
}

test_import_a_real_drive()
{
    setup
    run tc import-rekordbox "$drive"
    assert_status "import" 0
    assert_contains "counts" "imported 7, duplicates 0, audio missing 0, failed 0" "$out"
    assert_contains "what came from rekordbox" "7 beat grids, 7 cue lists, 7 waveforms" "$out"
    assert_contains "one playlist" "playlists made: 1" "$out"
    run tc check
    assert_status "the library is valid" 0
    local t
    t=$(find "$LIB/track-master-copy" -mindepth 2 -maxdepth 2 -type d | sort | head -n 1)
    assert_file "audio" "$t/track.mp3"
    assert_file "waveform.bin" "$t/waveform.bin"
    assert_file "cuemarks" "$t/cuemarks.txt"
    assert_file "original DAT kept" "$t/anlz/ANLZ0000.DAT"
    assert_file "original EXT kept" "$t/anlz/ANLZ0000.EXT"
    assert_contains "origin: beat grid" "origin.beatgrid: rekordbox" "$(cat "$t/source.txt")"
    assert_contains "origin: waveform" "origin.waveform: rekordbox" "$(cat "$t/source.txt")"
    assert_contains "origin: meta" "origin.meta: rekordbox" "$(cat "$t/source.txt")"
    assert_contains "imported from" "imported_from: rekordbox:/Contents/Intapol/UK Dembow/" "$(cat "$t/source.txt")"
    assert_contains "the artist from the database" "artist: Intapol" "$(cat "$t/meta.txt")"
    assert_contains "bpm from the grid" "bpm: 133.00" "$(cat "$t/beatgrid.txt")"
    assert_contains "grid analyser" "analyser: rekordbox" "$(cat "$t/beatgrid.txt")"
    assert_eq "ANLZ kept byte for byte" "$(cksum < "$drive/PIONEER/USBANLZ/P004/00028148/ANLZ0000.DAT")" \
        "$(cksum < "$(grep -l 'Whistling Diesel\|diesel' "$LIB"/track-master-copy/*/*/meta.txt | head -n 1 | xargs dirname)/anlz/ANLZ0000.DAT")"
    assert_eq "the playlist has seven entries" "7" "$(ls "$LIB"/pendrives/*/symlinks | wc -l)"
    assert_eq "the drive is untouched" "" "$(find "$drive" -newer "$T" -type f 2>/dev/null | head -n 1)"
}

test_imported_tracks_are_not_reanalysed()
{
    setup
    tc import-rekordbox "$drive" >/dev/null 2>&1
    run tc analyse --all
    assert_status "analyse" 0
    assert_contains "all skipped" "analysed 0, skipped 7, failed 0" "$err"
}

test_a_second_import_finds_duplicates()
{
    setup
    tc import-rekordbox "$drive" >/dev/null 2>&1
    run tc import-rekordbox "$drive"
    assert_status "second import" 0
    assert_contains "all duplicates" "imported 0, duplicates 7" "$out"
    assert_contains "the playlist stays" "already exists" "$err"
}

test_dry_run_changes_nothing()
{
    setup
    run tc -n import-rekordbox "$drive"
    assert_status "dry run" 0
    assert_contains "counted" "imported 7" "$out"
    assert_eq "no folders" "" "$(ls "$LIB/track-master-copy")"
    assert_eq "no playlists" "" "$(ls "$LIB/pendrives")"
}

test_options()
{
    setup
    run tc import-rekordbox --no-playlists --no-playlist-folders --folder Real --no-anlz --no-waveform --settings "$drive"
    assert_status "options" 0
    assert_file "own folder" "$LIB/track-master-copy/Real"
    assert_eq "no playlists" "" "$(ls "$LIB/pendrives")"
    assert_no_file "no waveform" "$LIB/track-master-copy/Real/$(ls "$LIB/track-master-copy/Real" | head -n 1)/waveform.bin"
    assert_no_file "no ANLZ copy" "$LIB/track-master-copy/Real/$(ls "$LIB/track-master-copy/Real" | head -n 1)/anlz"
    assert_file "settings kept" "$LIB/user-settings/DEVSETTING.DAT"
    run tc --format tsv import-rekordbox --by-path "$T/nothing"
    assert_status "not a drive" 1
}

test_import_then_export_is_a_valid_drive()
{
    setup
    tc import-rekordbox "$drive" >/dev/null 2>&1
    local pl
    pl=$(ls "$LIB/pendrives" | head -n 1)
    run tc export "$pl" "$T/out"
    assert_status "export of the imported library" 0
    run tc verify "$T/out"
    assert_status "and it verifies" 0
    assert_contains "seven tracks" "checked 7 tracks, 1 playlists" "$err"
}

test_pdb_dump_and_diff()
{
    local pdb=$drive/PIONEER/rekordbox/export.pdb
    LIB=$T/lib
    run tc pdb dump "$pdb" --table tracks
    assert_status "dump" 0
    assert_eq "seven tracks" "7" "$(printf '%s\n' "$out" | wc -l)"
    assert_contains "a title" "Whistling Diesel" "$out"
    run tc pdb dump "$pdb" --pages
    assert_contains "pages" "header	pages=41" "$out"
    run tc pdb diff "$pdb" "$pdb"
    assert_status "same file" 0
    setup
    tc import-rekordbox "$drive" >/dev/null 2>&1
    tc export "$(ls "$LIB/pendrives" | head -n 1)" "$T/out" >/dev/null 2>&1
    run tc pdb diff "$pdb" "$T/out/PIONEER/rekordbox/export.pdb"
    assert_status "real and ours differ" 1
    assert_contains "the diff says where" "tracks" "$out"
    run tc pdb dump "$T/nothing"
    assert_status "missing file" 1
}

test_anlz_dump_and_diff()
{
    local f=$drive/PIONEER/USBANLZ/P004/00028148/ANLZ0000.EXT
    LIB=$T/lib
    run tc anlz dump "$f"
    assert_status "dump" 0
    assert_contains "tags" "tag	PQT2" "$out"
    assert_contains "path" "path	/Contents/Intapol/UK Dembow/" "$out"
    assert_contains "beats" "beats	count=521" "$out"
    run tc anlz dump "$f" --tag PWV5
    assert_contains "one tag" "tag	PWV5" "$out"
    assert_not_contains "only that" "PQT2" "$out"
    run tc anlz dump "$f" --tag PCOB --raw
    assert_contains "raw" "header-words" "$out"
    run tc anlz diff "$f" "$f"
    assert_status "same" 0
    run tc anlz diff "$f" "$drive/PIONEER/USBANLZ/P00C/0000D1E2/ANLZ0000.EXT"
    assert_status "different tracks" 1
}

test_unknown_bytes()
{
    setup
    tc import-rekordbox "$drive" >/dev/null 2>&1
    run tc --format tsv unknown-bytes
    assert_status "unknown-bytes" 0
    assert_contains "header" "track	file	tag	field	kind	value" "$out"
    assert_contains "the PQT2 word" "PQT2	header+0x2c	u32	0x" "$out"
    assert_contains "the cue markers" "PCP2	marker	u24	150" "$out"
    run tc --format jsonl unknown-bytes
    assert_contains "jsonl" '"tag":"PQTZ"' "$out"
}

run_tests
