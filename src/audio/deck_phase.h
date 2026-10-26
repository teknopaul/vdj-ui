#ifndef DECK_PHASE_H
#define DECK_PHASE_H

#include <QHash>

namespace vdj {

// Where the other decks are in their beat and bar, extrapolated from their last beat packet. Pure: the caller passes a
// steady-clock time in ms (the packet's arrival and "now"). Every query returns wall-clock ms, or -1 when the deck is unknown or
// its last packet is stale (older than kStaleBeats beats). Our own tempo never matters: waits are wall-clock.
class DeckPhase {
public:
    static constexpr double kStaleBeats = 2.0;

    void setBpm(int player, double bpm);                          // the deck's reported (pitch adjusted) BPM
    void onBeat(int player, double steadyMs, int barPos);         // a beat packet arrived: barPos 1..4 is the beat that just happened
    void forget(int player);

    double msToNextBeat(int player, double nowMs) const;
    double msToNextGrid(int player, double nowMs, int div) const; // div = 1, 2, 4: beat subdivisions
    double msToNextDownbeat(int player, double nowMs) const;      // the next beat with barPos == 1

private:
    struct Deck {
        double bpm = 0.0;
        double t0 = 0.0;
        int barPos = 1;
        bool beat = false;
    };
    // beats since the last packet and the beat length, false when unknown or stale
    bool elapsed(int player, double nowMs, double* beats, double* beatLenMs, const Deck** deck) const;

    QHash<int, Deck> decks_;
};

// How long to wait before a quantized sequencer restart (wall ms). refDeck = the deck we are synced to (0 = none: we are the reference, i.e. the
// master): wait for that deck's next downbeat so the restarted sequencer's bar 1 coincides with its bar. With no reference deck wait for the
// sequencer's own next whole beat (seqBeats = raw beats since start). A reference deck that is unknown or stale falls back to our own beat and
// sets *usedFallback.
double restartWaitMs(int refDeck, const DeckPhase& phase, double nowMs, double seqBeats, double seqBpm, bool* usedFallback = nullptr);

// How long Play waits (wall ms) so the sequencer starts on the sync deck's next grid point at the global resolution `div`.
// 0 = start now: Q off, or no sync deck (we are the master: a stopped clock has no phase to align to). A sync deck whose beats are unknown or
// stale also starts now and sets *stale so the caller can say so once.
double playWaitMs(bool quantize, int refDeck, const DeckPhase& phase, double nowMs, int div, bool* stale = nullptr);

} // namespace vdj

#endif // DECK_PHASE_H
