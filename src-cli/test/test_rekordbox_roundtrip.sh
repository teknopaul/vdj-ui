#!/bin/bash
#
# Round trips on the real rekordbox files of libcdj's fixtures (references/libcdj/rekordbox, 7 tracks): the PDB model reproduces every
# byte, the ANLZ writer rebuilds every real analysis file tag by tag, the rows agree with libcdj's readers; and `verify` accepts the
# real drive. The large real export of the GUI's tests (2195 tracks) is used when it is there.
#
. "$(dirname "$0")/lib.sh"

drive=$TC_ROOT/../references/libcdj/rekordbox
big=$TC_ROOT/../tests/fixtures/dbserver/export/PIONEER/rekordbox/export.pdb
[ -f "$drive/PIONEER/rekordbox/export.pdb" ] || { echo "test_rekordbox_roundtrip.sh: $drive is not there, skipped"; exit 0; }
make -s -C "$TC_ROOT/c" target/rb_roundtrip >/dev/null 2>&1 || { echo "test_rekordbox_roundtrip.sh: cannot build rb_roundtrip"; exit 1; }
rt=$TC_ROOT/c/target/rb_roundtrip

test_pdb_of_a_real_drive_is_reproduced_byte_for_byte()
{
    run "$rt" pdb "$drive/PIONEER/rekordbox/export.pdb"
    assert_status "round trip" 0
    assert_contains "identical" "identical" "$out"
    assert_contains "data pages are decoded" "10 data pages decoded" "$out"
    run "$rt" pdb "$TC_ROOT/../data/trakcrate/template-export.pdb"
    assert_status "the template" 0
}

test_pdb_of_a_large_real_export_is_reproduced()
{
    [ -f "$big" ] || return 0
    run "$rt" pdb "$big"
    assert_status "round trip" 0
    run "$rt" rows "$big"
    assert_status "rows" 0
    assert_contains "2195 tracks as libcdj" "2195 tracks" "$out"
}

test_rows_agree_with_libcdj()
{
    run "$rt" rows "$drive/PIONEER/rekordbox/export.pdb"
    assert_status "rows" 0
    assert_contains "seven tracks" "7 tracks, 7 playlist entries" "$out"
}

test_real_analysis_files_are_rebuilt_tag_by_tag()
{
    local d n=0
    for d in "$drive"/PIONEER/USBANLZ/P*/*/; do
        run "$rt" anlz "${d%/}"
        assert_status "${d#"$drive"/}" 0
        assert_contains "identical" "tags identical" "$out"
        n=$((n + 1))
    done
    assert_eq "seven real pairs" "7" "$n"
}

test_verify_accepts_a_real_drive()
{
    run "$TC_HELPER" verify "$drive"
    assert_status "verify" 0
    assert_contains "counts" "checked 7 tracks, 1 playlists" "$err"
    assert_not_contains "no errors" "	error	" "$out"
}

run_tests
