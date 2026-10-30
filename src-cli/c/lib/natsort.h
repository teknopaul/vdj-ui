#ifndef TC_NATSORT_H
#define TC_NATSORT_H

/*
 * The order of names in a trakcrate library (TRAKCRATE-FORMAT.md 2.1): case-insensitive (ASCII) and
 * natural, runs of digits compare as numbers ("track2" before "track10", leading zeros do not count),
 * independent of the locale. Ties are broken by a plain byte comparison so the order is total.
 */
int tc_natural_compare(const char *a, const char *b);

#endif /* TC_NATSORT_H */
