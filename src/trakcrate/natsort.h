#ifndef TRAKCRATE_NATSORT_H
#define TRAKCRATE_NATSORT_H

#include <string>

namespace trakcrate {

// Natural order, case-insensitive for ASCII, independent of the locale: "track2" < "Track10". Runs of digits compare as numbers.
// Ties (names that differ only in case or leading zeros) fall back to a plain byte comparison so the order is total and repeatable.
bool naturalLess(const std::string& a, const std::string& b);

} // namespace trakcrate

#endif
