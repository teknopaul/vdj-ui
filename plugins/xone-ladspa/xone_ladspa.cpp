/*
 * Xone filter: a LADSPA version of the LMMS "Xone" effect (Teknopaul, 2023), a high pass filter with resonance for a whole mix,
 * tuned to behave like the filter of an Xone mixer.
 *
 * Parameters as in LMMS:  Freq 10 .. 14000 Hz (LMMS: 0 .. 14000, and its filter never goes below 50 Hz)
 *                         Wild 0.01 .. 6.5, the resonance (Q) at the cutoff.
 * Unlike LMMS the resonance fades out as the cutoff goes down to 10 Hz (full from kFullQHz up), so with the knob fully down the
 * filter is out of the way even at full Wild: a resonance peak in the sub bass only distorts the mix.
 *
 * The filter is LMMS BasicFilters' Highpass_RC24: a 24 dB/octave simulation of an active RC network, run 4 times oversampled and
 * clamped so that it can be driven to self-oscillation without blowing up (do not remove the clamps).
 * (C) 1998 ... 2009 S.Fendt (the filter). Released under the GPL v2.0 or any later version, as LMMS.
 */

#include "../../src/audio/ladspa/ladspa.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

constexpr unsigned long kUniqueId = 931;          // private range of the LADSPA registry
constexpr float kMinQ = 0.01f;
constexpr float kMinHz = 10.0f;
constexpr float kMaxHz = 20000.0f;
constexpr float kFullQHz = 80.0f;             // from here up Wild has its full effect
constexpr float k2Pi = 6.283185307179586f;

enum Port { PortFreq, PortWild, PortInL, PortInR, PortOutL, PortOutR, PortCount };

struct Channel {
    float last0 = 0, hp0 = 0, bp0 = 0;            // first stage
    float last1 = 0, hp1 = 0, bp1 = 0;            // second stage
};

struct Xone {
    float rate = 44100.0f;
    float* port[PortCount] = {};
    Channel ch[2];
    float coeffFreq = -1.0f, coeffWild = -1.0f;
    float rca = 0, rcb = 0, rcc = 0, rcq = 0;

    void coeffs(float freq, float q)
    {
        if (freq == coeffFreq && q == coeffWild)
            return;
        coeffFreq = freq;
        coeffWild = q;
        freq = std::clamp(freq, kMinHz, kMaxHz);
        // resonance fades in over the octaves between kMinHz and kFullQHz
        const float fade = std::min(1.0f, std::log(freq / kMinHz) / std::log(kFullQHz / kMinHz));
        q = std::max(kMinQ + (q - kMinQ) * fade, kMinQ);
        const float sr = (1.0f / rate) * 0.25f;
        const float f = 1.0f / (freq * k2Pi);
        rca = 1.0f - sr / (f + sr);
        rcb = 1.0f - rca;
        rcc = f / (f + sr);
        rcq = q * 0.25f;                          // self-oscillation starts at a Q of about 2.5
    }

    float update(float in0, Channel& c) const
    {
        float hp = 0.0f, bp, in;
        for (int n = 4; n != 0; --n) {
            in = in0 + c.bp0 * rcq;
            in = std::clamp(in, -1.0f, 1.0f);
            hp = rcc * (c.hp0 + in - c.last0);
            hp = std::clamp(hp, -1.0f, 1.0f);
            bp = hp * rcb + c.bp0 * rca;
            bp = std::clamp(bp, -1.0f, 1.0f);
            c.last0 = in;
            c.hp0 = hp;
            c.bp0 = bp;

            in = hp + c.bp1 * rcq;                // the second stage gets the output of the first
            in = std::clamp(in, -1.0f, 1.0f);
            hp = rcc * (c.hp1 + in - c.last1);
            hp = std::clamp(hp, -1.0f, 1.0f);
            bp = hp * rcb + c.bp1 * rca;
            bp = std::clamp(bp, -1.0f, 1.0f);
            c.last1 = in;
            c.hp1 = hp;
            c.bp1 = bp;
        }
        return hp;
    }
};

LADSPA_Handle instantiate(const LADSPA_Descriptor*, unsigned long rate)
{
    auto* x = new Xone;
    x->rate = float(rate);
    return x;
}

void connectPort(LADSPA_Handle h, unsigned long p, LADSPA_Data* data)
{
    if (p < PortCount)
        static_cast<Xone*>(h)->port[p] = data;
}

void activate(LADSPA_Handle h)
{
    auto* x = static_cast<Xone*>(h);
    x->ch[0] = Channel();
    x->ch[1] = Channel();
    x->coeffFreq = x->coeffWild = -1.0f;
}

void run(LADSPA_Handle h, unsigned long n)
{
    auto* x = static_cast<Xone*>(h);
    x->coeffs(*x->port[PortFreq], *x->port[PortWild]);
    for (unsigned long i = 0; i < n; ++i) {
        x->port[PortOutL][i] = x->update(x->port[PortInL][i], x->ch[0]);
        x->port[PortOutR][i] = x->update(x->port[PortInR][i], x->ch[1]);
    }
}

void cleanup(LADSPA_Handle h)
{
    delete static_cast<Xone*>(h);
}

const char* const kNames[PortCount] = { "Freq", "Wild", "Input L", "Input R", "Output L", "Output R" };
const LADSPA_PortDescriptor kDescriptors[PortCount] = {
    LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,   LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
    LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,  LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO };
LADSPA_PortRangeHint g_hints[PortCount];

LADSPA_Descriptor g_descriptor;
bool g_ready = false;

void init()
{
    if (g_ready)
        return;
    g_hints[PortFreq] = { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_LOGARITHMIC | LADSPA_HINT_DEFAULT_MINIMUM,
                          kMinHz, 14000.0f };
    g_hints[PortWild] = { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_MINIMUM, kMinQ, 6.5f };
    for (int p = PortInL; p < PortCount; ++p)
        g_hints[p] = { 0, 0.0f, 0.0f };
    g_descriptor.UniqueID = kUniqueId;
    g_descriptor.Label = "xone_hp";
    g_descriptor.Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE;
    g_descriptor.Name = "Xone filter";
    g_descriptor.Maker = "Teknopaul";
    g_descriptor.Copyright = "GPL-2.0-or-later";
    g_descriptor.PortCount = PortCount;
    g_descriptor.PortDescriptors = kDescriptors;
    g_descriptor.PortNames = kNames;
    g_descriptor.PortRangeHints = g_hints;
    g_descriptor.ImplementationData = nullptr;
    g_descriptor.instantiate = instantiate;
    g_descriptor.connect_port = connectPort;
    g_descriptor.activate = activate;
    g_descriptor.run = run;
    g_descriptor.run_adding = nullptr;
    g_descriptor.set_run_adding_gain = nullptr;
    g_descriptor.deactivate = nullptr;
    g_descriptor.cleanup = cleanup;
    g_ready = true;
}

} // namespace

extern "C" const LADSPA_Descriptor* ladspa_descriptor(unsigned long index)
{
    init();
    return index == 0 ? &g_descriptor : nullptr;
}
