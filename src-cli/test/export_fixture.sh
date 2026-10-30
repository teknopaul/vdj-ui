# Sourced by test_export.sh and test_export_oracle.sh: a small analysed library with three playlists.
#
#   export_fixture LIB    folders F and G; tracks F/t1 (128 bpm, stereo, artwork, a hot cue and a cue), F/t2 (100 bpm, the same artwork),
#                         G/t3 (123.7 bpm, no artwork, a non-ASCII title); playlists A = t1 t2, B = t3 t2, C = empty
export_fixture()
{
    local lib=$1 m
    "$TRAKCRATE" --library "$lib" init >/dev/null 2>&1
    m() { printf '%s/track-master-copy/%s' "$lib" "$1"; }
    make_track "$lib" F t1 "title: First Track" "artist: Alpha" "album: Debut" "genre: House" "label: Lbl" "year: 2020" "track_number: 3" "rating: 4" \
        "duration_ms: 12000" "bitrate: 320000" "sample_rate: 44100" "audio_sha256: 0a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f9"
    make_track "$lib" F t2 "title: Second" "artist: Alpha" "album: Debut" "genre: House" "duration_ms: 12000" "comment: a comment"
    make_track "$lib" G t3 "title: Třetí skladba" "artist: Beta" "genre: Techno" "duration_ms: 12000" "bpm_tag: 124"
    rm -f "$(m F/t1)/track.mp3" "$(m F/t2)/track.mp3" "$(m G/t3)/track.mp3"
    python3 -I "$TC_TEST_DIR/gen_beats.py" "$(m F/t1)/track.wav" 128 12 stereo 0.1
    python3 -I "$TC_TEST_DIR/gen_beats.py" "$(m F/t2)/track.wav" 100 12 mono 0.2
    python3 -I "$TC_TEST_DIR/gen_beats.py" "$(m G/t3)/track.wav" 123.7 12 stereo 0.3
    python3 -I "$TC_TEST_DIR/gen_png.py" "$(m F/t1)/artwork.png"
    cp "$(m F/t1)/artwork.png" "$(m F/t2)/artwork.png"
    printf '# cuemarks\n0\t1000\tintro\tA\t-\n4\t3000\tdrop\t-\t-\n' > "$(m F/t1)/cuemarks.txt"
    "$TRAKCRATE" --library "$lib" analyse --all >/dev/null 2>&1
    "$TRAKCRATE" --library "$lib" pendrive new A >/dev/null 2>&1
    "$TRAKCRATE" --library "$lib" pendrive add A t1 t2 >/dev/null 2>&1
    "$TRAKCRATE" --library "$lib" pendrive new B >/dev/null 2>&1
    "$TRAKCRATE" --library "$lib" pendrive add B t3 t2 >/dev/null 2>&1
    "$TRAKCRATE" --library "$lib" pendrive new C >/dev/null 2>&1
}
