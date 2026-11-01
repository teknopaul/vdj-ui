#!/bin/bash
#
# import, rm and check through the wrapper, with real (tiny) wav files made by ffmpeg
#
. "$(dirname "$0")/lib.sh"

if ! command -v ffmpeg >/dev/null; then
    echo "test_import.sh: ffmpeg not found, skipped"
    exit 0
fi

tc() { "$TRAKCRATE" --library "$LIB" "$@"; }

# $T/Music/Gig with a2.wav (tagged), a10.wav, b.wav (not audio), readme.txt; and an empty library
setup()
{
    LIB=$T/lib
    SRC=$T/Music/Gig
    tc init >/dev/null 2>&1
    make_wav "$SRC/a10.wav" 0.2 440 title="Ten" artist="Me" date=2020 track=3/12
    make_wav "$SRC/a2.wav" 0.2 660
    echo "not audio" > "$SRC/b.wav"
    echo text > "$SRC/readme.txt"
}

M() { printf '%s/track-master-copy/%s' "$LIB" "$1"; }

test_import_creates_tracks_and_playlist()
{
    setup
    run tc import "$SRC"
    assert_status "one file failed, the others were imported" 3
    assert_contains "summary" "imported 2, duplicates 0, unsupported files 1, failed 1" "$out"
    assert_contains "folder" "folder: Gig" "$out"
    assert_contains "failure reported on stderr" "b.wav" "$err"

    assert_file "audio copied as track.wav" "$(M Gig/a10)/track.wav"
    assert_eq "the source is untouched" "1" "$(ls "$SRC/a10.wav" | wc -l)"
    local meta
    meta=$(cat "$(M Gig/a10)/meta.txt")
    assert_contains "container" "container: wav" "$meta"
    assert_contains "duration" "duration_ms: 200" "$meta"
    assert_contains "sample rate" "sample_rate: 44100" "$meta"
    assert_contains "title tag" "title: Ten" "$meta"
    assert_contains "artist tag" "artist: Me" "$meta"
    assert_contains "year from the date tag" "year: 2020" "$meta"
    assert_contains "track number from 3/12" "track_number: 3" "$meta"
    assert_contains "original file name" "original_filename: a10.wav" "$meta"
    assert_contains "audio hash" "audio_sha256: $(sha256sum "$SRC/a10.wav" | cut -d' ' -f1)" "$meta"
    assert_contains "date added" "date_added: $(date -u +%Y-%m-%d)" "$meta"
    local source
    source=$(cat "$(M Gig/a10)/source.txt")
    assert_contains "provenance" "origin.beatgrid: generated" "$source"
    assert_contains "imported_from" "imported_from: $SRC/a10.wav" "$source"
    assert_contains "imported_at" "imported_at: 20" "$source"
    assert_no_file "the failed file made no track" "$(M Gig/b)"
    assert_eq "no temporary directories left" "" "$(find "$LIB" -name '.import.*')"

    run tc pendrive ls Gig
    assert_eq "natural order: a2 before a10" "$(printf '000_a2\tGig/a2\n001_a10\tGig/a10')" "$out"
}

test_import_twice()
{
    setup
    tc import "$SRC" >/dev/null 2>&1
    run tc import "$SRC"
    assert_status "playlist exists" 1
    assert_contains "says what to do" "--append" "$err"
    run tc import --append "$SRC"
    assert_contains "everything is a duplicate now" "imported 0, duplicates 2" "$out"
    run tc pendrive ls Gig
    assert_eq "append added nothing" "2" "$(printf '%s\n' "$out" | wc -l)"
    assert_eq "no second copy of the audio" "2" "$(ls "$(M Gig)" | wc -l)"

    run tc --yes import --replace "$SRC"
    assert_contains "replace works" "playlist: Gig" "$out"
    run tc import --replace "$SRC"
    assert_status "replace without --yes and no terminal" 1
}

test_import_options()
{
    setup
    run tc import --folder Mixes --playlist friday --order none --reverse "$SRC"
    run tc pendrive ls friday
    assert_eq "reverse" "$(printf '000_a10\tMixes/a10\n001_a2\tMixes/a2')" "$out"
    assert_file "--folder" "$(M Mixes/a2)/track.wav"

    run tc import --no-dedupe --no-playlist --folder Other "$SRC"
    assert_no_file "--no-playlist" "$LIB/playlists/Gig"
    assert_eq "tracks still imported" "2" "$(ls "$(M Other)" | wc -l)"

    run tc import --playlist bytitle --order title "$SRC"
    run tc pendrive ls bytitle
    assert_eq "order by title (a2 has none: its name); these are duplicates of the first import" "$(printf '000_a2\tMixes/a2\n001_a10\tMixes/a10')" "$out"

    run tc import --playlist x --order sideways "$SRC"
    assert_status "bad order" 2
    run tc import "$T/nonesuch"
    assert_status "not a directory" 1
}

test_import_dedupe_and_collisions()
{
    setup
    tc import --no-playlist "$SRC" >/dev/null 2>&1
    mkdir -p "$T/Other"
    cp "$SRC/a2.wav" "$T/Other/same-audio.wav"
    make_wav "$T/Other/a2.wav" 0.2 880
    run tc --format jsonl import --playlist other "$T/Other"
    assert_contains "same audio under another name is a duplicate" '"reason":"duplicate"' "$out"
    assert_file "the different audio with a taken name gets a suffix" "$(M Other/a2)/track.wav"
    make_wav "$T/Other2/a2.wav" 0.2 990
    tc import --no-playlist --folder Gig "$T/Other2" >/dev/null 2>&1
    assert_file "name collision in the same folder: a2 2" "$(M 'Gig/a2 2')/track.wav"

    run tc --no-dedupe import --no-playlist --folder Copy "$SRC"
    run tc import --no-dedupe --no-playlist --folder Copy2 "$SRC"
    assert_file "--no-dedupe imports the same audio again" "$(M Copy2/a2)/track.wav"
}

test_import_modes_and_dry_run()
{
    setup
    run tc -n import "$SRC"
    assert_no_file "dry run: no track" "$(M Gig)"
    assert_no_file "dry run: no playlist" "$LIB/playlists/Gig"
    assert_no_file "dry run: no lock left" "$LIB/.trakcrate-lock"

    tc import --link --no-playlist "$SRC" >/dev/null 2>&1
    assert_eq "--link makes a symlink to the source" "$SRC/a2.wav" "$(readlink "$(M Gig/a2)/track.wav")"

    make_wav "$T/Mv/m.wav" 0.2 300
    tc import --move --no-playlist "$T/Mv" >/dev/null 2>&1
    assert_file "--move imported it" "$(M Mv/m)/track.wav"
    assert_no_file "--move removed the source" "$T/Mv/m.wav"
}

test_import_recursive()
{
    setup
    make_wav "$SRC/sub/deep/c.wav" 0.2 500
    run tc import --recursive --playlist all "$SRC"
    assert_file "a subfolder becomes <folder> - <sub path>" "$(M 'Gig - sub - deep/c')/track.wav"
    run tc pendrive ls all
    assert_eq "one playlist, sorted by relative path" "$(printf '000_a2\tGig/a2\n001_a10\tGig/a10\n002_c\tGig - sub - deep/c')" "$out"
    run tc import --playlist top --no-dedupe --folder Flat "$SRC"
    assert_no_file "without --recursive the subfolder is not entered" "$(M Flat/c)"
}

test_import_names_are_made_safe()
{
    setup
    make_wav "$T/Weird/.hidden name .wav" 0.2 210
    run tc import --no-playlist "$T/Weird"
    assert_file "leading dot and trailing space trimmed" "$(M 'Weird/hidden name')/track.wav"
}

test_lock()
{
    setup
    printf '%s\n' "$$" > "$LIB/.trakcrate-lock"
    run tc import "$SRC"
    assert_status "a live process holds the lock" 4
    assert_contains "message" "library busy" "$err"
    assert_no_file "nothing was imported" "$(M Gig)"
    assert_file "the lock is not ours: left alone" "$LIB/.trakcrate-lock"

    printf '99999999\n' > "$LIB/.trakcrate-lock"
    run tc import --no-playlist "$SRC"
    assert_status "a stale lock is taken over" 3
    assert_no_file "the lock is released" "$LIB/.trakcrate-lock"
    run tc ls
    assert_status "readers never lock" 0
}

test_rm()
{
    setup
    tc import "$SRC" >/dev/null 2>&1
    tc pendrive new second >/dev/null 2>&1
    tc pendrive add second a10 a2 >/dev/null 2>&1
    run tc rm nonesuch a10
    assert_status "an unknown name stops everything" 1
    assert_file "nothing removed" "$(M Gig/a10)"

    run tc rm a10
    assert_status "in playlists: needs --yes" 1
    assert_contains "lists the playlists" "playlists/second" "$err"
    run tc --yes rm a10
    assert_status "rm" 0
    assert_no_file "removed from the library" "$(M Gig/a10)"
    assert_eq "moved to .trash/" "1" "$(find "$LIB/.trash" -name track.wav | wc -l)"
    run tc pendrive ls Gig
    assert_eq "link removed and renumbered" "$(printf '000_a2\tGig/a2')" "$out"
    run tc pendrive ls second
    assert_eq "from every playlist" "$(printf '000_a2\tGig/a2')" "$out"
    run tc check
    assert_status "the library is still valid" 0

    run tc --yes rm --purge a2
    assert_no_file "purge deletes" "$(M Gig/a2)"
    assert_eq "purge leaves nothing in .trash/" "1" "$(find "$LIB/.trash" -name track.wav | wc -l)"
    run tc -n rm Gig/a2
    assert_status "dry run of a missing track" 1
}

test_rm_without_playlists_needs_no_confirmation()
{
    setup
    tc import --no-playlist "$SRC" >/dev/null 2>&1
    run tc rm a2
    assert_status "a track in no playlist" 0
    assert_no_file "removed" "$(M Gig/a2)"
}

test_check()
{
    setup
    tc import "$SRC" >/dev/null 2>&1
    run tc check
    assert_status "a good library" 0
    assert_eq "no findings" "" "$out"

    cp "$(M Gig/a2)/track.wav" "$(M Gig/a2)/second.MP3"
    printf 'nonsense' > "$(M Gig/a10)/waveform.bin"
    ln -s ../../../track-master-copy/Gig/a2 "$LIB/playlists/Gig/symlinks/000_dup"
    ln -s ../../../track-master-copy/Gig/a2 "$LIB/playlists/Gig/symlinks/noprefix"
    ln -s ../../../track-master-copy/Gone/x "$LIB/playlists/Gig/symlinks/009_gone"
    mkdir -p "$(M Gig/empty)"
    run tc --format tsv check
    assert_status "errors found" 1
    assert_contains "header" "where	severity	code	message" "$out"
    assert_contains "two audio files" "Gig/a2	error	multiple-audio" "$out"
    assert_contains "bad waveform" "Gig/a10/waveform.bin	error	bad-waveform" "$out"
    assert_contains "duplicate prefix" "error	duplicate-prefix" "$out"
    assert_contains "no prefix" "noprefix	error	bad-prefix" "$out"
    assert_contains "dangling link" "009_gone	warning	dangling-link" "$out"
    assert_contains "no audio" "Gig/empty	warning	no-audio" "$out"
    run tc --format jsonl check
    assert_contains "jsonl" '"severity":"error","code":"multiple-audio"' "$out"
    run tc --format json check
    assert_eq "json array" "[" "${out:0:1}"

    printf 'TCWV\001\000\000\000' > "$(M Gig/a10)/waveform.bin"
    rm -rf "$(M Gig/a2)/second.MP3" "$LIB/playlists/Gig/symlinks/000_dup" "$LIB/playlists/Gig/symlinks/noprefix"
    run tc check
    assert_status "only warnings left" 0
}

run_tests
