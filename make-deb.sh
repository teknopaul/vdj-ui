#!/bin/bash
# Build a .deb package for vdj-ui.
# Usage: ./make-deb.sh [build-dir]
set -e

BUILD_DIR="${1:-build-release}"

# ./version holds VERSION=x.y.z; CMake reads the same file for CPACK_PACKAGE_VERSION, the man page and --version
cd "$(dirname "$0")"
. ./version
echo "Building vdj-ui $VERSION"

cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --parallel "$(nproc)"
(cd "$BUILD_DIR" && cpack -G DEB)

DEB=$(ls "$BUILD_DIR"/vdj-ui-"$VERSION"*.deb 2>/dev/null | head -1)
if [ -n "$DEB" ]
then
    echo "Package: $DEB"
    dpkg-deb --info "$DEB"
else
    echo "No .deb produced — check cpack output above" >&2
    exit 1
fi
