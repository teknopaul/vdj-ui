#!/bin/bash
#
# The .deb: build it, look inside, unpack it into a scratch root and run it there.
# (A full install into a clean container is the release check; this is what can be done without root.)
#
. "$(dirname "$0")/lib.sh"

if ! command -v dpkg-deb >/dev/null; then
    echo "test_package.sh: dpkg-deb not found, skipped"
    exit 0
fi

build_deb()
{
    "$TC_ROOT/deploy/build-deb.sh" >"$T/build.log" 2>&1
    deb=$(ls "$TC_ROOT"/target/trakcrate-cli_"$TC_VERSION"_*.deb 2>/dev/null | head -n 1)
}

test_package_contents()
{
    build_deb
    assert_file "the .deb is built, named after the version" "$deb"
    local files
    files=$(dpkg-deb -c "$deb")
    assert_contains "wrapper" " ./usr/bin/trakcrate" "$files"
    assert_contains "helper" " ./usr/lib/trakcrate/trakcrate-helper" "$files"
    assert_contains "configuration" " ./etc/trakcrate/trakcrate.conf" "$files"
    assert_contains "empty directory for local commands" " ./usr/local/lib/trakcrate/" "$files"
    assert_not_contains "no placeholder file is shipped" ".gitkeep" "$files"
    assert_not_contains "nothing in a home directory" " ./home" "$files"
    assert_not_contains "nothing in /root" " ./root" "$files"

    local control
    control=$(dpkg-deb -f "$deb")
    assert_contains "package name" "Package: trakcrate-cli" "$control"
    assert_contains "version comes from ./version" "Version: $TC_VERSION" "$control"
    local depends
    depends=$(dpkg-deb -f "$deb" Depends | tr 'A-Z' 'a-z')
    assert_not_contains "no Qt dependency" "qt" "$depends"
    assert_contains "the ffmpeg library of the helper" "libavformat" "$depends"
    assert_not_contains "no dependency on the GUI package" "vdj-ui" "$depends"
    assert_eq "the configuration is a conffile" "/etc/trakcrate/trakcrate.conf" "$(dpkg-deb -I "$deb" conffiles)"
}

test_unpacked_package_runs()
{
    build_deb
    local root=$T/root
    mkdir -p "$root"
    dpkg-deb -x "$deb" "$root"

    run "$root/usr/bin/trakcrate" --version
    assert_status "wrapper" 0
    assert_eq "the wrapper carries the version" "trakcrate $TC_VERSION (api 1)" "$out"
    assert_not_contains "no placeholder left" "@VERSION@" "$(cat "$root/usr/bin/trakcrate")"

    run "$root/usr/lib/trakcrate/trakcrate-helper" --version
    assert_status "helper" 0
    assert_eq "the helper carries the version" "trakcrate-helper $TC_VERSION (api 1)" "$out"

    assert_eq "the helper links no Qt" "" "$(ldd "$root/usr/lib/trakcrate/trakcrate-helper" | grep -i qt || true)"

    export TRAKCRATE_LOCAL_DIR=$root/usr/local/lib/trakcrate
    run "$root/usr/bin/trakcrate" --list-commands
    assert_status "--list-commands on the installed tree" 0
    assert_not_contains "the helper is not a command" "helper" "$out"
    assert_contains "the bash commands are installed" "$(printf 'init\t%s' "$root/usr/lib/trakcrate")" "$out"
    assert_file "the shared library of the commands" "$root/usr/lib/trakcrate/common.sh"

    # an administrator's command in the (empty) local directory is found
    make_cmd "$root/usr/local/lib/trakcrate" mine 'echo mine'
    run "$root/usr/bin/trakcrate" mine
    assert_eq "a command added to /usr/local/lib/trakcrate" "mine" "$out"

    assert_eq "nothing is created in the home directory" "" "$(ls -A "$HOME")"
}

run_tests
