#include "deck_phase.h"
#include "midi/seq_bar.h"
#include <cmath>

namespace vdj {

namespace {
constexpr double kEps = 1e-6;

// ms until the next multiple of `step` after `e` (0 when exactly on one)
double toNext(double e, double step)
{
    const double k = std::ceil(e / step - kEps);
    return std::max(0.0, k * step - e);
}
}

void DeckPhase::setBpm(int player, double bpm)
{
    if (bpm > 0.0)
        decks_[player].bpm = bpm;
}

void DeckPhase::onBeat(int player, double steadyMs, int barPos)
{
    Deck& d = decks_[player];
    d.t0 = steadyMs;
    d.barPos = barPos < 1 || barPos > 4 ? 1 : barPos;
    d.beat = true;
}

void DeckPhase::forget(int player)
{
    decks_.remove(player);
}

bool DeckPhase::elapsed(int player, double nowMs, double* beats, double* beatLenMs, const Deck** deck) const
{
    auto it = decks_.constFind(player);
    if (it == decks_.constEnd() || !it->beat || it->bpm <= 0.0)
        return false;
    const double len = 60000.0 / it->bpm;
    const double e = std::max(0.0, nowMs - it->t0);
    if (e > kStaleBeats * len)
        return false;
    *beats = e / len;
    *beatLenMs = len;
    *deck = &*it;
    return true;
}

double DeckPhase::msToNextBeat(int player, double nowMs) const
{
    return msToNextGrid(player, nowMs, 1);
}

double DeckPhase::msToNextGrid(int player, double nowMs, int div) const
{
    double beats, len;
    const Deck* d;
    if (!elapsed(player, nowMs, &beats, &len, &d))
        return -1.0;
    if (div < 1)
        div = 1;
    return toNext(beats * len, len / div);
}

double DeckPhase::msToNextDownbeat(int player, double nowMs) const
{
    double beats, len;
    const Deck* d;
    if (!elapsed(player, nowMs, &beats, &len, &d))
        return -1.0;
    const double pos = std::fmod((d->barPos - 1) + beats, 4.0);    // beats into the deck's bar
    return pos < kEps ? 0.0 : (4.0 - pos) * len;
}

double restartWaitMs(int refDeck, const DeckPhase& phase, double nowMs, double seqBeats, double seqBpm, bool* usedFallback)
{
    if (usedFallback)
        *usedFallback = false;
    if (refDeck != 0) {
        const double w = phase.msToNextDownbeat(refDeck, nowMs);
        if (w >= 0.0)
            return w;
        if (usedFallback)
            *usedFallback = true;
    }
    return beatsToWallMs(beatsToNextBeat(seqBeats), seqBpm);
}

double playWaitMs(bool quantize, int refDeck, const DeckPhase& phase, double nowMs, int div, bool* stale)
{
    if (stale)
        *stale = false;
    if (!quantize || refDeck == 0)
        return 0.0;
    const double w = phase.msToNextGrid(refDeck, nowMs, div);
    if (w < 0.0) {
        if (stale)
            *stale = true;
        return 0.0;
    }
    return w;
}

} // namespace vdj
