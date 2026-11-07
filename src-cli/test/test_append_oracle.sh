#!/bin/bash
#
# The C database editor (c/lib/rbappend.c, driven by c/target/append_plan) against the GUI's C++ one (the oracle, test/oracle/cxx_append.cpp):
# the same plans added to the same export.pdb give the same summaries and byte identical databases, also when one plan is added after another,
# when a playlist reuses tracks the stick already has and when the playlist name is already there. Skipped when the GUI library is not built.
#
. "$(dirname "$0")/lib.sh"

oracle=$("$TC_TEST_DIR/oracle/build.sh" append 2>/dev/null) || { echo "test_append_oracle.sh: the GUI library is not built, skipped"; exit 0; }
command -v python3 >/dev/null || { echo "test_append_oracle.sh: python3 not found, skipped"; exit 0; }
make -s -C "$TC_ROOT/c" target/append_plan >/dev/null 2>&1 || { echo "test_append_oracle.sh: cannot build append_plan"; exit 1; }
cplan=$TC_ROOT/c/target/append_plan

fixtures=$TC_ROOT/../tests/fixtures/trakcrate
template=$fixtures/pdb/export7.pdb
big=$TC_ROOT/../references/libcdj/src/test/fixtures/export.pdb

# plans PDB DIR: writes DIR/plan1 .. plan3 for the database PDB. plan1: two new tracks (one under an artist and album the stick has, one with
# non-ASCII names); plan2: a track the stick has, a track plan1 added and a new one; plan3: 40 new tracks with long titles (new pages).
make_plans()
{
    local pdb=$1 dir=$2 d
    d=$dir/drive
    mkdir -p "$d/PIONEER/rekordbox"
    cp "$pdb" "$d/PIONEER/rekordbox/export.pdb"
    "$TC_HELPER" rbdrive "$d" > "$dir/rows.tsv"
    python3 -I - "$dir" <<'PY'
import sys
d = sys.argv[1]
tracks = []
lists = []
for line in open(d + '/rows.tsv', encoding='utf-8'):
    f = line.rstrip('\n').split('\t')
    if f[0] == 'T':
        tracks.append(f[2])
    elif f[0] == 'P':
        lists.append(f[2])
parts = tracks[0].split('/')          # '', Contents, artist, album, file
artist, album = parts[2], parts[3]

def t(title, art, alb, genre, label, path, key='', apath=''):
    return 'T\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t12345\n' % (title, art, alb, genre, label, path, key, apath)

with open(d + '/plan1', 'w', encoding='utf-8') as o:
    o.write(t('Added One', artist, album, 'Test Genre', 'Test Label', '/Contents/%s/%s/added-one.mp3' % (artist, album), 'k1', '/PIONEER/Artwork/00001/a900.jpg'))
    o.write(t('Přidaná skladba', 'Čeština Ünï', 'Álbum ✓', 'Žánr', '', '/Contents/Čeština Ünï/Álbum ✓/přidaná.mp3', 'k2', '/PIONEER/Artwork/00001/a901.jpg'))
    o.write('P\tFirst Added\t0,1\n')
with open(d + '/plan2', 'w', encoding='utf-8') as o:
    o.write(t('Old One', artist, album, '', '', tracks[0]))
    o.write(t('Added One', artist, album, 'Test Genre', 'Test Label', '/Contents/%s/%s/added-one.mp3' % (artist, album)))
    o.write(t('Third', 'Third Artist', 'Third Album', 'Test Genre', 'Test Label', '/Contents/Third Artist/Third Album/third.mp3', 'k3', '/PIONEER/Artwork/00001/a902.jpg'))
    o.write('P\tMixed\t0,1,2\n')
with open(d + '/plan3', 'w', encoding='utf-8') as o:
    for i in range(40):
        o.write(t('A long title for track number %d %s' % (i, 'x' * 60), 'Artist %d' % (i % 7), 'Album %d' % (i % 5), 'Genre %d' % (i % 3), 'Label %d' % (i % 2),
                  '/Contents/Artist %d/Album %d/track%02d.mp3' % (i % 7, i % 5, i)))
    o.write('P\tBig\t' + ','.join(str(i) for i in range(40)) + '\n')
with open(d + '/clash', 'w', encoding='utf-8') as o:
    o.write(t('X', 'X', 'X', '', '', '/Contents/X/X/x.mp3'))
    o.write('P\t' + lists[0].upper() + '\t0\n')
PY
}

# same PDB PLAN...: both implementations on the database with the plans one after another
same()
{
    local name=$1 pdb=$2
    shift 2
    "$oracle" "$pdb" "$template" "$T/x.pdb" "$@" > "$T/x.out" 2>&1
    local xs=$?
    "$cplan" "$pdb" "$template" "$T/c.pdb" "$@" > "$T/c.out" 2>&1
    local cs=$?
    assert_eq "$name: the same exit status" "$xs" "$cs"
    assert_eq "$name: the same summary" "$(cat "$T/x.out")" "$(cat "$T/c.out")"
    if [ "$xs" = 0 ]; then
        if cmp -s "$T/x.pdb" "$T/c.pdb"; then
            pass_assert
        else
            fail_assert "$name: the databases differ" "C++: $(wc -c < "$T/x.pdb") bytes" "C:   $(wc -c < "$T/c.pdb") bytes"
        fi
    fi
}

each_fixture()
{
    local f
    for f in "$fixtures/pdb-drive/rb-mini.pdb" "$fixtures/pdb-drive/rb-loads-fine.pdb" "$fixtures/pdb/export7.pdb" "$big" \
             /media/*/TEKNOPAUL64/PIONEER/rekordbox/export.pdb /transfer/cdj64/PIONEER/rekordbox/export.pdb; do   # real sticks, when present
        [ -f "$f" ] || continue
        "$@" "$f"
    done
}

test_plans_one_after_another_are_byte_identical()
{
    one() {
        local f=$1 p=$T/p.$(echo "$f" | tr / _)
        mkdir -p "$p"
        make_plans "$f" "$p"
        same "$(basename "$f"): plan 1" "$f" "$p/plan1"
        same "$(basename "$f"): plan 1 then 2" "$f" "$p/plan1" "$p/plan2"
        same "$(basename "$f"): plans 1, 2 and 3" "$f" "$p/plan1" "$p/plan2" "$p/plan3"
        same "$(basename "$f"): plan 3 alone" "$f" "$p/plan3"
    }
    each_fixture one
}

test_a_playlist_name_that_exists_is_refused_alike()
{
    one() {
        local f=$1 p=$T/p.$(echo "$f" | tr / _)
        mkdir -p "$p"
        make_plans "$f" "$p"
        same "$(basename "$f"): existing name" "$f" "$p/clash"
        assert_contains "$(basename "$f"): the refusal names the playlist" "already has a playlist called" "$(cat "$T/c.out")"
    }
    each_fixture one
}

run_tests
