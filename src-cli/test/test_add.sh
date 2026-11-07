#!/bin/bash
#
# add-to-drive through the wrapper: a playlist added to a stick made by `trakcrate export` (a database the command did not build in this run)
#
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/export_fixture.sh"

command -v python3 >/dev/null || { echo "test_add.sh: python3 not found, skipped"; exit 0; }

tc() { "$TRAKCRATE" --library "$LIB" "$@"; }

# a checksum of every file below DIR with its name, to see that nothing changed
tree_sum() { (cd "$1" && find . -type f -print0 | sort -z | xargs -0 cksum 2>/dev/null); }

# the stick holds playlist A (t1, t2); B (t3, t2) and E (t1, t2 again) are in the library to be added
setup()
{
    LIB=$T/lib
    export_fixture "$LIB"
    tc pendrive new E >/dev/null 2>&1
    tc pendrive add E t1 t2 >/dev/null 2>&1
    D=$T/stick
    BAK=$T/bak
    tc export --no-verify --no-manifest A "$D" >/dev/null 2>&1      # no SHA256SUMS: adding does not rewrite it, and verify would find it stale
}

add() { tc add-to-drive --backup-dir "$BAK" "$@"; }

# the playlists of the stick as "NAME: track paths ..." lines, read through the helper
lists_of()
{
    "$TC_HELPER" rbdrive "$1" | python3 -I -c '
import sys
t = {}; p = {}; e = {}
for line in sys.stdin:
    f = line.rstrip("\n").split("\t")
    if f[0] == "T": t[f[1]] = f[2]
    elif f[0] == "P": p[f[1]] = f[2]; e[f[1]] = []
    elif f[0] == "E": e[f[1]].append(t[f[2]])
for i in sorted(p): print(p[i] + ": " + " ".join(e[i]))'
}

test_add_a_playlist_with_a_track_the_stick_has()
{
    setup
    local libsum=$(tree_sum "$LIB") before=$(tree_sum "$D")
    local settings; settings=$(cd "$D/PIONEER" && cksum DEVSETTING.DAT MYSETTING.DAT djprofile.nxs rekordbox/RBFLTR.DAT rekordbox/exportExt.pdb USBANLZ/USBMNG.DAT)
    cp "$D/PIONEER/rekordbox/export.pdb" "$T/old.pdb"
    run add B "$D"
    assert_status "add" 0
    assert_contains "summary" 'added playlist "B"' "$out"
    assert_contains "one new, one already there" "1 new tracks, 1 already on the pendrive" "$out"
    assert_file "audio of t3" "$D/Contents/Beta/UnknownAlbum/t3.wav"
    assert_eq "analysis files: 3 tracks x 2" "6" "$(find "$D/PIONEER/USBANLZ" -name 'ANLZ0000.*' | wc -l)"
    assert_eq "no temporary files left" "" "$(find "$D" -name '*.trakcrate-tmp' -o -name 'export.pdb.new')"
    assert_file "export.pdb.bak is the old database" "$D/PIONEER/rekordbox/export.pdb.bak"
    cmp -s "$D/PIONEER/rekordbox/export.pdb.bak" "$T/old.pdb"
    assert_status "export.pdb.bak holds the old bytes" 0
    local copy; copy=$(ls "$BAK"/rekordbox-edit-backup-export.*.pdb)
    assert_eq "one copy in the backup folder" "1" "$(echo "$copy" | wc -l)"
    cmp -s "$copy" "$T/old.pdb"
    assert_status "the copy holds the old bytes" 0
    assert_eq "the library is untouched" "$libsum" "$(tree_sum "$LIB")"
    assert_eq "settings files and USBMNG.DAT are untouched" "$settings" "$(cd "$D/PIONEER" && cksum DEVSETTING.DAT MYSETTING.DAT djprofile.nxs rekordbox/RBFLTR.DAT rekordbox/exportExt.pdb USBANLZ/USBMNG.DAT)"
    local files_before; files_before=$(echo "$before" | grep -v 'export.pdb$' | grep -v 'SHA256SUMS\|manifest')
    local files_after; files_after=$(tree_sum "$D" | grep -F "$(echo "$files_before" | awk '{print $3}')" | grep -v 'export.pdb')
    assert_eq "files the stick had are byte identical" "$(echo "$files_before" | sort)" "$(echo "$files_after" | sort)"
    run tc verify "$D"
    assert_status "verify" 0
    assert_contains "verify counts" "checked 3 tracks, 2 playlists" "$err"
    local lists; lists=$(lists_of "$D")
    assert_contains "B names t3 then the t2 of A" "Contents/Beta/UnknownAlbum/t3.wav /Contents/Alpha/Debut/t2.wav" "$lists"
}

test_a_playlist_of_tracks_the_stick_has_writes_no_files()
{
    setup
    local files_before; files_before=$(cd "$D" && find . -type f ! -name 'export.pdb*' | sort)
    run add E "$D"
    assert_status "add" 0
    assert_contains "nothing new" "0 new tracks, 2 already on the pendrive" "$out"
    assert_eq "no file added" "$files_before" "$(cd "$D" && find . -type f ! -name 'export.pdb*' | sort)"
    run tc verify "$D"
    assert_status "verify" 0
    run add B "$D"
    assert_status "a second add" 0
    assert_file "export.pdb.bak.2 from the rotation" "$D/PIONEER/rekordbox/export.pdb.bak.2"
}

test_refusals_change_nothing()
{
    setup
    local before; before=$(tree_sum "$D")
    run add A "$D"
    assert_status "playlist name already there" 1
    assert_contains "names the playlist" 'already has a playlist called "A"' "$err"
    tc pendrive new a >/dev/null 2>&1
    tc pendrive add a t3 >/dev/null 2>&1
    run add a "$D"
    assert_status "same name ignoring case" 1
    touch "$D/PIONEER/rekordbox/exportLibrary.db"
    run add B "$D"
    assert_status "exportLibrary.db" 1
    assert_contains "says why" "exportLibrary.db" "$err"
    rm "$D/PIONEER/rekordbox/exportLibrary.db"
    run add B "$T/nowhere"
    assert_status "no such pendrive" 1
    mkdir -p "$T/empty"
    run add B "$T/empty"
    assert_status "no export.pdb" 1
    assert_contains "says why" "no PIONEER/rekordbox/export.pdb" "$err"
    cp "$D/PIONEER/rekordbox/export.pdb" "$T/good.pdb"
    head -c 100000 "$T/good.pdb" > "$D/PIONEER/rekordbox/export.pdb"
    run add B "$D"
    assert_status "a damaged database" 1
    assert_contains "says why" "cannot be added to (nothing was changed)" "$err"
    cp "$T/good.pdb" "$D/PIONEER/rekordbox/export.pdb"
    assert_eq "the stick is unchanged" "$before" "$(tree_sum "$D")"
    assert_eq "no backup copy was made by a refusal before the copy" "0" "$(ls "$BAK" 2>/dev/null | wc -l)"
}

test_unanalysed_tracks_are_refused_unless_allowed()
{
    setup
    make_track "$LIB" F t4 "title: Fourth" "artist: Alpha" "album: Debut" "duration_ms: 12000"
    python3 -I "$TC_TEST_DIR/gen_beats.py" "$LIB/track-master-copy/F/t4/track.wav" 120 12 mono 0.2
    rm -f "$LIB/track-master-copy/F/t4/track.mp3"
    tc pendrive new D >/dev/null 2>&1
    tc pendrive add D t4 >/dev/null 2>&1
    local before; before=$(tree_sum "$D")
    run add D "$D"
    assert_status "unanalysed" 1
    assert_contains "says so" "not analysed" "$err"
    assert_eq "the stick is unchanged" "$before" "$(tree_sum "$D")"
    run add --allow-unanalysed D "$D"
    assert_status "allowed" 0
    assert_contains "warns" "not analysed" "$err"
    run tc verify "$D"
    assert_status "verify" 0
}

test_dry_run_changes_nothing()
{
    setup
    local before; before=$(tree_sum "$D")
    run add -n B "$D"
    assert_status "dry run" 0
    assert_contains "says what it would do" "would add playlist \"B\"" "$out"
    assert_eq "the stick is unchanged" "$before" "$(tree_sum "$D")"
    assert_eq "no backup copy" "0" "$(ls "$BAK" 2>/dev/null | wc -l)"
    assert_eq "no staging left behind" "" "$(find /dev/shm -maxdepth 1 -name 'trakcrate-add.*' 2>/dev/null)"
}

test_formats()
{
    setup
    run tc --format json add-to-drive --backup-dir "$BAK" B "$D"
    assert_status "json" 0
    assert_contains "json record" '"playlist":"B","new":1,"existing":1' "$out"
}

test_a_stick_pulled_at_any_step_still_has_a_loadable_database()
{
    local step
    for step in backup planned audio analysis database switch; do
        setup
        local old; old=$(cksum < "$D/PIONEER/rekordbox/export.pdb")
        run add --stop-at "$step" B "$D"
        assert_status "stopped at $step" 1
        assert_eq "$step: export.pdb is the old database" "$old" "$(cksum < "$D/PIONEER/rekordbox/export.pdb")"
        run tc verify "$D"
        assert_status "$step: verify passes" 0
        rm -rf "$T/lib" "$D" "$BAK"
    done
}

test_a_stick_pulled_between_the_renames_is_repaired_by_a_rename()
{
    setup
    run add --stop-at between-renames B "$D"
    assert_status "stopped between the renames" 1
    assert_no_file "no export.pdb" "$D/PIONEER/rekordbox/export.pdb"
    assert_file "export.pdb.bak" "$D/PIONEER/rekordbox/export.pdb.bak"
    assert_file "export.pdb.new" "$D/PIONEER/rekordbox/export.pdb.new"
    mv "$D/PIONEER/rekordbox/export.pdb.new" "$D/PIONEER/rekordbox/export.pdb"
    run tc verify "$D"
    assert_status "verify after the repair" 0
    assert_contains "verify counts" "checked 3 tracks, 2 playlists" "$err"
}

test_an_error_while_copying_removes_what_was_added()
{
    setup
    [ "$(id -u)" != 0 ] || { echo "    (skipped as root)"; return; }
    local before; before=$(tree_sum "$D")
    chmod a-w "$D/Contents"
    run add B "$D"
    chmod u+w "$D/Contents"
    assert_status "copy fails" 1
    assert_eq "the stick is unchanged" "$before" "$(tree_sum "$D")"
    assert_no_file "no export.pdb.new" "$D/PIONEER/rekordbox/export.pdb.new"
    run tc verify "$D"
    assert_status "verify" 0
}

run_tests
