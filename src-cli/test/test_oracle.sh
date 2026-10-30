#!/bin/bash
#
# The C analyser against the GUI's C++ analyser (the oracle, test/oracle/): same audio, byte-identical beatgrid.txt, waveform.bin,
# meta.txt and source.txt. Skipped when the C++ library of the GUI is not built (build/src/libvdj-trakcrate.a).
#
. "$(dirname "$0")/lib.sh"

oracle=$("$TC_TEST_DIR/oracle/build.sh" 2>/dev/null) || { echo "test_oracle.sh: the GUI library is not built, skipped"; exit 0; }
command -v python3 >/dev/null || { echo "test_oracle.sh: python3 not found, skipped"; exit 0; }

# compare NAME BPM SECONDS [stereo] [offset] [level]
compare()
{
    local name=$1
    shift
    local c=$T/c-$name x=$T/x-$name
    mkdir -p "$c" "$x"
    python3 -I "$TC_TEST_DIR/gen_beats.py" "$c/track.wav" "$@"
    cp "$c/track.wav" "$x/track.wav"
    run "$TC_HELPER" analyse "$c"
    assert_status "$name: C analyser" 0
    run "$oracle" "$x"
    assert_status "$name: C++ analyser" 0
    local f
    for f in beatgrid.txt waveform.bin meta.txt source.txt; do
        if cmp -s "$c/$f" "$x/$f"; then
            pass_assert
        else
            fail_assert "$name: $f differs" "C:   $(head -c 300 "$c/$f" | tr '\n' '|')" "C++: $(head -c 300 "$x/$f" | tr '\n' '|')"
        fi
    done
}

test_house_tempo()    { compare house 128 20; }
test_slow_tempo()     { compare slow 90 24 stereo 0.25; }
test_fast_tempo()     { compare fast 174 20 mono 0.03 0.6; }
test_odd_tempo()      { compare odd 123.7 30 stereo 0.4 0.8; }
test_quiet_track()    { compare quiet 120 20 mono 0.1 0.05; }
test_short_track()    { compare short 128 3; }
test_silence()        { compare silence 0 10; }
test_noise()          { compare noise -1 10; }

run_tests
