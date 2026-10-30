#!/bin/bash
#
# The wrapper script (src-cli/script/usr/bin/trakcrate) with fake commands: search order, shadowing, exit status
# passthrough, unknown commands, odd arguments, help, configuration and the environment it hands to commands.
#
. "$(dirname "$0")/lib.sh"

# a fake command that prints one line per argument and the settings it was given
ARGS_CMD='printf "[%s]\n" "$@"'
ENV_CMD='for v in TRAKCRATE_BIN TRAKCRATE_LIBEXEC TRAKCRATE_HELPER TRAKCRATE_LIBRARY TRAKCRATE_FORMAT TRAKCRATE_COLOR TRAKCRATE_JOBS TRAKCRATE_VERBOSE TRAKCRATE_QUIET TRAKCRATE_DRY_RUN TRAKCRATE_YES TRAKCRATE_API; do printf "%s=%s\n" "$v" "${!v-<unset>}"; done'

test_version()
{
    run "$TRAKCRATE" --version
    assert_status "--version" 0
    assert_eq "from the source tree the version comes from ./version" "trakcrate $TC_VERSION (api 1)" "$out"

    install_tree "$T/prefix"
    run "$T/prefix/usr/bin/trakcrate" -V
    assert_eq "an installed wrapper reports the version filled in at build time" "trakcrate 9.9.9 (api 1)" "$out"
}

test_no_arguments_is_a_usage_error()
{
    run "$TRAKCRATE"
    assert_status "no arguments" 2
    assert_contains "usage on stderr" "Usage: trakcrate" "$err"
    assert_eq "nothing on stdout" "" "$out"
}

test_help()
{
    run "$TRAKCRATE" --help
    assert_status "--help" 0
    assert_contains "usage on stdout" "Usage: trakcrate" "$out"
    run "$TRAKCRATE" -h
    assert_status "-h" 0
}

test_unknown_command()
{
    make_cmd "$T/cmds" import 'echo import'
    make_cmd "$T/cmds" export 'echo export'
    export TRAKCRATE_PATH=$T/cmds

    run "$TRAKCRATE" foo
    assert_status "unknown command" 2
    assert_contains "message" "'foo' is not a trakcrate command" "$err"
    assert_not_contains "no suggestion when nothing is close" "most similar" "$err"

    run "$TRAKCRATE" improt
    assert_status "typo" 2
    assert_contains "suggests a close command" "    import" "$err"
    assert_not_contains "does not suggest a distant one" "    export" "$err"
    assert_eq "nothing on stdout" "" "$out"
}

test_exit_status_and_streams_pass_through()
{
    make_cmd "$T/cmds" fake 'echo to-stdout; echo to-stderr >&2; exit 7'
    export TRAKCRATE_PATH=$T/cmds
    run "$TRAKCRATE" fake
    assert_status "status of the command" 7
    assert_eq "stdout" "to-stdout" "$out"
    assert_eq "stderr" "to-stderr" "$err"

    make_cmd "$T/cmds" partial 'exit 3'
    run "$TRAKCRATE" partial
    assert_status "partial success" 3
    make_cmd "$T/cmds" busy 'exit 4'
    run "$TRAKCRATE" busy
    assert_status "library busy" 4
}

test_stdin_passes_through()
{
    make_cmd "$T/cmds" cat 'cat'
    export TRAKCRATE_PATH=$T/cmds
    stdin_data="line one"
    run_stdin "$TRAKCRATE" cat
    assert_eq "stdin reaches the command" "line one" "$out"
}

test_arguments_with_spaces_and_dashes()
{
    make_cmd "$T/cmds" args "$ARGS_CMD"
    export TRAKCRATE_PATH=$T/cmds
    run "$TRAKCRATE" args "a b" --x -- -n "" '*' --library=elsewhere -v
    assert_status "args" 0
    assert_eq "every argument arrives untouched" "$(printf '[%s]\n' 'a b' --x -- -n '' '*' --library=elsewhere -v)" "$out"
}

test_global_options_stop_at_the_command()
{
    make_cmd "$T/cmds" args "$ARGS_CMD"
    export TRAKCRATE_PATH=$T/cmds
    run "$TRAKCRATE" -q --jobs 3 args -q --jobs 9
    assert_eq "options after the command belong to it" "$(printf '[%s]\n' -q --jobs 9)" "$out"

    run "$TRAKCRATE" -- args -q
    assert_eq "-- ends the global options" "[-q]" "$out"

    run "$TRAKCRATE" --jobs
    assert_status "an option without its value" 2
    run "$TRAKCRATE" --unknown args
    assert_status "an unknown global option" 2
}

test_search_order_and_shadowing()
{
    make_cmd "$T/dev" which 'echo dev'
    make_cmd "$T/local" which 'echo local'
    make_cmd "$T/local" only-local 'echo only-local'
    export TRAKCRATE_PATH=$T/dev
    export TRAKCRATE_LOCAL_DIR=$T/local
    install_tree "$T/prefix"
    local w=$T/prefix/usr/bin/trakcrate

    run "$w" which
    assert_eq "TRAKCRATE_PATH wins over the administrator's directory" "dev" "$out"
    run "$w" only-local
    assert_eq "the administrator's directory is searched" "only-local" "$out"

    run "$w" --list-commands
    assert_status "--list-commands" 0
    assert_contains "winner with its directory and what it shadows" "$(printf 'which\t%s\tshadows %s' "$T/dev" "$T/local")" "$out"
    assert_contains "a command that shadows nothing" "$(printf 'only-local\t%s' "$T/local")" "$out"
    assert_eq "one line per command" "2" "$(printf '%s\n' "$out" | wc -l)"
}

test_installed_layout()
{
    install_tree "$T/prefix"
    make_cmd "$T/prefix/usr/lib/trakcrate" packaged 'echo "packaged $TRAKCRATE_LIBEXEC"'
    make_cmd "$T/prefix/usr/lib/trakcrate" both 'echo packaged-both'
    make_cmd "$T/prefix/usr/lib/trakcrate" helper 'echo not-a-command'
    make_cmd "$T/prefix/usr/lib/trakcrate" help 'echo not-a-command'
    make_cmd "$T/local" both 'echo local-both'
    export TRAKCRATE_LOCAL_DIR=$T/local
    local w=$T/prefix/usr/bin/trakcrate

    run "$w" packaged
    assert_eq "the package's commands are found next to the wrapper" "packaged $T/prefix/usr/lib/trakcrate" "$out"
    run "$w" both
    assert_eq "a local command overrides a packaged one" "local-both" "$out"
    run "$w" --list-commands
    assert_contains "shadowing of a packaged command is flagged" "$(printf 'both\t%s\tshadows %s' "$T/local" "$T/prefix/usr/lib/trakcrate")" "$out"
    assert_not_contains "the helper is not a command" "helper" "$out"
    run "$w" helper
    assert_status "the helper cannot be run as a command" 2
}

test_not_found_in_path_or_home()
{
    make_cmd "$T/bin" sneaky 'echo run-from-PATH'
    make_cmd "$HOME/.local/lib/trakcrate" sneaky 'echo run-from-HOME'
    make_cmd "$HOME" sneaky 'echo run-from-HOME'
    export PATH=$T/bin:$PATH
    run "$TRAKCRATE" sneaky
    assert_status "\$PATH and \$HOME are not searched" 2
    assert_eq "nothing ran" "" "$out"
}

test_bad_command_names()
{
    make_cmd "$T/cmds" upper 'echo ran'
    cp "$T/cmds/trakcrate-upper" "$T/cmds/trakcrate-Upper"
    export TRAKCRATE_PATH=$T/cmds
    local n
    for n in '../x' 'Upper' '-x' 'a/b' '' 'x y'; do
        run "$TRAKCRATE" -- "$n"
        assert_status "bad name '$n'" 2
    done
}

test_not_executable_is_ignored()
{
    mkdir -p "$T/cmds"
    printf '#!/bin/bash\necho ran\n' > "$T/cmds/trakcrate-plain"
    export TRAKCRATE_PATH=$T/cmds
    install_tree "$T/prefix"
    run "$T/prefix/usr/bin/trakcrate" plain
    assert_status "a file without the execute bit" 2
    run "$T/prefix/usr/bin/trakcrate" --list-commands
    assert_eq "is not listed" "" "$out"
}

test_help_for_a_command()
{
    make_cmd "$T/cmds" fake 'case "$1" in --help) echo "usage of fake";; --summary) echo "does fake things";; *) echo ran;; esac'
    export TRAKCRATE_PATH=$T/cmds

    run "$TRAKCRATE" help fake
    assert_status "help fake" 0
    assert_eq "help <command> runs <command> --help" "usage of fake" "$out"
    run "$TRAKCRATE" -h fake
    assert_eq "-h <command> too" "usage of fake" "$out"
    run "$TRAKCRATE" help
    assert_contains "help lists the commands with their summaries" "fake" "$out"
    assert_contains "summary" "does fake things" "$out"
    run "$TRAKCRATE" help nonesuch
    assert_status "help for an unknown command" 2
}

test_environment_for_commands()
{
    make_cmd "$T/cmds" env "$ENV_CMD"
    export TRAKCRATE_PATH=$T/cmds
    TRAKCRATE_HELPER=/preset/helper run "$TRAKCRATE" env
    assert_contains "a preset helper is kept (tests, development builds)" "TRAKCRATE_HELPER=/preset/helper" "$out"
    unset TRAKCRATE_HELPER
    run "$TRAKCRATE" --library "$T/lib" env
    assert_status "env" 0
    assert_contains "TRAKCRATE_BIN" "TRAKCRATE_BIN=$(readlink -f "$TRAKCRATE")" "$out"
    assert_contains "TRAKCRATE_LIBEXEC" "TRAKCRATE_LIBEXEC=$TC_ROOT/script/usr/lib/trakcrate" "$out"
    assert_contains "TRAKCRATE_HELPER" "TRAKCRATE_HELPER=$TC_ROOT/script/usr/lib/trakcrate/trakcrate-helper" "$out"
    assert_contains "TRAKCRATE_LIBRARY" "TRAKCRATE_LIBRARY=$T/lib" "$out"
    assert_contains "default format" "TRAKCRATE_FORMAT=text" "$out"
    assert_contains "default color" "TRAKCRATE_COLOR=auto" "$out"
    assert_contains "API version" "TRAKCRATE_API=1" "$out"
    assert_contains "verbose defaults to 0" "TRAKCRATE_VERBOSE=0" "$out"
    assert_contains "quiet is unset" "TRAKCRATE_QUIET=<unset>" "$out"
    assert_contains "dry run is unset" "TRAKCRATE_DRY_RUN=<unset>" "$out"
    assert_contains "yes is unset" "TRAKCRATE_YES=<unset>" "$out"
    assert_contains "jobs is the number of CPUs" "TRAKCRATE_JOBS=$(nproc)" "$out"

    run "$TRAKCRATE" -q -vv -v -n --yes --format jsonl --color never -j 3 env
    assert_contains "quiet" "TRAKCRATE_QUIET=1" "$out"
    assert_contains "-vv -v counts to 3" "TRAKCRATE_VERBOSE=3" "$out"
    assert_contains "dry run" "TRAKCRATE_DRY_RUN=1" "$out"
    assert_contains "yes" "TRAKCRATE_YES=1" "$out"
    assert_contains "format" "TRAKCRATE_FORMAT=jsonl" "$out"
    assert_contains "color" "TRAKCRATE_COLOR=never" "$out"
    assert_contains "jobs" "TRAKCRATE_JOBS=3" "$out"

    run "$TRAKCRATE" --jobs=5 --format=tsv --color=always env
    assert_contains "--opt=value: jobs" "TRAKCRATE_JOBS=5" "$out"
    assert_contains "--opt=value: format" "TRAKCRATE_FORMAT=tsv" "$out"
    assert_contains "--opt=value: color" "TRAKCRATE_COLOR=always" "$out"
}

test_bad_option_values()
{
    make_cmd "$T/cmds" env 'echo ran'
    export TRAKCRATE_PATH=$T/cmds
    run "$TRAKCRATE" --format xml env
    assert_status "bad format" 2
    run "$TRAKCRATE" --color purple env
    assert_status "bad color" 2
    run "$TRAKCRATE" --jobs 0 env
    assert_status "zero jobs" 2
    run "$TRAKCRATE" --jobs many env
    assert_status "jobs that is not a number" 2
    assert_eq "no command ran" "" "$out"
}

test_library_precedence()
{
    make_cmd "$T/cmds" lib 'echo "$TRAKCRATE_LIBRARY"'
    export TRAKCRATE_PATH=$T/cmds
    local conf=$T/conf
    printf 'library=%s\n' "$T/from-config" > "$conf"

    run "$TRAKCRATE" lib
    assert_eq "default is ~/trakcrate" "$HOME/trakcrate" "$out"
    run "$TRAKCRATE" --config "$conf" lib
    assert_eq "configuration file" "$T/from-config" "$out"
    TRAKCRATE_HOME=$T/from-home run "$TRAKCRATE" --config "$conf" lib
    assert_eq "TRAKCRATE_HOME beats configuration" "$T/from-home" "$out"
    TRAKCRATE_LIBRARY=$T/from-env TRAKCRATE_HOME=$T/from-home run "$TRAKCRATE" --config "$conf" lib
    assert_eq "TRAKCRATE_LIBRARY beats TRAKCRATE_HOME" "$T/from-env" "$out"
    TRAKCRATE_LIBRARY=$T/from-env run "$TRAKCRATE" --library "$T/from-option" lib
    assert_eq "--library beats everything" "$T/from-option" "$out"
    cd "$T" || return
    run "$TRAKCRATE" --library relative/../lib2 lib
    assert_eq "a relative path is made absolute" "$T/lib2" "$out"
    run "$TRAKCRATE" --library "$T/with space" lib
    assert_eq "a library with a space" "$T/with space" "$out"
}

test_configuration_files()
{
    make_cmd "$T/cmds" cfg 'echo "$TRAKCRATE_LIBRARY|$TRAKCRATE_FORMAT|$TRAKCRATE_JOBS|$TRAKCRATE_COLOR"'
    export TRAKCRATE_PATH=$T/cmds
    install_tree "$T/prefix"
    local w=$T/prefix/usr/bin/trakcrate

    cat > "$T/prefix/etc/trakcrate/trakcrate.conf" <<'EOF'
# system defaults
library = ~/sys-library
format=tsv
jobs=2
color=never
unknown_key=whatever
this line has no equals sign
EOF
    run "$w" cfg
    assert_eq "system configuration, ~/ expanded, unknown keys and junk ignored" "$HOME/sys-library|tsv|2|never" "$out"

    mkdir -p "$HOME/.config/trakcrate"
    printf 'format=jsonl\n  jobs =  6  \n' > "$HOME/.config/trakcrate/config"
    run "$w" cfg
    assert_eq "the user file overrides the system file, per key" "$HOME/sys-library|jsonl|6|never" "$out"

    XDG_CONFIG_HOME=$T/xdg run "$w" cfg
    assert_eq "XDG_CONFIG_HOME moves the user file" "$HOME/sys-library|tsv|2|never" "$out"

    TRAKCRATE_FORMAT=json run "$w" cfg
    assert_eq "the environment beats configuration" "$HOME/sys-library|json|6|never" "$out"
    run "$w" --format text --jobs 1 cfg
    assert_eq "the command line beats the environment" "$HOME/sys-library|text|1|never" "$out"

    printf 'format=bogus\n' > "$HOME/.config/trakcrate/config"
    run "$w" cfg
    assert_status "an invalid value in configuration is a usage error" 2

    # a configuration file is data: it is never run
    printf 'library=$(touch %s/pwned)\n' "$T" > "$HOME/.config/trakcrate/config"
    run "$w" cfg
    assert_no_file "configuration is never evaluated" "$T/pwned"
}

test_commands_can_call_back()
{
    make_cmd "$T/cmds" outer '"$TRAKCRATE_BIN" inner "$@"'
    make_cmd "$T/cmds" inner 'echo "inner $TRAKCRATE_LIBRARY $TRAKCRATE_FORMAT $*"'
    export TRAKCRATE_PATH=$T/cmds
    run "$TRAKCRATE" --library "$T/lib" --format tsv outer "a b"
    assert_status "nested call" 0
    assert_eq "the settings survive the round trip" "inner $T/lib tsv a b" "$out"
}

test_leaves_nothing_behind()
{
    run "$TRAKCRATE" --version
    run "$TRAKCRATE" --list-commands
    run "$TRAKCRATE" help
    assert_eq "nothing is created in the home directory" "" "$(ls -A "$HOME")"
}

run_tests
