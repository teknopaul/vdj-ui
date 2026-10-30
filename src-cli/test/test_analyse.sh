#!/bin/bash
#
# analyse, beatgrid, waveform, loudness (and import --analyse) through the wrapper, on synthetic audio (gen_beats.py)
#
. "$(dirname "$0")/lib.sh"

command -v python3 >/dev/null || { echo "test_analyse.sh: python3 not found, skipped"; exit 0; }

tc() { "$TRAKCRATE" --library "$LIB" "$@"; }
M() { printf '%s/track-master-copy/%s' "$LIB" "$1"; }

# a library with F/t1 (128 bpm, mono) and F/t2 (100 bpm, stereo), 12 seconds each
setup()
{
    LIB=$T/lib
    tc init >/dev/null 2>&1
    make_track "$LIB" F t1
    make_track "$LIB" F t2
    rm -f "$(M F/t1)/track.mp3" "$(M F/t2)/track.mp3"
    python3 -I "$TC_TEST_DIR/gen_beats.py" "$(M F/t1)/track.wav" 128 12
    python3 -I "$TC_TEST_DIR/gen_beats.py" "$(M F/t2)/track.wav" 100 12 stereo 0.2
}

test_analyse_writes_the_files()
{
    setup
    run tc analyse t1
    assert_status "analyse" 0
    assert_contains "text result" "128.00 bpm" "$out"
    local grid
    grid=$(cat "$(M F/t1)/beatgrid.txt")
    assert_contains "a header comment" "# trakcrate beatgrid" "$grid"
    assert_contains "bpm" "bpm: 128.00" "$grid"
    assert_contains "first beat near 100 ms" "first_beat_us: 10" "$grid"
    assert_contains "confidence" "confidence: 0." "$grid"
    assert_contains "analyser" "analyser: trakcrate-beats 1" "$grid"
    assert_eq "waveform.bin starts with TCWV version 1" "5443575601000000" "$(head -c 8 "$(M F/t1)/waveform.bin" | od -An -tx1 | tr -d ' \n')"
    assert_contains "peak" "peak_dbfs: -2.3" "$(cat "$(M F/t1)/meta.txt")"
    assert_contains "rms" "rms_dbfs: -" "$(cat "$(M F/t1)/meta.txt")"
    assert_contains "provenance: beatgrid" "origin.beatgrid: generated" "$(cat "$(M F/t1)/source.txt")"
    assert_contains "provenance: waveform" "origin.waveform: generated" "$(cat "$(M F/t1)/source.txt")"
    assert_no_file "the lock is released" "$LIB/.trakcrate-lock"
    assert_eq "no temporary files" "" "$(find "$LIB" -name '.*.tmp.*')"
    run tc check
    assert_status "the library is valid" 0
    run tc ls -l t1
    assert_contains "ls sees it as analysed" "analysed" "$out"
}

test_analysis_is_deterministic()
{
    setup
    tc analyse t1 >/dev/null 2>&1
    cp "$(M F/t1)/beatgrid.txt" "$T/g1"; cp "$(M F/t1)/waveform.bin" "$T/w1"; cp "$(M F/t1)/meta.txt" "$T/m1"
    tc analyse t1 >/dev/null 2>&1
    assert_eq "beatgrid.txt: same bytes" "$(cksum < "$T/g1")" "$(cksum < "$(M F/t1)/beatgrid.txt")"
    assert_eq "waveform.bin: same bytes" "$(cksum < "$T/w1")" "$(cksum < "$(M F/t1)/waveform.bin")"
    assert_eq "meta.txt: same bytes" "$(cksum < "$T/m1")" "$(cksum < "$(M F/t1)/meta.txt")"
}

test_formats()
{
    setup
    run tc --format jsonl analyse t2
    assert_contains "jsonl: bpm" '"status":"ok","bpm":100.00' "$out"
    assert_contains "jsonl: written" '"written":["beatgrid","waveform"]' "$out"
    assert_contains "jsonl: analyser" '"analyser":"trakcrate-beats 1"' "$out"
    run tc --format tsv analyse --force t2
    assert_eq "tsv header" "path	status	bpm	first_beat_us	confidence	analyser	written	skipped_groups	message" "$(printf '%s\n' "$out" | head -n 1)"
    assert_contains "tsv row" "ok	100.00	" "$(printf '%s\n' "$out" | sed -n 2p)"
    run tc --format json analyse --all
    assert_eq "json array" "[" "${out:0:1}"
    assert_eq "two records" "2" "$(printf '%s' "$out" | grep -o '"status"' | wc -l)"
}

test_all_and_order_with_jobs()
{
    setup
    run tc -j 2 --format paths analyse --all
    assert_eq "paths, in library order" "$(printf '%s\n%s' "$(M F/t1)" "$(M F/t2)")" "$out"
    assert_contains "t2 analysed too" "bpm: 100.00" "$(cat "$(M F/t2)/beatgrid.txt")"
    run tc analyse
    assert_status "no tracks named" 2
    run tc analyse --all t1
    assert_status "--all with names" 2
}

test_protected_groups_and_force()
{
    setup
    tc analyse t1 >/dev/null 2>&1
    tc set t1 title=x >/dev/null 2>&1
    printf 'origin.beatgrid: rekordbox\norigin.waveform: edited\n' > "$(M F/t1)/source.txt"
    printf '# my grid\nbpm: 70.00\nfirst_beat_us: 5\n' > "$(M F/t1)/beatgrid.txt"
    cp "$(M F/t1)/waveform.bin" "$T/w"
    run tc --format jsonl analyse t1
    assert_status "all protected" 0
    assert_contains "skipped" '"status":"skipped","skipped_groups":["beatgrid:rekordbox","waveform:edited"]' "$out"
    assert_eq "the grid is untouched" "$(printf '# my grid\nbpm: 70.00\nfirst_beat_us: 5')" "$(cat "$(M F/t1)/beatgrid.txt")"
    assert_eq "the waveform is untouched" "$(cksum < "$T/w")" "$(cksum < "$(M F/t1)/waveform.bin")"

    run tc --format jsonl analyse --only waveform t1
    assert_contains "only the waveform asked: protected" '"skipped_groups":["waveform:edited"]' "$out"
    run tc analyse --force t1
    assert_contains "--force redoes both" "128.00 bpm" "$out"
    assert_eq "the origin becomes generated" "origin.beatgrid: generated
origin.waveform: generated" "$(cat "$(M F/t1)/source.txt")"
}

test_print_writes_nothing()
{
    setup
    run tc beatgrid --print --no-prior t2
    assert_status "beatgrid --print" 0
    assert_contains "result" "100.00 bpm" "$out"
    assert_no_file "no beatgrid.txt" "$(M F/t2)/beatgrid.txt"
    assert_no_file "no waveform.bin" "$(M F/t2)/waveform.bin"
    assert_no_file "meta.txt untouched" "$(M F/t2)/meta.txt"
    run tc -n analyse t1 t2
    assert_status "dry run" 0
    assert_no_file "dry run writes nothing" "$(M F/t1)/waveform.bin"
}

test_subcommands()
{
    setup
    run tc beatgrid t1
    assert_status "beatgrid" 0
    assert_file "beatgrid.txt" "$(M F/t1)/beatgrid.txt"
    assert_no_file "only the beat grid" "$(M F/t1)/waveform.bin"
    run tc waveform t1
    assert_status "waveform" 0
    assert_file "waveform.bin" "$(M F/t1)/waveform.bin"
    run tc beatgrid --bpm 128 t1
    assert_status "not available yet: --bpm" 2
    run tc waveform --png x.png t1
    assert_status "not available yet: --png" 2
    run tc --format tsv loudness
    assert_contains "loudness of an analysed track" "$(M F/t1)	-2.3	-" "$out"
    assert_contains "and one that was not" "$(M F/t2)			skipped" "$out"
    run tc --format jsonl loudness t1
    assert_contains "loudness jsonl" '"peak_dbfs":-2.3' "$out"
}

test_failures()
{
    setup
    make_track "$LIB" F bad
    echo "not audio" > "$(M F/bad)/track.mp3"
    make_track "$LIB" F empty
    rm "$(M F/empty)/track.mp3"
    run tc analyse t1 bad t2
    assert_status "some failed, others done" 3
    assert_contains "the failure is reported" "failed" "$out"
    assert_contains "bad" "bad" "$err"
    assert_file "t2 was still analysed" "$(M F/t2)/waveform.bin"
    run tc analyse bad
    assert_status "all failed" 1
    run tc --format jsonl analyse empty
    assert_contains "a track without audio" '"status":"failed"' "$out"
    assert_contains "says why" "no audio file" "$out"
    rm -f "$(M F/t1)/waveform.bin"
    run tc analyse --fail-fast bad t1
    assert_status "fail fast" 1
    assert_no_file "stopped before t1" "$(M F/t1)/waveform.bin"
    run tc analyse nonesuch
    assert_status "an unknown track" 1
}

test_timeout()
{
    setup
    run tc --format jsonl analyse --timeout 1 t1
    # a 12 second track takes well under a second; the option must at least be accepted
    assert_status "a generous timeout" 0
    run tc analyse --timeout soon t1
    assert_status "bad timeout" 2
}

test_progress_events()
{
    setup
    tc --format paths analyse --progress-fd 3 t1 t2 3> "$T/events" >/dev/null 2>&1
    local events
    events=$(cat "$T/events")
    assert_eq "start, two items, done" "4" "$(printf '%s\n' "$events" | wc -l)"
    assert_contains "start" '{"event":"start","command":"analyse","total":2}' "$(printf '%s\n' "$events" | sed -n 1p)"
    assert_contains "item 1" '"event":"item","index":1,"path":"'"$(M F/t1)"'","status":"ok"' "$(printf '%s\n' "$events" | sed -n 2p)"
    assert_contains "item 2" '"index":2' "$(printf '%s\n' "$events" | sed -n 3p)"
    assert_contains "done" '"event":"done","ok":2,"skipped":0,"failed":0' "$(printf '%s\n' "$events" | sed -n 4p)"
    run tc analyse --progress-fd 9 t1
    assert_status "a closed descriptor" 2
    run tc analyse --progress t1
    assert_contains "--progress lines on stderr" "[1/1] ok" "$err"
}

test_lock()
{
    setup
    printf '%s\n' "$$" > "$LIB/.trakcrate-lock"
    run tc analyse t1
    assert_status "busy" 4
    assert_no_file "nothing was written" "$(M F/t1)/beatgrid.txt"
    run tc beatgrid --print t1
    assert_status "--print does not need the lock" 0
}

test_import_analyse()
{
    command -v ffmpeg >/dev/null || { echo "    ffmpeg not found, skipped"; return 0; }
    LIB=$T/lib
    tc init >/dev/null 2>&1
    mkdir -p "$T/Music/Set"
    python3 -I "$TC_TEST_DIR/gen_beats.py" "$T/Music/Set/four.wav" 128 12
    run tc import --analyse "$T/Music/Set"
    assert_status "import --analyse" 0
    assert_contains "the import summary" "imported 1" "$out"
    assert_contains "the analysis on stderr" "128.00 bpm" "$err"
    assert_file "analysed" "$LIB/track-master-copy/Set/four/waveform.bin"
    assert_contains "bpm" "bpm: 128.00" "$(cat "$LIB/track-master-copy/Set/four/beatgrid.txt")"
    assert_no_file "the lock is released" "$LIB/.trakcrate-lock"
}

run_tests
