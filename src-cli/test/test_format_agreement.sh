#!/bin/bash
#
# The same format rules are implemented twice here: in bash/awk (common.sh, used by the commands) and in C (lib/kv.c,
# the helper). Whatever they read or write must be identical (TRAKCRATE-FORMAT.md section 3).
#
. "$(dirname "$0")/lib.sh"
. "$TC_ROOT/script/usr/lib/trakcrate/common.sh"

h() { "$TC_HELPER" "$@"; }

write_samples()
{
    cat > "$T/typical.txt" <<'X'
# trakcrate meta
title: Say "hi"
artist:   Alice  
rating: 4
duration_ms: 61000
X
    printf 'a: 1\na: 2\nb: x\na: 3\n' > "$T/repeated.txt"
    printf '# top\n\n   \nweird line without colon\n  indented : value\n\t# tab comment\nkey:no space\nspaced   :   lots   \n' > "$T/messy.txt"
    printf 'note: two\\nlines and a \\\\ backslash\nplain: a\\tb\nend: trailing\\\n' > "$T/escapes.txt"
    printf 'url: http://example.org:8080/a:b\nempty:\nonly-spaces:    \nx-tool: 1\n' > "$T/colons.txt"
    printf 'last: no newline at the end' > "$T/nonewline.txt"
    : > "$T/empty.txt"
}

test_reading_agrees()
{
    write_samples
    local f key bash_out c_out bash_rc c_rc
    for f in typical repeated messy escapes colons nonewline empty; do
        for key in title artist rating a b key spaced indented note plain end url empty only-spaces x-tool last nothing; do
            bash_out=$(tc_kv_get "$T/$f.txt" "$key"; echo "rc=$?")
            c_out=$(h kv get "$T/$f.txt" "$key"; echo "rc=$?")
            assert_eq "get $key from $f.txt" "$bash_out" "$c_out"
        done
    done
}

test_writing_agrees()
{
    write_samples
    local f op i=0
    for f in typical repeated messy escapes colons nonewline empty; do
        for op in "set rating 5" "set brand-new fresh" "set note two<NL>lines<BS>x" "set a 9" "set empty filled" "unset a" "unset rating" "unset nothing" "set title"; do
            i=$((i + 1))
            cp "$T/$f.txt" "$T/bash.txt"
            cp "$T/$f.txt" "$T/c.txt"
            set -- $op
            value=${3:-}
            value=${value//<NL>/$'\n'}
            value=${value//<BS>/\\}
            if [ "$1" = set ]; then
                tc_kv_set "$T/bash.txt" "$2" "$value"
                h kv set "$T/c.txt" "$2" "$value"
            else
                tc_kv_unset "$T/bash.txt" "$2"
                h kv unset "$T/c.txt" "$2"
            fi
            assert_eq "'$op' on $f.txt gives the same bytes" "$(od -c "$T/bash.txt")" "$(od -c "$T/c.txt")"
        done
    done
}

test_helper_kv_edges()
{
    write_samples
    run h kv get "$T/missing.txt" a
    assert_status "get from a missing file" 1
    run h kv set "$T/new.txt" a 1
    assert_status "set creates the file" 0
    assert_eq "content" "a: 1" "$(cat "$T/new.txt")"
    run h kv unset "$T/nonesuch.txt" a
    assert_status "unset in a missing file is fine" 0
    assert_no_file "and creates nothing" "$T/nonesuch.txt"
    run h kv
    assert_status "usage" 2
    run h kv get "$T/typical.txt"
    assert_status "usage: key missing" 2
    run h kv frob "$T/typical.txt" a
    assert_status "usage: unknown action" 2
    assert_eq "no temporary files left behind" "" "$(find "$T" -name '.*.tmp.*')"
}

test_helper_sha256()
{
    printf 'abc' > "$T/abc"
    run h sha256 "$T/abc"
    assert_eq "known value" "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  $T/abc" "$out"
    run h sha256 "$T/abc" "$TC_HELPER"
    assert_eq "same as sha256sum" "$(sha256sum "$T/abc" "$TC_HELPER")" "$out"
    run h sha256 "$T/abc" "$T/missing"
    assert_status "a missing file" 1
    assert_contains "the others are still done" "ba7816bf" "$out"
    run h sha256
    assert_status "usage" 2
}

run_tests
