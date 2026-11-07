#!/bin/bash
#
# Builds the C++ oracle (cxx_analyse) against the GUI's static library: build/src/libvdj-trakcrate.a (cmake build of vdj-ui).
# Prints the path of the binary; exits 1 (quietly) when the library is not there.
#
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
lib=$root/build/src/libvdj-trakcrate.a
out=$root/src-cli/target/cxx_analyse
out_export=$root/src-cli/target/cxx_export
out_append=$root/src-cli/target/cxx_append
out_add=$root/src-cli/target/cxx_add

if [ ! -f "$lib" ]; then
    exit 1
fi
libs="$(pkg-config --libs libavformat libavcodec libavutil libswresample) $(pkg-config --libs aubio 2>/dev/null || true) -lcdj"
build()
{
    # build SRC OUT
    if [ ! -x "$2" ] || [ "$lib" -nt "$2" ] || [ "$here/$1" -nt "$2" ]; then
        mkdir -p "$(dirname "$2")"
        # shellcheck disable=SC2086
        g++ -std=c++17 -O1 -I"$root/src" "$here/$1" "$lib" $libs -o "$2" >/dev/null 2>&1 || exit 1
    fi
}
# `build.sh export` builds the exporter oracle, `build.sh append` the database editor oracle, `build.sh add` the add-to-drive oracle, else the analyser
if [ "${1:-}" = add ]; then
    build cxx_add.cpp "$out_add"
    printf '%s\n' "$out_add"
elif [ "${1:-}" = append ]; then
    build cxx_append.cpp "$out_append"
    printf '%s\n' "$out_append"
elif [ "${1:-}" = export ]; then
    build cxx_export.cpp "$out_export"
    printf '%s\n' "$out_export"
else
    build cxx_analyse.cpp "$out"
    printf '%s\n' "$out"
fi
