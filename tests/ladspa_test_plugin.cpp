// A one-plugin LADSPA library for test_ladspa_host: "testgain" multiplies both channels by its control (default 0.5).
#include "audio/ladspa_min.h"

namespace {

struct Inst { LADSPA_Data *gain = nullptr, *inL = nullptr, *inR = nullptr, *outL = nullptr, *outR = nullptr; };

const LADSPA_PortDescriptor kPorts[5] = {LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
                                         LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO, LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
                                         LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO};
const char* const kNames[5] = {"gain", "in.l", "in.r", "out.l", "out.r"};
const LADSPA_PortRangeHint kHints[5] = {{LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_MIDDLE, 0.f, 1.f}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

LADSPA_Handle inst(const LADSPA_Descriptor*, unsigned long) { return new Inst; }
void connect(LADSPA_Handle h, unsigned long p, LADSPA_Data* d)
{
    Inst* i = static_cast<Inst*>(h);
    (&i->gain)[p] = d;
}
void run(LADSPA_Handle h, unsigned long n)
{
    Inst* i = static_cast<Inst*>(h);
    for (unsigned long k = 0; k < n; ++k) { i->outL[k] = i->inL[k] * *i->gain; i->outR[k] = i->inR[k] * *i->gain; }
}
void cleanup(LADSPA_Handle h) { delete static_cast<Inst*>(h); }

const LADSPA_Descriptor kDesc = {9999999, "testgain", 0, "Test gain", "vdj-ui tests", "none", 5, kPorts, kNames, kHints, nullptr,
                                 inst, connect, nullptr, run, nullptr, nullptr, nullptr, cleanup};
} // namespace

extern "C" const LADSPA_Descriptor* ladspa_descriptor(unsigned long index) { return index == 0 ? &kDesc : nullptr; }
