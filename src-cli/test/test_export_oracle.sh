#!/bin/bash
#
# The C exporter against the GUI's C++ exporter (the oracle, test/oracle/cxx_export.cpp): the same library gives a byte identical drive,
# artwork JPEGs aside (the encoders differ, so artwork is compared by path). Skipped when the GUI library is not built.
#
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/export_fixture.sh"

oracle=$("$TC_TEST_DIR/oracle/build.sh" export 2>/dev/null) || { echo "test_export_oracle.sh: the GUI library is not built, skipped"; exit 0; }
command -v python3 >/dev/null || { echo "test_export_oracle.sh: python3 not found, skipped"; exit 0; }

template=$TC_ROOT/script/usr/share/trakcrate/template-export.pdb

setup()
{
    LIB=$T/lib
    export_fixture "$LIB"
    M=$LIB/track-master-copy
    # the lists as `trakcrate export` hands them to the helper
    stdin_data=$(printf 'A\t%s\nA\t%s\nB\t%s\nB\t%s' "$M/F/t1" "$M/F/t2" "$M/G/t3" "$M/F/t2")
}

# compare_trees C CXX [exclude pattern]: every file byte identical
compare_trees()
{
    local c=$1 x=$2 what=$3
    shift 3
    local a b
    a=$(cd "$c" && find . -type f "$@" | sort)
    b=$(cd "$x" && find . -type f "$@" | sort)
    assert_eq "$what: the same files" "$b" "$a"
    local f
    while IFS= read -r f; do
        if cmp -s "$c/$f" "$x/$f"; then
            pass_assert
        else
            fail_assert "$what: $f differs" "C:   $(wc -c < "$c/$f") bytes" "C++: $(wc -c < "$x/$f") bytes"
        fi
    done <<< "$b"
}

test_drive_without_artwork_is_byte_identical()
{
    setup
    run_stdin "$oracle" "$template" "$T/x" --no-artwork
    assert_status "C++ export" 0
    run_stdin "$TC_HELPER" export --to "$T/c" --template "$template" --no-artwork
    assert_status "C export" 0
    compare_trees "$T/c" "$T/x" "no artwork" -not -name 'xxx-none'
}

test_drive_with_artwork_matches_apart_from_the_jpegs()
{
    setup
    run_stdin "$oracle" "$template" "$T/x"
    assert_status "C++ export" 0
    run_stdin "$TC_HELPER" export --to "$T/c" --template "$template"
    assert_status "C export" 0
    assert_eq "artwork: the same paths" "$(cd "$T/x" && find PIONEER/Artwork -type f | sort)" "$(cd "$T/c" && find PIONEER/Artwork -type f | sort)"
    # SHA256SUMS lists the artwork bytes, which differ by design
    compare_trees "$T/c" "$T/x" "with artwork" -not -path '*/Artwork/*' -not -name SHA256SUMS
    assert_eq "SHA256SUMS: the same lines but the artwork's" "$(grep -v Artwork "$T/x/SHA256SUMS")" "$(grep -v Artwork "$T/c/SHA256SUMS")"
}

test_settings_and_options_match()
{
    setup
    run_stdin "$oracle" "$template" "$T/x" --no-artwork
    run_stdin "$TC_HELPER" export --to "$T/c" --template "$template" --no-artwork
    assert_eq "DEVSETTING.DAT" "$(cksum < "$T/x/PIONEER/DEVSETTING.DAT")" "$(cksum < "$T/c/PIONEER/DEVSETTING.DAT")"
    assert_eq "MYSETTING.DAT" "$(cksum < "$T/x/PIONEER/MYSETTING.DAT")" "$(cksum < "$T/c/PIONEER/MYSETTING.DAT")"
    assert_eq "export.pdb" "$(cksum < "$T/x/PIONEER/rekordbox/export.pdb")" "$(cksum < "$T/c/PIONEER/rekordbox/export.pdb")"
}

run_tests
