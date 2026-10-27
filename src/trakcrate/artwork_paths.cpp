#include "artwork_paths.h"

#include <cstdio>

namespace trakcrate {

std::string artworkPath(uint32_t id, bool medium)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, "/PIONEER/Artwork/%05u/a%u%s.jpg", id / 20 + 1, id, medium ? "_m" : "");
    return buf;
}

} // namespace trakcrate
