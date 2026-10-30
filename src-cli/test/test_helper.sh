#!/bin/bash
#
# trakcrate-helper: natsort and probe
#
. "$(dirname "$0")/lib.sh"

h() { "$TC_HELPER" "$@"; }

test_version_and_usage()
{
    run h --version
    assert_eq "version" "trakcrate-helper $TC_VERSION (api 1)" "$out"
    run h
    assert_status "no subcommand" 2
    run h bogus
    assert_status "unknown subcommand" 2
}

test_natsort()
{
    out=$(printf '%s\n' 'Track 10' 'track 2' 'TRACK 1' 'track 02b' 'a' | h natsort)
    assert_eq "natural, case-insensitive order" "$(printf '%s\n' a 'TRACK 1' 'track 2' 'track 02b' 'Track 10')" "$out"
    out=$(printf 'b\0a 10\0a 9\0' | h natsort -z | tr '\0' '|')
    assert_eq "NUL separated, names may hold anything" "a 9|a 10|b|" "$out"
    out=$(printf 'x\ny' | h natsort)
    assert_eq "a last line without newline" "$(printf 'x\ny')" "$out"
    out=$(printf '' | h natsort)
    assert_eq "empty input" "" "$out"
    out=$(printf 'b\nb\na\n' | h natsort)
    assert_eq "duplicates are kept" "$(printf 'a\nb\nb')" "$out"
    run h natsort --bogus
    assert_status "bad option" 2
}

test_probe()
{
    command -v ffmpeg >/dev/null || { echo "    ffmpeg not found, skipped"; return 0; }
    make_wav "$T/t.wav" 0.5 440 title='A "quoted" \ title' artist=Me album=Al genre=House date=2019-05-01 track=7
    run h probe "$T/t.wav"
    assert_status "probe" 0
    assert_contains "container" "container: wav" "$out"
    assert_contains "duration" "duration_ms: 500" "$out"
    assert_contains "channels" "channels: 1" "$out"
    assert_contains "title, escaped as in meta.txt" 'title: A "quoted" \\ title' "$out"
    assert_contains "album" "album: Al" "$out"
    assert_contains "genre" "genre: House" "$out"
    assert_contains "year" "year: 2019" "$out"
    assert_contains "track number" "track_number: 7" "$out"
    assert_not_contains "no artwork without --artwork-dir" "artwork" "$out"

    echo "not audio" > "$T/bad.wav"
    run h probe "$T/bad.wav"
    assert_status "not audio" 1
    assert_contains "says why" "cannot open" "$err"
    run h probe "$T/missing.wav"
    assert_status "missing file" 1
    : > "$T/empty.wav"
    run h probe "$T/empty.wav"
    assert_status "empty file" 1
    run h probe
    assert_status "no file" 2

    # a cover picture, when this ffmpeg can make an mp3 with one
    ffmpeg -loglevel error -y -f lavfi -i color=c=red:s=16x16:d=0.1 -frames:v 1 "$T/cover.png" 2>/dev/null
    if ffmpeg -loglevel error -y -f lavfi -i sine=d=0.3 -i "$T/cover.png" -map 0 -map 1 -c:a libmp3lame -c:v copy -id3v2_version 3 \
        -metadata:s:v title=Cover -disposition:v attached_pic "$T/c.mp3" 2>/dev/null; then
        mkdir -p "$T/art"
        run h probe "$T/c.mp3" --artwork-dir "$T/art"
        assert_status "probe with a cover" 0
        assert_contains "artwork reported" "artwork: artwork.png" "$out"
        assert_file "cover written" "$T/art/artwork.png"
        assert_eq "no temporary file left" "" "$(ls -A "$T/art" | grep tmp || true)"
    else
        echo "    no mp3 encoder, cover test skipped"
    fi
}

run_tests
