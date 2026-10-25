#!/bin/bash
# Build a .deb package for vdj-ui.
# Usage: ./make-deb.sh [build-dir]
set -e

BUILD_DIR="${1:-build-release}"

cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --parallel "$(nproc)"
(cd "$BUILD_DIR" && cpack -G DEB)

DEB=$(ls "$BUILD_DIR"/vdj-ui-*.deb 2>/dev/null | head -1)
if [ -n "$DEB" ]
then
    echo "Package: $DEB"
    dpkg-deb --info "$DEB"
else
    echo "No .deb produced — check cpack output above" >&2
    exit 1
fi
