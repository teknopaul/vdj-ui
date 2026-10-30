#!/bin/bash
#
# export and verify through the wrapper: a drive made from a small analysed library, read back with libcdj's parsers
#
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/export_fixture.sh"

command -v python3 >/dev/null || { echo "test_export.sh: python3 not found, skipped"; exit 0; }

tc() { "$TRAKCRATE" --library "$LIB" "$@"; }

setup()
{
    LIB=$T/lib
    export_fixture "$LIB"
    D=$T/drive
}

test_export_builds_a_drive()
{
    setup
    run tc export A B "$D"
    assert_status "export" 0
    assert_contains "summary" "exported 3 tracks in 2 playlists" "$out"
    assert_contains "verified" "verified" "$out"
    assert_file "export.pdb" "$D/PIONEER/rekordbox/export.pdb"
    assert_file "devsetting" "$D/PIONEER/DEVSETTING.DAT"
    assert_file "mysetting" "$D/PIONEER/MYSETTING.DAT"
    assert_file "sha256sums" "$D/SHA256SUMS"
    assert_file "manifest" "$D/trakcrate-manifest.txt"
    assert_file "audio of t1" "$D/Contents/Alpha/Debut/t1.wav"
    assert_file "audio of t3" "$D/Contents/Beta/UnknownAlbum/t3.wav"
    assert_eq "analysis files: 3 tracks x 2" "6" "$(find "$D/PIONEER/USBANLZ" -name 'ANLZ0000.*' | wc -l)"
    assert_eq "one artwork file for the shared cover" "1" "$(find "$D/PIONEER/Artwork" -name '*.jpg' | wc -l)"
    assert_file "the artwork is a001" "$D/PIONEER/Artwork/00001/a1.jpg"
    assert_eq "the artwork is a JPEG" "ffd8ff" "$(head -c 3 "$D/PIONEER/Artwork/00001/a1.jpg" | od -An -tx1 | tr -d ' \n')"
    assert_contains "manifest lists the playlists" '"A" "B"' "$(cat "$D/trakcrate-manifest.txt")"
    assert_contains "manifest says full" "variant: full" "$(cat "$D/trakcrate-manifest.txt")"
    assert_eq "no staging left behind" "" "$(find /dev/shm -maxdepth 1 -name 'trakcrate-export.*' 2>/dev/null)"
    (cd "$D" && sha256sum -c SHA256SUMS >/dev/null 2>&1)
    assert_status "sha256sum agrees with SHA256SUMS" 0
}

test_verify_accepts_the_export()
{
    setup
    tc export A B "$D" >/dev/null 2>&1
    run tc verify "$D"
    assert_status "verify" 0
    assert_contains "summary on stderr" "checked 3 tracks, 2 playlists" "$err"
    run tc --format tsv verify "$D"
    assert_status "verify tsv" 0
    assert_eq "tsv: header only" "where	severity	code	message" "$out"
}

test_verify_finds_damage()
{
    setup
    tc export A "$D" >/dev/null 2>&1
    rm "$D/Contents/Alpha/Debut/t2.wav"
    find "$D/PIONEER/USBANLZ" -name ANLZ0000.EXT | head -n 1 | xargs rm
    printf 'x' >> "$D/PIONEER/DEVSETTING.DAT"
    run tc --format tsv verify "$D"
    assert_status "verify fails" 1
    assert_contains "missing audio" "audio-missing" "$out"
    assert_contains "missing analysis file" "anlz-missing" "$out"
    assert_contains "bad settings" "settings-bad" "$out"
    assert_contains "checksum of the missing file" "checksum-missing" "$out"
    run tc --format jsonl verify "$D"
    assert_contains "jsonl" '"code":"audio-missing"' "$out"
}

test_verify_without_a_database()
{
    setup
    mkdir -p "$T/empty"
    run tc --format tsv verify "$T/empty"
    assert_status "no export.pdb" 1
    assert_contains "pdb-missing" "pdb-missing" "$out"
}

test_export_uses_the_rekordbox_layout()
{
    setup
    make_track "$LIB" H u1 "title: One" "artist: K.O." "album: Hijack: The Remixes" "duration_ms: 12000"
    make_track "$LIB" H u2 "title: Two" "artist: k.o." "album: Hijack: The Remixes" "duration_ms: 12000"
    make_track "$LIB" H u3 "title: Three" "artist: Gamma" "album: Joy Squad / Shellshock - VTSS & Dubyshkin Remixes" "duration_ms: 12000"
    for n in u1 u2 u3; do
        rm -f "$LIB/track-master-copy/H/$n/track.mp3"
        python3 -I "$TC_TEST_DIR/gen_beats.py" "$LIB/track-master-copy/H/$n/track.wav" 120 12 stereo 0.1
    done
    "$TRAKCRATE" --library "$LIB" analyse --all >/dev/null 2>&1
    "$TRAKCRATE" --library "$LIB" pendrive new R >/dev/null 2>&1
    "$TRAKCRATE" --library "$LIB" pendrive add R u1 u2 u3 >/dev/null 2>&1
    run tc export R "$D"
    assert_status "exported" 0
    assert_file "trailing dot, colon" "$D/Contents/K.O_/Hijack_ The Remixes/u1.wav"
    assert_file "same artist in another letter case shares the folder" "$D/Contents/K.O_/Hijack_ The Remixes/u2.wav"
    assert_file "slash, cut at 48 characters" "$D/Contents/Gamma/Joy Squad _ Shellshock - VTSS & Dubyshkin Remixe/u3.wav"
    assert_file "the library is not touched" "$LIB/track-master-copy/H/u1/track.wav"
}

test_export_always_writes_the_support_files()
{
    setup
    run tc export A "$D"
    assert_status "exported" 0
    assert_eq "exportExt.pdb" 73728 "$(stat -c %s "$D/PIONEER/rekordbox/exportExt.pdb")"
    assert_eq "RBFLTR.DAT" 232 "$(stat -c %s "$D/PIONEER/rekordbox/RBFLTR.DAT")"
    assert_eq "djprofile.nxs" 160 "$(stat -c %s "$D/PIONEER/djprofile.nxs")"
    assert_eq "DEVSETTING.DAT" 140 "$(stat -c %s "$D/PIONEER/DEVSETTING.DAT")"
    assert_eq "MYSETTING.DAT" 148 "$(stat -c %s "$D/PIONEER/MYSETTING.DAT")"
    cmp -s "$D/PIONEER/rekordbox/exportExt.pdb" "$(dirname "$0")/../c/data/user-settings/exportExt.pdb" || fail_assert "not the real file"
}

test_export_leaves_other_files_on_the_stick_alone()
{
    setup
    mkdir -p "$D/.Trash-1000/files"
    echo keep > "$D/mine.txt"
    run tc export A "$D"
    assert_status "other files do not block" 0
    assert_eq "kept" "keep" "$(cat "$D/mine.txt")"
    [ -d "$D/.Trash-1000/files" ] || fail_assert "trash folder was removed"
    [ -f "$D/PIONEER/rekordbox/export.pdb" ] || fail_assert "no export.pdb"
}

test_export_refuses_a_destination_that_has_our_names()
{
    setup
    mkdir -p "$D/pioneer"
    echo keep > "$D/mine.txt"
    run tc export A "$D"
    assert_status "refused" 1
    assert_contains "says why" "already has" "$err"
    assert_eq "nothing changed" "mine.txt pioneer" "$(ls "$D" | tr '\n' ' ' | sed 's/ $//')"
}

test_export_refuses_tracks_that_are_not_analysed()
{
    setup
    rm "$LIB/track-master-copy/F/t2/waveform.bin"
    run tc export A "$D"
    assert_status "refused" 1
    assert_contains "names the track" "not analysed: t2" "$err"
    assert_no_file "nothing was created" "$D"
    run tc export --allow-unanalysed A "$D"
    assert_status "allowed" 0
    assert_contains "warns" "flat waveform" "$err"
    assert_contains "variant recorded" "unanalysed tracks allowed" "$(cat "$D/trakcrate-manifest.txt")"
    run tc verify "$D"
    assert_status "the drive is still valid" 0
}

test_export_dry_run_writes_nothing()
{
    setup
    run tc export -n A B "$D"
    assert_status "dry run" 0
    assert_contains "report" "would export 3 tracks" "$out"
    assert_no_file "no destination" "$D"
}

test_export_errors()
{
    setup
    run tc export A
    assert_status "usage: needs a destination" 2
    run tc export Nope "$D"
    assert_status "unknown playlist" 1
    assert_contains "names it" "no such playlist: Nope" "$err"
    run tc export C "$D"
    assert_status "an empty playlist" 1
    assert_contains "says so" "empty" "$err"
}

test_export_options()
{
    setup
    run tc export --exclude-art --no-verify --no-manifest A "$D"
    assert_status "options" 0
    assert_no_file "no artwork" "$D/PIONEER/Artwork"
    assert_no_file "no sha256sums" "$D/SHA256SUMS"
    assert_not_contains "not verified" "verified" "$out"
    run tc --format json export A "$T/d2"
    assert_status "json" 0
    assert_contains "json record" '"playlists":1,"tracks":2' "$out"
    assert_contains "json verified" '"verified":true' "$out"
    run tc --format tsv export A "$T/d3"
    assert_contains "tsv header" "dest	playlists	tracks	files	bytes	verified	status" "$out"
}

test_export_progress_events()
{
    setup
    tc export --progress-fd 3 A "$D" 3> "$T/events" >/dev/null 2>&1
    assert_contains "start" '"event":"start","command":"export"' "$(cat "$T/events")"
    assert_contains "stage phase" '"name":"stage"' "$(cat "$T/events")"
    assert_contains "copy phase" '"name":"copy"' "$(cat "$T/events")"
    assert_contains "verify phase" '"name":"verify"' "$(cat "$T/events")"
    assert_contains "done" '"event":"done","ok":2' "$(cat "$T/events")"
}

test_export_is_deterministic()
{
    setup
    tc export A B "$T/d1" >/dev/null 2>&1
    tc export A B "$T/d2" >/dev/null 2>&1
    run diff -r "$T/d1" "$T/d2"
    assert_status "two exports of the same library are identical" 0
}

run_tests
