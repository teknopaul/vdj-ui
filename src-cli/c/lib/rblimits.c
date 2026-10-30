#include "rblimits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//SNIP_rblimits

struct tc_deck_limits tc_deck_limits(int raised)
{
    struct tc_deck_limits l;

    if (raised) {
        l.tracks = 20000;
        l.artists = 16000;
        l.albums = 16000;
    } else {
        l.tracks = 10000;
        l.artists = 10000;
        l.albums = 10000;
    }
    return l;
}

struct pair {
    const char *artist, *album;
};

static int cmp_artist(const void *a, const void *b)
{
    return strcmp(((const struct pair *)a)->artist, ((const struct pair *)b)->artist);
}

static int cmp_pair(const void *a, const void *b)
{
    const struct pair *x = a, *y = b;
    int c = strcmp(x->artist, y->artist);

    return c != 0 ? c : strcmp(x->album, y->album);
}

int tc_count_artists_albums(const char *const *artists, const char *const *albums, size_t n, size_t *nartists, size_t *nalbums)
{
    struct pair *p = malloc((n ? n : 1) * sizeof *p);
    size_t i, na = 0, nb = 0, m = 0;

    if (p == NULL) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        p[i].artist = artists != NULL && artists[i] != NULL ? artists[i] : "";
        p[i].album = albums != NULL && albums[i] != NULL ? albums[i] : "";
    }
    qsort(p, n, sizeof *p, cmp_artist);
    for (i = 0; i < n; i++) {
        if (p[i].artist[0] && (i == 0 || strcmp(p[i - 1].artist, p[i].artist) != 0)) {
            na++;
        }
    }
    for (i = 0; i < n; i++) {
        if (p[i].album[0]) {
            p[m++] = p[i];
        }
    }
    qsort(p, m, sizeof *p, cmp_pair);
    for (i = 0; i < m; i++) {
        if (i == 0 || cmp_pair(&p[i - 1], &p[i]) != 0) {
            nb++;
        }
    }
    free(p);
    *nartists = na;
    *nalbums = nb;
    return 0;
}

int tc_check_deck_limits(size_t tracks, size_t artists, size_t albums, int raised, char *out, size_t outsz, int *over_default, int *over_raised)
{
    struct tc_deck_limits low = tc_deck_limits(0), high = tc_deck_limits(1), use = raised ? high : low;
    int od = tracks > low.tracks || artists > low.artists || albums > low.albums;
    int orr = tracks > high.tracks || artists > high.artists || albums > high.albums;
    size_t n = 0;
    int count = 0;

    if (over_default != NULL) {
        *over_default = od;
    }
    if (over_raised != NULL) {
        *over_raised = orr;
    }
    if (outsz > 0) {
        out[0] = '\0';
    }
#define ONE(what, have, limit)                                                                                          \
    if ((have) > (limit)) {                                                                                             \
        if (n < outsz) {                                                                                                \
            n += (size_t)snprintf(out + n, outsz - n, "%stoo many " what " for a pendrive: %zu, the limit is %zu (%s)", \
                                  count ? "\n" : "", (size_t)(have), (size_t)(limit), raised ? "newer decks" : "older decks"); \
        }                                                                                                               \
        count++;                                                                                                        \
    }
    ONE("tracks", tracks, use.tracks)
    ONE("artists", artists, use.artists)
    ONE("albums", albums, use.albums)
#undef ONE
    if (count && n < outsz) {
        snprintf(out + n, outsz - n,
                 "\nDecks run out of memory above these numbers, so the pendrive is not written (trakcrate's own library has no limit). "
                 "Older decks hold %zu tracks, %zu artists and %zu albums; newer decks %zu, %zu and %zu. "
                 "Remove tracks from this pendrive's lists in trakcrate%s",
                 low.tracks, low.artists, low.albums, high.tracks, high.artists, high.albums,
                 (!raised && !orr) ? ", or, if your decks are newer, use --raise-limits (not tested on every deck)." : ".");
    }
    return count;
}

//SNIP_rblimits
