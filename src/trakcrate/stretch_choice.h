#ifndef TRAKCRATE_STRETCH_CHOICE_H
#define TRAKCRATE_STRETCH_CHOICE_H

#include <map>
#include <string>

namespace trakcrate {

// Which time stretch engine to use and its settings (the engines themselves live in stretch_engine.h). Empty engine = "use the mix default".
using StretchOptions = std::map<std::string, std::string>;

struct StretchChoice {
    std::string engine;
    StretchOptions options;
    bool empty() const { return engine.empty(); }
};

} // namespace trakcrate

#endif
