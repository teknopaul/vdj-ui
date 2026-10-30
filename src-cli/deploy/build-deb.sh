#!/bin/bash
#
# Build the trakcrate-cli .deb:  src-cli/deploy/build-deb.sh
#
# src-cli/script/ is the root of the installed system, so the staging tree is a copy of it plus the
# compiled helper and the version number. No root needed (dpkg-deb --root-owner-group).
# Output: src-cli/target/trakcrate-cli_<version>_<arch>.deb
#
set -eu

cd "$(dirname "$0")/.."
cli_root=$PWD

if ! command -v dpkg-deb >/dev/null; then
    echo "build-deb.sh: dpkg-deb not found" >&2
    exit 1
fi

# ../version holds VERSION=x.y.z, the one place for the number
version=$(sed -n 's/^VERSION=//p' ../version | head -n 1)
if [ -z "$version" ]; then
    echo "build-deb.sh: no VERSION in ../version" >&2
    exit 1
fi
arch=$(dpkg --print-architecture)
name=trakcrate-cli

echo "Building $name $version ($arch)"
make -C c

stage=$cli_root/target/deb-staging
deb=$cli_root/target/${name}_${version}_${arch}.deb
rm -rf "$stage"
mkdir -p "$stage/DEBIAN"

# the tree of the installed system
cp -a script/. "$stage/"
find "$stage" -name .gitkeep -delete

# the compiled helper, beside the bash commands
make -C c install DESTDIR="$stage"

# an empty directory for the administrator's own commands (Debian policy: created, never removed)
mkdir -p "$stage/usr/local/lib/trakcrate"

# the version number goes into the wrapper
sed -i "s/@VERSION@/$version/" "$stage/usr/bin/trakcrate"
if grep -q '@VERSION@' "$stage/usr/bin/trakcrate"; then
    echo "build-deb.sh: version placeholder left in the wrapper" >&2
    exit 1
fi

# permissions: directories 755, files 644 unless they are programs
find "$stage" -type d -exec chmod 755 {} +
find "$stage" -type f -exec chmod go-w {} +

# conffiles: everything under /etc
(
    cd "$stage"
    find etc -type f | sed 's|^|/|' > DEBIAN/conffiles
)

# the packages of the shared libraries the helper needs directly (ffmpeg, libc); the rest follows from their own dependencies
helper_bin=$stage/usr/lib/trakcrate/trakcrate-helper
shlibs=$(
    readelf -d "$helper_bin" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | while read -r soname; do
        ldd "$helper_bin" | awk -v n="$soname" '$1 == n { print $3 }'
    done | xargs -r -n1 readlink -f | xargs -r dpkg -S 2>/dev/null | sed -n 's/^\([^:]*\):.*/\1/p' | sort -u | paste -sd, - | sed 's/,/, /g'
)
# libcdj has its own version, separate from ours (references/libcdj/version)
shlibs=$(printf '%s' "$shlibs" | sed 's/\(^\|, \)libcdj\(,\|$\)/\1libcdj (>= 0.1)\2/')

size=$(du -sk "$stage" | cut -f 1)
sed -e "s/@VERSION@/$version/" -e "s/@ARCH@/$arch/" -e "s/@SIZE@/$size/" -e "s/@SHLIBS@/$shlibs/" deploy/DEBIAN/control.in > "$stage/DEBIAN/control"

mkdir -p "$cli_root/target"
dpkg-deb --root-owner-group --build "$stage" "$deb" >/dev/null

test -f "$deb"
echo "built $deb"
