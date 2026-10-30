#!/bin/bash
#
# A very small test harness for the trakcrate command line tools (no bats needed).
#
# A test file sources this, defines functions named test_*, and ends with `run_tests`.
# Each test runs in a subshell with a fresh scratch directory in $T and a private $HOME.
#
# run CMD...     runs CMD with stdout in $out, stderr in $err, exit status in $status
# assert_eq NAME EXPECTED ACTUAL, assert_status NAME EXPECTED, assert_contains NAME NEEDLE HAYSTACK,
# assert_not_contains NAME NEEDLE HAYSTACK, assert_file NAME PATH
#
set -u

TC_TEST_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
TC_ROOT=$(dirname "$TC_TEST_DIR")
TRAKCRATE=$TC_ROOT/script/usr/bin/trakcrate
TC_HELPER=${TC_HELPER:-$TC_ROOT/c/target/trakcrate-helper}     # TC_HELPER=... runs the tests against another build (a sanitizer build)
TC_VERSION=$(sed -n 's/^VERSION=//p' "$TC_ROOT/../version" | head -n 1)

failures=0
assertions=0

fail_assert()
{
    failures=$((failures + 1))
    printf '    [FAIL] %s\n' "$1"
    shift
    if [ $# -gt 0 ]; then
        printf '           %s\n' "$@"
    fi
}

pass_assert()
{
    assertions=$((assertions + 1))
}

assert_eq()
{
    if [ "$2" = "$3" ]; then
        pass_assert
    else
        fail_assert "$1" "expected: $2" "actual:   $3"
    fi
}

assert_status()
{
    assert_eq "$1 (exit status)" "$2" "$status"
}

assert_contains()
{
    case $3 in
        *"$2"*) pass_assert ;;
        *) fail_assert "$1" "missing:  $2" "in:       $3" ;;
    esac
}

assert_not_contains()
{
    case $3 in
        *"$2"*) fail_assert "$1" "unexpected: $2" "in:         $3" ;;
        *) pass_assert ;;
    esac
}

assert_file()
{
    if [ -e "$2" ]; then
        pass_assert
    else
        fail_assert "$1" "no such file: $2"
    fi
}

assert_no_file()
{
    if [ ! -e "$2" ]; then
        pass_assert
    else
        fail_assert "$1" "unexpected file: $2"
    fi
}

# runs a command, keeping stdout, stderr and the exit status; never stops the test
run()
{
    local errfile=$T/.stderr
    out=$("$@" 2>"$errfile" </dev/null)
    status=$?
    err=$(cat "$errfile")
}

# like run, but the command reads $stdin_data on its standard input
run_stdin()
{
    local errfile=$T/.stderr
    out=$("$@" 2>"$errfile" <<< "$stdin_data")
    status=$?
    err=$(cat "$errfile")
}

# make_cmd DIR NAME 'shell body': an executable fake command DIR/trakcrate-NAME
make_cmd()
{
    mkdir -p "$1"
    printf '#!/bin/bash\n%s\n' "$3" > "$1/trakcrate-$2"
    chmod +x "$1/trakcrate-$2"
}

# install_tree PREFIX: the wrapper and its configuration laid out as the package installs them (PREFIX/usr/bin ...),
# with the version filled in as build-deb.sh does
install_tree()
{
    mkdir -p "$1/usr/bin" "$1/usr/lib/trakcrate" "$1/etc/trakcrate"
    sed "s/@VERSION@/9.9.9/" "$TRAKCRATE" > "$1/usr/bin/trakcrate"
    chmod +x "$1/usr/bin/trakcrate"
    cp "$TC_ROOT/script/etc/trakcrate/trakcrate.conf" "$1/etc/trakcrate/trakcrate.conf"
}

# make_track LIB FOLDER TRACK [meta.txt line...]: a track directory with a dummy audio file (the format allows any file)
make_track()
{
    local d=$1/track-master-copy/$2/$3
    mkdir -p "$d"
    : > "$d/track.mp3"
    shift 3
    if [ $# -gt 0 ]; then
        printf '%s\n' "$@" > "$d/meta.txt"
    fi
}

# make_wav FILE [SECONDS] [FREQUENCY] [metadata key=value ...]: a real (tiny) wav file, made with ffmpeg; returns 1 without ffmpeg
make_wav()
{
    local file=$1 secs=${2:-0.2} freq=${3:-440} args=()
    shift 3 2>/dev/null || shift $#
    local kv
    for kv in "$@"; do
        args+=(-metadata "$kv")
    done
    command -v ffmpeg >/dev/null || return 1
    mkdir -p "$(dirname "$file")"
    ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=$freq:duration=$secs" "${args[@]}" "$file"
}

# a clean environment: nothing from the developer's own setup can leak into a test
clean_env()
{
    unset TRAKCRATE_PATH TRAKCRATE_HOME TRAKCRATE_LIBRARY TRAKCRATE_FORMAT TRAKCRATE_COLOR TRAKCRATE_JOBS \
          TRAKCRATE_VERBOSE TRAKCRATE_QUIET TRAKCRATE_DRY_RUN TRAKCRATE_YES TRAKCRATE_CONFIG TRAKCRATE_BIN \
          TRAKCRATE_LIBEXEC TRAKCRATE_HELPER TRAKCRATE_API XDG_CONFIG_HOME NO_COLOR
    export HOME=$T/home
    mkdir -p "$HOME"
    export TRAKCRATE_LOCAL_DIR=$T/no-local-dir
    # the helper of this source tree (run.sh builds it); installed trees have their own
    export TRAKCRATE_HELPER=$TC_HELPER
}

run_tests()
{
    local name file_failures=0 count=0 root
    root=$(mktemp -d "${TMPDIR:-/tmp}/trakcrate-test.XXXXXX")
    # shellcheck disable=SC2064
    trap "rm -rf '$root'" EXIT
    printf '%s\n' "$(basename "$0")"
    for name in $(declare -F | awk '$3 ~ /^test_/ { print $3 }'); do
        count=$((count + 1))
        printf '  %s\n' "$name"
        (
            T=$root/$name
            mkdir -p "$T"
            clean_env
            "$name"
            exit "$failures"
        )
        file_failures=$((file_failures + $?))
    done
    printf '  %d tests, %d failed\n' "$count" "$file_failures"
    [ "$file_failures" -eq 0 ]
}
