#include "natsort.h"

namespace trakcrate {

static bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }
static unsigned char lower(unsigned char c) { return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + 32) : c; }

static int naturalCompare(const std::string& a, const std::string& b)
{
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        unsigned char ca = static_cast<unsigned char>(a[i]), cb = static_cast<unsigned char>(b[j]);
        if (isDigit(ca) && isDigit(cb)) {
            size_t si = i, sj = j;
            while (si < a.size() && a[si] == '0') ++si;                  // leading zeros do not count
            while (sj < b.size() && b[sj] == '0') ++sj;
            size_t ei = si, ej = sj;
            while (ei < a.size() && isDigit(static_cast<unsigned char>(a[ei]))) ++ei;
            while (ej < b.size() && isDigit(static_cast<unsigned char>(b[ej]))) ++ej;
            if (ei - si != ej - sj) return (ei - si) < (ej - sj) ? -1 : 1;   // fewer digits = smaller number
            int c = a.compare(si, ei - si, b, sj, ej - sj);
            if (c != 0) return c < 0 ? -1 : 1;
            i = ei;
            j = ej;
            continue;
        }
        unsigned char la = lower(ca), lb = lower(cb);
        if (la != lb) return la < lb ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

bool naturalLess(const std::string& a, const std::string& b)
{
    int c = naturalCompare(a, b);
    if (c != 0) return c < 0;
    return a < b;
}

} // namespace trakcrate
