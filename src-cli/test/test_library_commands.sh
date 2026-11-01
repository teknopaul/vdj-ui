#!/bin/bash
#
# init, ls, info, set and pendrive, through the wrapper, on throw-away libraries (TRAKCRATE-FORMAT.md, cli-interface.md)
#
. "$(dirname "$0")/lib.sh"

tc() { "$TRAKCRATE" --library "$LIB" "$@"; }

setup_library()
{
    LIB=$T/lib
    tc init >/dev/null 2>&1
    make_track "$LIB" House "Track One" 'title: One' 'artist: Alice' 'rating: 4' 'duration_ms: 61000'
    make_track "$LIB" House "Track 10"
    make_track "$LIB" Acid "t1" '# a comment' 'title: Say "hi"	\ there'
    printf 'bpm: 128.00\nfirst_beat_us: 100\n' > "$LIB/track-master-copy/House/Track One/beatgrid.txt"
    : > "$LIB/track-master-copy/House/Track One/waveform.bin"
}

test_init()
{
    LIB=$T/new/lib
    run tc init
    assert_status "init" 0
    assert_eq "stdout is the library" "$LIB" "$out"
    assert_file "track-master-copy" "$LIB/track-master-copy"
    assert_file "playlists" "$LIB/playlists"
    assert_file "user-settings" "$LIB/user-settings"
    : > "$LIB/track-master-copy/keep"
    run tc init
    assert_status "init again" 0
    assert_file "an existing library is left alone" "$LIB/track-master-copy/keep"
    run tc --format jsonl init "$T/other"
    assert_eq "jsonl" "{\"path\":\"$T/other\",\"status\":\"created\"}" "$out"
    run tc -n init "$T/dry"
    assert_no_file "dry run writes nothing" "$T/dry"
    printf x > "$T/afile"
    run tc init "$T/afile"
    assert_status "a file in the way" 1
}

test_commands_need_a_library()
{
    run "$TRAKCRATE" --library "$T/nothing" ls
    assert_status "ls without a library" 1
    assert_contains "says what to do" "trakcrate init" "$err"
}

test_ls()
{
    setup_library
    run tc ls
    assert_status "ls" 0
    assert_eq "folder/track, in name order" "$(printf 'Acid/t1\nHouse/Track 10\nHouse/Track One')" "$out"
    run tc ls one
    assert_eq "a word matches anywhere, ignoring case" "House/Track One" "$out"
    run tc ls 'acid/*'
    assert_eq "a glob matches the whole name" "Acid/t1" "$out"
    run tc ls 'track*'
    assert_eq "a glob that does not match the whole name" "" "$out"
    run tc ls -l one
    assert_contains "long: title" "One" "$out"
    assert_contains "long: bpm" "128.00" "$out"
    assert_contains "long: analysed" "analysed" "$out"
}

test_ls_formats()
{
    setup_library
    run tc --format paths ls house
    assert_eq "paths" "$(printf '%s\n%s' "$LIB/track-master-copy/House/Track 10" "$LIB/track-master-copy/House/Track One")" "$out"
    assert_eq "paths, NUL separated" "$LIB/track-master-copy/Acid/t1" "$(tc --format paths ls -0 acid | tr '\0' '\n')"
    assert_eq "NUL separated output has no newline" "1" "$(tc --format paths ls -0 acid | tr -cd '\0' | wc -c)"

    run tc --format tsv ls
    assert_eq "tsv header" "path	folder	track	title	artist	album	genre	key	rating	bpm	duration_ms	container	audio_sha256	analysed	origin_meta	origin_beatgrid	origin_cues	origin_waveform	origin_artwork" "$(printf '%s\n' "$out" | head -n 1)"
    assert_eq "tsv: header and three tracks" "4" "$(printf '%s\n' "$out" | wc -l)"
    local row
    row=$(printf '%s\n' "$out" | grep "Track One")
    assert_contains "tsv fields" "$(printf 'House\tTrack One\tOne\tAlice\t\t\t\t4\t128.00\t61000\t\t\t1\tgenerated')" "$row"
    row=$(printf '%s\n' "$out" | grep "/Acid/")
    assert_contains "tsv escapes a tab and a backslash inside a value" 'Say "hi"\t\\ there' "$row"

    run tc --format jsonl ls one
    assert_eq "jsonl" "{\"path\":\"$LIB/track-master-copy/House/Track One\",\"folder\":\"House\",\"track\":\"Track One\",\"title\":\"One\",\"artist\":\"Alice\",\"rating\":4,\"bpm\":128.00,\"duration_ms\":61000,\"analysed\":true,\"origin_meta\":\"generated\",\"origin_beatgrid\":\"generated\",\"origin_cues\":\"generated\",\"origin_waveform\":\"generated\",\"origin_artwork\":\"generated\"}" "$out"
    run tc --format jsonl ls acid
    assert_contains "jsonl escapes quotes, tabs and backslashes" '"title":"Say \"hi\"\t\\ there"' "$out"
    run tc --format json ls house
    assert_eq "json is one array" "[" "${out:0:1}"
    assert_eq "json ends the array" "]" "${out: -1}"
    assert_eq "json has both tracks" "2" "$(printf '%s' "$out" | grep -o '"folder"' | wc -l)"
    run tc --format json ls nonesuch
    assert_eq "json, no match: an empty array" "[]" "$out"
}

test_info()
{
    setup_library
    tc pendrive new gig >/dev/null 2>&1
    tc pendrive add gig "Track One" >/dev/null 2>&1
    run tc info "House/Track One"
    assert_status "info" 0
    assert_contains "path" "path: $LIB/track-master-copy/House/Track One" "$out"
    assert_contains "meta" "artist: Alice" "$out"
    assert_contains "beat grid" "bpm: 128.00" "$out"
    assert_contains "files" "$(printf 'waveform.bin\t0')" "$out"
    assert_contains "playlists" "$(printf 'gig\t000_Track One')" "$out"
    run tc info "Track One"
    assert_status "info by a unique name" 0
    run tc info t1
    assert_contains "info by the name of another track" "Say" "$out"
    run tc --format jsonl info "Track One"
    assert_contains "jsonl is the ls record" '"title":"One"' "$out"
    run tc info nonesuch
    assert_status "unknown track" 1
    run tc info ../../etc
    assert_status "no way out of the library" 1
    run tc info /tmp
    assert_status "a directory outside the library" 1
}

test_ambiguous_track_name()
{
    setup_library
    make_track "$LIB" Other "Track One"
    run tc info "Track One"
    assert_status "ambiguous name" 1
    assert_contains "lists the candidates" "House/Track One" "$err"
    run tc info "Other/Track One"
    assert_status "folder/track is not ambiguous" 0
}

test_set()
{
    setup_library
    local meta="$LIB/track-master-copy/House/Track One/meta.txt"
    run tc set "Track One" rating=5 'comment=two
lines \ here' year=2020
    assert_status "set" 0
    assert_contains "rating replaced in place" "$(printf 'title: One\nartist: Alice\nrating: 5\nduration_ms: 61000')" "$(cat "$meta")"
    assert_contains "multi-line and backslash escaped" 'comment: two\nlines \\ here' "$(cat "$meta")"
    assert_contains "year appended" "year: 2020" "$(cat "$meta")"
    assert_eq "provenance recorded" "origin.meta: edited" "$(cat "$LIB/track-master-copy/House/Track One/source.txt")"

    run tc set "Track One" artist=
    assert_not_contains "an empty value removes the key" "artist" "$(cat "$meta")"

    run tc set t1 rating=3
    assert_contains "comments survive" "# a comment" "$(cat "$LIB/track-master-copy/Acid/t1/meta.txt")"
    run tc set "Track 10" title=New
    assert_eq "a track without meta.txt gets one" "title: New" "$(cat "$LIB/track-master-copy/House/Track 10/meta.txt")"

    assert_eq "no temporary files left behind" "" "$(find "$LIB" -name '.*.tmp.*')"
}

test_set_validation()
{
    setup_library
    local meta="$LIB/track-master-copy/House/Track One/meta.txt"
    local before
    before=$(cat "$meta")
    run tc set "Track One" rating=9
    assert_status "rating out of range" 2
    run tc set "Track One" year=soon
    assert_status "year not a number" 2
    run tc set "Track One" duration_ms=5
    assert_status "measured field needs --force" 2
    run tc set "Track One" 'Bad Key=1'
    assert_status "bad key" 2
    run tc set "Track One" nonsense
    assert_status "not FIELD=VALUE" 2
    run tc set "Track One" title=ok rating=9
    assert_status "one bad assignment stops all" 2
    assert_eq "nothing changed" "$before" "$(cat "$meta")"
    run tc set --force "Track One" duration_ms=5
    assert_status "--force" 0
    run tc -n set "Track One" title=dry
    assert_not_contains "dry run changes nothing" "dry" "$(cat "$meta")"
}

test_pendrive()
{
    setup_library
    run tc pendrive new gig
    assert_status "new" 0
    assert_file "playlist created" "$LIB/playlists/gig/symlinks"
    run tc pendrive new "a/b"
    assert_status "bad playlist name" 2
    run tc pendrive add gig "Track One" t1
    assert_status "add" 0
    assert_eq "numbered links, relative" "$(printf '000_Track One -> ../../../track-master-copy/House/Track One\n001_t1 -> ../../../track-master-copy/Acid/t1')" \
        "$(cd "$LIB/playlists/gig/symlinks" && for l in *; do printf '%s -> %s\n' "$l" "$(readlink "$l")"; done)"
    run tc pendrive add --at 0 gig "Track 10"
    run tc pendrive ls gig
    assert_eq "inserted at the front, renumbered" "$(printf '000_Track 10\tHouse/Track 10\n001_Track One\tHouse/Track One\n002_t1\tAcid/t1')" "$out"
    run tc pendrive add gig t1
    assert_contains "a duplicate is skipped" "already in gig" "$err"
    assert_eq "still three" "3" "$(ls "$LIB/playlists/gig/symlinks" | wc -l)"
    run tc pendrive add nonesuch t1
    assert_status "no such playlist" 1
    run tc pendrive add gig nonesuch
    assert_status "no such track" 1

    run tc --format jsonl pendrive ls gig
    assert_contains "jsonl" '"position":1,"path":"'"$LIB"'/track-master-copy/House/Track One","link":"001_Track One","dangling":false' "$out"
    run tc pendrive ls
    assert_eq "without a name: the playlists" "$(printf 'gig\t3')" "$out"
    assert_eq "no temporary folders left" "" "$(find "$LIB/playlists" -name '.*')"
}

test_pendrive_order_rules()
{
    setup_library
    tc pendrive new p >/dev/null 2>&1
    local s=$LIB/playlists/p/symlinks
    ln -s ../../../track-master-copy/Acid/t1 "$s/005_t1"
    ln -s ../../../track-master-copy/House/Track\ One "$s/2_one"
    ln -s ../../../track-master-copy/House/Track\ 10 "$s/unnumbered"
    run tc pendrive ls p
    assert_eq "numeric order, unnumbered entries last" "$(printf '2_one\tHouse/Track One\n005_t1\tAcid/t1\nunnumbered\tHouse/Track 10')" "$out"
    tc pendrive add p "House/Track One" >/dev/null 2>&1
    run tc pendrive ls p
    assert_eq "adding a present track changes nothing" "3" "$(printf '%s\n' "$out" | wc -l)"
    make_track "$LIB" Z z
    tc pendrive add p z >/dev/null 2>&1
    run tc pendrive ls p
    assert_eq "renumbered consecutively, unnumbered entries keep their name" "$(printf '000_one\tHouse/Track One\n001_t1\tAcid/t1\n002_unnumbered\tHouse/Track 10\n003_z\tZ/z')" "$out"
    ln -sfn ../../../track-master-copy/Gone/x "$s/003_z"
    run tc pendrive ls p
    assert_contains "a dangling link is listed" "(dangling)" "$out"
}

run_tests
