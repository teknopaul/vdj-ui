#!/bin/bash
#
# Shared by the trakcrate bash commands (doc/trakcrate/cli.md). Sourced, not run:
#
#   . "${TRAKCRATE_LIBEXEC:?run through the trakcrate wrapper}/common.sh"
#
# What it gives a command: messages, the common options, the library, track lookup, and the
# rules of the key/value files of TRAKCRATE-FORMAT.md (atomic writes, preserved comments).
#

tc_cmd=${0##*/}
tc_cmd=${tc_cmd#trakcrate-}

tc_err()
{
    printf 'trakcrate %s: %s\n' "$tc_cmd" "$*" >&2
}

# exit status 1: runtime failure
tc_die()
{
    tc_err "$@"
    exit 1
}

# exit status 2: usage error
tc_usage_err()
{
    tc_err "$@"
    exit 2
}

# chatter on stderr, silenced by -q
tc_say()
{
    if [ -z "${TRAKCRATE_QUIET:-}" ]; then
        printf '%s\n' "$*" >&2
    fi
}

# only with -v
tc_debug()
{
    if [ "${TRAKCRATE_VERBOSE:-0}" -gt 0 ]; then
        printf '%s\n' "$*" >&2
    fi
}

#
# Options: the global ones the wrapper sets can also be given after the command.
# tc_opt_value NAME ARGS...  sets tc_value and tc_shift for "--name value" and "--name=value"
#
tc_opt_value()
{
    local name=$1
    shift
    case $1 in
        "$name"=*)
            tc_value=${1#*=}
            tc_shift=1
            ;;
        *)
            if [ $# -lt 2 ]; then
                tc_usage_err "option $name needs a value"
            fi
            tc_value=$2
            tc_shift=2
            ;;
    esac
}

# tc_common_opt ARGS...  returns 0 and sets tc_shift when the first argument is a common option
tc_common_opt()
{
    tc_shift=1
    case $1 in
        -q|--quiet) TRAKCRATE_QUIET=1 ;;
        -v|--verbose) TRAKCRATE_VERBOSE=$((${TRAKCRATE_VERBOSE:-0} + 1)) ;;
        -n|--dry-run) TRAKCRATE_DRY_RUN=1 ;;
        --yes) TRAKCRATE_YES=1 ;;
        --format|--format=*)
            tc_opt_value --format "$@"
            case $tc_value in
                text|tsv|json|jsonl|paths) TRAKCRATE_FORMAT=$tc_value ;;
                *) tc_usage_err "bad format '$tc_value' (text, tsv, json, jsonl or paths)" ;;
            esac
            ;;
        --library|--library=*)
            tc_opt_value --library "$@"
            TRAKCRATE_LIBRARY=$(readlink -m -- "$tc_value")
            ;;
        *) return 1 ;;
    esac
    return 0
}

# the usual response to --help and --summary: tc_help_or_summary "one line summary" "usage text" ARGS...
tc_help_or_summary()
{
    local summary=$1 usage=$2
    shift 2
    case ${1:-} in
        --summary) printf '%s\n' "$summary"; exit 0 ;;
        --help|-h) printf '%s\n' "$usage"; exit 0 ;;
    esac
}

#
# The library
#
tc_format=${TRAKCRATE_FORMAT:-text}

# tc_library: sets tc_lib and tc_master, dies unless there is a library
tc_library()
{
    tc_lib=${TRAKCRATE_LIBRARY:-}
    if [ -z "$tc_lib" ]; then
        tc_usage_err "no library: use --library DIR or run through the trakcrate wrapper"
    fi
    if [ ! -d "$tc_lib/track-master-copy" ]; then
        tc_die "$tc_lib is not a trakcrate library (no track-master-copy/); create one with 'trakcrate init'"
    fi
    tc_master=$tc_lib/track-master-copy
}

# a name that is one path component (TRAKCRATE-FORMAT.md 2.1)
tc_safe_name()
{
    case $1 in
        ''|.|..|*/*|*$'\n'*) return 1 ;;
    esac
    return 0
}

# tc_track TRACK: sets tc_track_dir to the track directory.
# TRACK is the directory itself, or folder/track below track-master-copy, or a track name that is unique in the library.
tc_track()
{
    local arg=$1 candidate real found
    local -a hits
    if [ -z "$arg" ]; then
        tc_usage_err "an empty track name"
    fi
    if [ -d "$arg" ]; then
        candidate=$arg
    elif [ -d "$tc_master/$arg" ] && [[ $arg != *..* ]]; then
        candidate=$tc_master/$arg
    else
        hits=()
        if [[ $arg != */* ]] && [ "$arg" != . ] && [ "$arg" != .. ]; then
            for found in "$tc_master"/*/"$arg"; do
                if [ -d "$found" ]; then
                    hits+=("$found")
                fi
            done
        fi
        if [ "${#hits[@]}" -eq 1 ]; then
            candidate=${hits[0]}
        elif [ "${#hits[@]}" -gt 1 ]; then
            tc_die "'$arg' is ambiguous: ${hits[*]#"$tc_master"/}"
        else
            tc_die "no such track: $arg"
        fi
    fi
    real=$(readlink -f -- "$candidate")
    case $real in
        "$(readlink -f -- "$tc_master")"/*/*) ;;
        *) tc_die "$arg is not a track of the library $tc_lib" ;;
    esac
    tc_track_dir=$real
}

# the compiled helper (audio, natural sort): dies when it is missing
tc_helper()
{
    if [ -z "${TRAKCRATE_HELPER:-}" ] || [ ! -x "$TRAKCRATE_HELPER" ]; then
        tc_die "the helper program is missing (${TRAKCRATE_HELPER:-TRAKCRATE_HELPER is not set}); is trakcrate-cli installed completely?"
    fi
}

# the directories of all tracks, one per line, in the natural name order of TRAKCRATE-FORMAT.md 2.1
tc_all_tracks()
{
    local d
    tc_helper
    for d in "$tc_master"/*/*/; do
        if [ -d "$d" ]; then
            printf '%s\0' "${d%/}"
        fi
    done | "$TRAKCRATE_HELPER" natsort -z | tr '\0' '\n'
}

#
# Files: atomic replacement (TRAKCRATE-FORMAT.md section 1, rule 5)
#
# tc_write_atomic FILE  < new content
tc_write_atomic()
{
    local dest=$1 dir base tmp
    dir=$(dirname -- "$dest")
    base=$(basename -- "$dest")
    tmp=$dir/.$base.tmp.$$
    if ! cat > "$tmp"; then
        rm -f "$tmp"
        return 1
    fi
    if ! mv -f -- "$tmp" "$dest"; then
        rm -f "$tmp"
        return 1
    fi
}

#
# Key/value files (meta.txt, source.txt, beatgrid.txt): "key: value", # comments, \\ and \n escapes,
# the last of a repeated key wins, comments and unknown keys survive a rewrite.
#
# tc_kv_get FILE KEY   prints the value; status 1 when the file or key is missing
tc_kv_get()
{
    if [ ! -r "$1" ]; then
        return 1
    fi
    awk -v key="$2" '
        function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); return s }
        function unesc(s,   out, i, c, n) {
            out = ""
            for (i = 1; i <= length(s); i++) {
                c = substr(s, i, 1)
                if (c == "\\" && i < length(s)) {
                    n = substr(s, i + 1, 1)
                    if (n == "n") { out = out "\n"; i++; continue }
                    if (n == "\\") { out = out "\\"; i++; continue }
                }
                out = out c
            }
            return out
        }
        { sub(/\r$/, "") }
        /^[ \t]*(#|$)/ { next }
        {
            i = index($0, ":")
            if (i == 0) next
            if (trim(substr($0, 1, i - 1)) == key) { val = trim(substr($0, i + 1)); found = 1 }
        }
        END { if (!found) exit 1; print unesc(val) }
    ' "$1"
}

# tc_kv_set FILE KEY VALUE   replaces the last line of that key in place, or appends; creates the file
tc_kv_set()
{
    local file=$1
    if [ ! -e "$file" ]; then
        : > "$file"
    fi
    TC_KEY=$2 TC_VAL=$3 awk '
        function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); return s }
        function esc(s,   out, i, c) {
            out = ""
            for (i = 1; i <= length(s); i++) {
                c = substr(s, i, 1)
                if (c == "\\") out = out "\\\\"
                else if (c == "\n") out = out "\\n"
                else out = out c
            }
            return out
        }
        function keyof(line,   i) {
            if (line ~ /^[ \t]*(#|$)/) return ""
            i = index(line, ":")
            if (i == 0) return ""
            return trim(substr(line, 1, i - 1))
        }
        BEGIN { key = ENVIRON["TC_KEY"]; val = ENVIRON["TC_VAL"] }
        FNR == NR { if (keyof($0) == key) last = FNR; next }
        { if (FNR == last) print key ": " esc(val); else print $0 }
        END { if (!last) print key ": " esc(val) }
    ' "$file" "$file" | tc_write_atomic "$file"
}

# tc_kv_unset FILE KEY   removes every line of that key
tc_kv_unset()
{
    if [ ! -e "$1" ]; then
        return 0
    fi
    TC_KEY=$2 awk '
        function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); return s }
        BEGIN { key = ENVIRON["TC_KEY"] }
        {
            if ($0 !~ /^[ \t]*(#|$)/) {
                i = index($0, ":")
                if (i > 0 && trim(substr($0, 1, i - 1)) == key) next
            }
            print $0
        }
    ' "$1" | tc_write_atomic "$1"
}

# JSON string, with quotes, from stdin-free argument
tc_json_str()
{
    TC_S=$1 awk 'BEGIN {
        s = ENVIRON["TC_S"]; out = "\""
        for (i = 1; i <= length(s); i++) {
            c = substr(s, i, 1)
            if (c == "\"") out = out "\\\""
            else if (c == "\\") out = out "\\\\"
            else if (c == "\n") out = out "\\n"
            else if (c == "\t") out = out "\\t"
            else if (c == "\r") out = out "\\r"
            else out = out c
        }
        printf "%s\"", out
    }'
}

# playlist entries (symlinks) in order: prints the link names of PLAYLIST, ordered by prefix
# entries without a numeric prefix sort after the numbered ones
tc_playlist_entries()
{
    local dir=$1/symlinks f n
    if [ ! -d "$dir" ]; then
        return 0
    fi
    for f in "$dir"/*; do
        if [ -L "$f" ] || [ -e "$f" ]; then
            n=${f##*/}
            if [[ $n =~ ^[0-9]+_ ]]; then
                printf '0\t%s\t%s\n' "$((10#${n%%_*}))" "$n"
            else
                printf '1\t0\t%s\n' "$n"
            fi
        fi
    done | LC_ALL=C sort -t "$(printf '\t')" -k1,1n -k2,2n -k3,3 | cut -f3
}

# the width of the numeric prefix for COUNT entries: at least 3 digits
tc_prefix_width()
{
    local w=${#1}
    if [ "$w" -lt 3 ]; then
        w=3
    fi
    printf '%s\n' "$w"
}

#
# The library lock (TRAKCRATE-FORMAT.md section 16): <library>/.trakcrate-lock holds the owner's pid.
# tc_lock exits with status 4 (library busy) when a live process holds it; a stale lock is taken over.
# Dry runs and readers do not lock. The lock is released on every exit of the command.
#
tc_locked=
tc_lock()
{
    local lockfile=$tc_lib/.trakcrate-lock pid attempt
    if [ -n "${TRAKCRATE_DRY_RUN:-}" ]; then
        return 0
    fi
    for attempt in 1 2; do
        # noclobber makes the redirection an exclusive create
        if ( set -o noclobber; printf '%s\n' "$$" > "$lockfile" ) 2>/dev/null; then
            tc_locked=1
            trap tc_unlock EXIT
            return 0
        fi
        pid=$(cat -- "$lockfile" 2>/dev/null || true)
        if [ "$attempt" = 1 ] && { ! [[ $pid =~ ^[0-9]+$ ]] || [ ! -d "/proc/$pid" ]; }; then
            rm -f -- "$lockfile"
            continue
        fi
        tc_err "library busy: $lockfile is held by process $pid"
        exit 4
    done
    tc_err "library busy: cannot take $lockfile"
    exit 4
}

tc_unlock()
{
    if [ -n "$tc_locked" ] && [ "$(cat -- "$tc_lib/.trakcrate-lock" 2>/dev/null || true)" = "$$" ]; then
        rm -f -- "$tc_lib/.trakcrate-lock"
    fi
    tc_locked=
}

# tc_confirm QUESTION: yes with --yes, asks on a terminal, otherwise no (no prompts in scripts)
tc_confirm()
{
    local answer
    if [ -n "${TRAKCRATE_YES:-}" ]; then
        return 0
    fi
    if [ -t 0 ] && [ -t 2 ]; then
        printf '%s [y/N] ' "$1" >&2
        read -r answer || return 1
        case $answer in
            y|Y|yes|YES) return 0 ;;
        esac
        return 1
    fi
    tc_err "$1 (not asked: no terminal; use --yes)"
    return 1
}

# a track or folder name from a file name (TRAKCRATE-FORMAT.md 2.1): control characters and / become _, no leading
# dots or spaces, no trailing spaces, at most 200 bytes without cutting a UTF-8 sequence
tc_sanitize_name()
{
    local s=$1
    s=${s//\//_}
    s=$(printf '%s' "$s" | tr '\000-\037\177' '_')
    s=${s#"${s%%[!. ]*}"}
    s=${s%"${s##*[! ]}"}
    if [ "${#s}" -gt 0 ] && [ "$(printf '%s' "$s" | wc -c)" -gt 200 ]; then
        s=$(printf '%s' "$s" | head -c 200 | iconv -c -f UTF-8 -t UTF-8)
    fi
    if [ -z "$s" ]; then
        s=track
    fi
    printf '%s\n' "$s"
}

# tc_playlist_write PLAYLIST_DIR NAMES_ARRAY LINKS_ARRAY: replaces the symlinks folder by one with these entries in this
# order, numbered consecutively. The new folder is built aside and swapped in: a reader sees the old or the new one.
tc_playlist_write()
{
    local dir=$1
    local -n names_ref=$2 links_ref=$3
    local count=${#names_ref[@]} last width i
    local new=$dir/.symlinks.new.$$ old=$dir/.symlinks.old.$$
    last=$((count > 0 ? count - 1 : 0))
    width=$(tc_prefix_width "$last")
    rm -rf -- "$new"
    mkdir -p -- "$new"
    for i in "${!names_ref[@]}"; do
        ln -s -- "${links_ref[$i]}" "$(printf '%s/%0*d_%s' "$new" "$width" "$i" "${names_ref[$i]}")"
    done
    if [ -d "$dir/symlinks" ]; then
        mv -- "$dir/symlinks" "$old"
    fi
    mv -- "$new" "$dir/symlinks"
    rm -rf -- "$old"
}

# tc_playlist_load PLAYLIST_DIR NAMES_ARRAY LINKS_ARRAY: the entries in order, names without the numeric prefix, links as stored
tc_playlist_load()
{
    local dir=$1 entry
    local -n names_ref=$2 links_ref=$3
    names_ref=()
    links_ref=()
    while IFS= read -r entry; do
        if [[ $entry =~ ^[0-9]+_ ]]; then
            names_ref+=("${entry#*_}")
        else
            names_ref+=("$entry")
        fi
        links_ref+=("$(readlink -- "$dir/symlinks/$entry" || true)")
    done < <(tc_playlist_entries "$dir")
}

# the relative link from a playlist's symlinks folder to a track directory
tc_track_link()
{
    printf '../../../track-master-copy/%s\n' "${1#"$tc_master"/}"
}

# tc_playlist_dirs PLAYLIST_DIR ARRAY: the resolved track directories of the entries, in order (same order as tc_playlist_load)
tc_playlist_dirs()
{
    local dir=$1 entry
    local -n dirs_ref=$2
    dirs_ref=()
    while IFS= read -r entry; do
        dirs_ref+=("$(readlink -f -- "$dir/symlinks/$entry" || true)")
    done < <(tc_playlist_entries "$dir")
}

# every folder that holds a symlinks/ directory of playlist entries: the playlists and the entries of the user defined pendrives
tc_all_playlist_dirs()
{
    local d
    for d in "$tc_lib"/playlists/*/ "$tc_lib"/drives/*/playlists/*/; do
        if [ -d "${d}symlinks" ]; then
            printf '%s\n' "${d%/}"
        fi
    done
}
