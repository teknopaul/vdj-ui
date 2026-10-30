#!/bin/bash
#
# sync: bring an exported drive up to date (rsync semantics), on the small analysed library of the export tests
#
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/export_fixture.sh"

command -v python3 >/dev/null || { echo "test_sync.sh: python3 not found, skipped"; exit 0; }
command -v rsync >/dev/null || { echo "test_sync.sh: rsync not found, skipped"; exit 0; }

tc() { "$TRAKCRATE" --library "$LIB" "$@"; }

setup()
{
    LIB=$T/lib
    export_fixture "$LIB"
    D=$T/drive
}

test_sync_into_an_empty_folder_makes_a_valid_drive()
{
    setup
    run tc sync A "$D"
    assert_status "sync" 0
    assert_contains "summary" "synced 2 tracks in 1 playlists" "$out"
    assert_file "audio" "$D/Contents/Alpha/Debut/t1.wav"
    assert_file "database" "$D/PIONEER/rekordbox/export.pdb"
    assert_no_file "no SHA256SUMS: sync does not read the audio" "$D/SHA256SUMS"
    assert_file "manifest" "$D/trakcrate-manifest.txt"
    run tc verify "$D"
    assert_status "the drive verifies" 0
    assert_eq "the audio is a real file, not a link" "" "$(find "$D" -type l)"
}

test_sync_changes_only_what_differs()
{
    setup
    tc sync A "$D" >/dev/null 2>&1
    run tc --format tsv sync A "$D"
    assert_status "second sync" 0
    assert_contains "nothing copied" "	0	0	0	ok" "$out"
    tc pendrive add A t3 >/dev/null 2>&1
    run tc sync A "$D"
    assert_status "after adding a track" 0
    assert_contains "three tracks" "synced 3 tracks" "$out"
    assert_file "the new audio" "$D/Contents/Beta/UnknownAlbum/t3.wav"
    run tc verify "$D"
    assert_status "still valid" 0
    assert_contains "three tracks on the drive" "checked 3 tracks" "$err"
}

test_sync_after_an_export_replaces_the_checksums()
{
    setup
    tc export A "$D" >/dev/null 2>&1
    assert_file "export wrote SHA256SUMS" "$D/SHA256SUMS"
    tc pendrive add A t3 >/dev/null 2>&1
    run tc sync A "$D"
    assert_status "sync" 0
    assert_no_file "the stale SHA256SUMS is gone" "$D/SHA256SUMS"
    run tc verify "$D"
    assert_status "valid" 0
}

test_orphans_are_kept_unless_asked()
{
    setup
    tc sync A B "$D" >/dev/null 2>&1
    run tc sync A "$D"
    assert_status "sync A only" 0
    assert_file "the audio of t3 is still there" "$D/Contents/Beta/UnknownAlbum/t3.wav"
    run tc --yes sync --delete-orphans A "$D"
    assert_status "with --delete-orphans" 0
    assert_no_file "removed" "$D/Contents/Beta/UnknownAlbum/t3.wav"
    assert_file "t1 stays" "$D/Contents/Alpha/Debut/t1.wav"
    assert_contains "listed" "UnknownAlbum/t3.wav" "$err"
    run tc verify "$D"
    assert_status "valid" 0
}

test_delete_orphans_needs_confirmation()
{
    setup
    tc sync A B "$D" >/dev/null 2>&1
    run tc sync --delete-orphans A "$D"
    assert_status "no terminal, no --yes" 1
    assert_file "nothing removed" "$D/Contents/Beta/UnknownAlbum/t3.wav"
}

test_unrelated_files_on_the_drive_are_never_removed()
{
    setup
    tc sync A "$D" >/dev/null 2>&1
    echo keep > "$D/notes.txt"
    mkdir -p "$D/Music"
    echo keep > "$D/Music/mine.mp3"
    tc --yes sync --delete-orphans A "$D" >/dev/null 2>&1
    assert_file "a file in the root" "$D/notes.txt"
    assert_file "another folder" "$D/Music/mine.mp3"
}

test_dry_run_changes_nothing()
{
    setup
    run tc -n sync A "$D"
    assert_status "dry run" 0
    assert_contains "report" "would copy" "$out"
    assert_eq "the target holds nothing" "" "$(ls -A "$D" 2>/dev/null)"
}

test_sync_errors()
{
    setup
    run tc sync A
    assert_status "needs a target" 2
    run tc sync Nope "$D"
    assert_status "unknown playlist" 1
    echo x > "$T/file"
    run tc sync A "$T/file"
    assert_status "target is a file" 1
}

run_tests
