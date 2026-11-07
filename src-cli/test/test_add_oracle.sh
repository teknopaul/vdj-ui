#!/bin/bash
#
# add-to-drive of the C helper against the GUI's C++ addToDrive (the oracle, test/oracle/cxx_add.cpp): two copies of one stick get the same playlist
# added; the sticks must be byte identical afterwards, artwork JPEGs aside (the encoders differ, so artwork is compared by path), and so must the
# copies of the old database. Skipped when the GUI library is not built.
#
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/export_fixture.sh"

oracle=$("$TC_TEST_DIR/oracle/build.sh" add 2>/dev/null) || { echo "test_add_oracle.sh: the GUI library is not built, skipped"; exit 0; }
command -v python3 >/dev/null || { echo "test_add_oracle.sh: python3 not found, skipped"; exit 0; }

template=$TC_ROOT/script/usr/share/trakcrate/template-export.pdb

tc() { "$TRAKCRATE" --library "$LIB" "$@"; }

# the stick holds B (t3, t2); the playlist added is A (t1, t2): t1 is new (with artwork), t2 is on the stick
setup()
{
    LIB=$T/lib
    export_fixture "$LIB"
    tc export --no-verify --no-manifest B "$T/stick-c" >/dev/null 2>&1
    cp -a "$T/stick-c" "$T/stick-x"
}

compare_sticks()
{
    local what=$1 a b f
    a=$(cd "$T/stick-c" && find . -type f | sort)
    b=$(cd "$T/stick-x" && find . -type f | sort)
    assert_eq "$what: the same files" "$b" "$a"
    while IFS= read -r f; do
        case $f in ./PIONEER/Artwork/*) continue ;; esac
        if cmp -s "$T/stick-c/$f" "$T/stick-x/$f"; then
            pass_assert
        else
            fail_assert "$what: $f differs" "C:   $(wc -c < "$T/stick-c/$f") bytes" "C++: $(wc -c < "$T/stick-x/$f") bytes"
        fi
    done <<< "$b"
}

test_the_stick_after_an_add_is_byte_identical()
{
    setup
    run tc add-to-drive --backup-dir "$T/bak-c" --template "$template" A "$T/stick-c"
    assert_status "C add" 0
    run "$oracle" "$template" "$T/stick-x" "$T/bak-x" "$LIB" A
    assert_status "C++ add" 0
    assert_contains "C++ says one new track" "new 1 existing 1" "$out"
    compare_sticks "add A to a stick with B"
    assert_eq "artwork: the same paths" "$(cd "$T/stick-x" && find PIONEER/Artwork -type f | sort)" "$(cd "$T/stick-c" && find PIONEER/Artwork -type f | sort)"
    assert_eq "the copies of the old database" "$(cksum < "$(ls "$T"/bak-x/*.pdb)")" "$(cksum < "$(ls "$T"/bak-c/*.pdb)")"
}

test_a_second_add_is_byte_identical()
{
    setup
    tc pendrive new E >/dev/null 2>&1
    tc pendrive add E t1 t2 t3 >/dev/null 2>&1
    run tc add-to-drive --backup-dir "$T/bak-c" --template "$template" A "$T/stick-c"
    run "$oracle" "$template" "$T/stick-x" "$T/bak-x" "$LIB" A
    run tc add-to-drive --backup-dir "$T/bak-c" --template "$template" E "$T/stick-c"
    assert_status "C second add" 0
    run "$oracle" "$template" "$T/stick-x" "$T/bak-x" "$LIB" E
    assert_status "C++ second add" 0
    compare_sticks "a second add"
}

test_the_refusals_are_the_same()
{
    setup
    run tc add-to-drive --backup-dir "$T/bak-c" --template "$template" B "$T/stick-c"
    local cs=$status cerr=$err
    run "$oracle" "$template" "$T/stick-x" "$T/bak-x" "$LIB" B
    assert_eq "the same exit status" "$status" "$cs"
    assert_contains "C names the playlist" 'already has a playlist called "B": nothing was changed' "$cerr"
    assert_contains "C++ names the playlist" 'already has a playlist called "B": nothing was changed' "$err"
}

run_tests
