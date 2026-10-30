#ifndef TC_RBLIMITS_H
#define TC_RBLIMITS_H

#include <stddef.h>

/*
 * How many tracks, artists and albums a deck can hold in a pendrive's database before it runs out of memory
 * (doc/trakcrate/rekordbox-db-output.md). Only pendrive exports are limited, never the trakcrate library. The lower limits
 * (older decks) are the default; --raise-limits selects the higher ones (newer decks).
 */
struct tc_deck_limits {
    size_t tracks, artists, albums;
};

struct tc_deck_limits tc_deck_limits(int raised);

/*
 * Counts the distinct artists (non empty names) and albums (an artist and a non empty name) of n tracks, the way the database
 * builder numbers them. artists[i] and albums[i] may be NULL or "" for none. -1 on out of memory.
 */
int tc_count_artists_albums(const char *const *artists, const char *const *albums, size_t n, size_t *nartists, size_t *nalbums);

/*
 * The reasons a pendrive with these counts is refused, one line each ('\n' between them, no final newline), the last line
 * saying what to do about it. Returns the number of limits exceeded (0: fine, out is empty). *over_default / *over_raised
 * (may be NULL) say whether the counts are above the lower / the higher limits.
 */
int tc_check_deck_limits(size_t tracks, size_t artists, size_t albums, int raised, char *out, size_t outsz, int *over_default, int *over_raised);

#endif /* TC_RBLIMITS_H */
