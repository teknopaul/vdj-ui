#ifndef MIX_FILTERS_H
#define MIX_FILTERS_H

#include "trakcrate/mix_engine.h"

#include <QString>
#include <vector>

// The Qt side of the master filters of a studio mix: the LADSPA entries of Mix::filters become plain processor functions for the engine.
struct MixFilterReport {
    std::vector<QString> skipped;          // plugins that could not be loaded, with the reason
};

// Disabled filters and the built-in compressor are skipped (the engine handles that one). Order is kept.
std::vector<trakcrate::MixProcessor> buildLadspaProcessors(const std::vector<trakcrate::MixFilter>& filters, MixFilterReport* report = nullptr);

#endif
