#ifndef TRAKCRATE_EDIT_PLAN_H
#define TRAKCRATE_EDIT_PLAN_H

#include "track.h"

#include <string>
#include <vector>

// DJ edits of a track without any Qt (doc/trakcrate/track-edits.md): regions of whole beats are cut out or looped, and the result is described as source
// ranges ("segments") played in order with a "join" at every seam. A cut of [a,b) is the join a -> b, each loop repeat of [a,b) is the join b -> a.
// All positions are source frames computed from the grid (never accumulated), so there is no drift.
namespace trakcrate {

enum class EditKind { Cut, Loop };
enum class EditAlgo { Cut, Cross, Fadeout, TailOverlap, EarlyCut };

// Legal parameters. Cut: fade in ms. Cross, Fadeout: beats. TailOverlap: beats of the removed range played over the next part.
constexpr int kCutFadeMs[] = {5, 10, 20, 50};
constexpr double kCrossBeats[] = {0.5, 1, 2};
constexpr double kFadeoutBeats[] = {0.5, 1, 2};
constexpr double kTailBeats[] = {0, 1, 2, 4};
constexpr int kEarlyShift[] = {1, 2, 3};
constexpr int kLoopBars[] = {1, 2, 4, 8, 16};
constexpr int kLoopTimesMin = 2, kLoopTimesMax = 16;

inline int barsToBeats(int bars) { return bars * 4; }

struct EditRegion {
    EditKind kind = EditKind::Cut;
    int startBeat = 0;                 // beat index on the grid, 0 = beat 1 of the grid
    int lenBeats = 4;                  // cut: whole bars; loop: 1, 2, 4, 8 or 16 bars
    EditAlgo algo = EditAlgo::Cut;
    double algoParam = 10;             // see the constants above
    int loopTimes = 2;                 // loops: how often the section is played in total
    int earlyShift = 1;                // EarlyCut: the removed range moves by this many beats ...
    bool earlyAfter = false;           // ... later (true) or earlier (false)
    EditAlgo earlyJoinAlgo = EditAlgo::Cut;     // EarlyCut: Cut or Cross
    double earlyJoinParam = 10;
};

struct EditJoin {
    long long outFrame = 0;            // audio up to this source frame ...
    long long inFrame = 0;             // ... continues into the audio from this one
    EditAlgo algo = EditAlgo::Cut;     // never EarlyCut
    double param = 10;
};

struct EditSegment {
    long long start = 0, end = 0;      // source frames, start < end
};

struct EditPlan {
    std::vector<EditSegment> segments;
    std::vector<EditJoin> joins;       // joins[i] sits between segments[i] and segments[i + 1]
};

constexpr int kEditRate = 44100;

// Source frame of a beat on the grid (beat 0 = the grid's first beat; fractions allowed).
long long beatFrame(const BeatGrid& grid, double beatIndex, int rate = kEditRate);
// A length of `beats` in frames.
double beatFrames(const BeatGrid& grid, double beats, int rate = kEditRate);

// "" when the regions can be rendered, otherwise a message for the user. Regions are numbered from 1 in the order given.
std::string validateRegions(const BeatGrid& grid, const std::vector<EditRegion>& regions, long long totalFrames);

// EarlyCut applied: plain cuts (and loops unchanged), sorted by position. Only meaningful for regions that passed validateRegions.
std::vector<EditRegion> resolveRegions(const std::vector<EditRegion>& regions);

// Segments and joins for validated regions (an invalid list gives an empty plan).
EditPlan planEdit(const BeatGrid& grid, const std::vector<EditRegion>& regions, long long totalFrames);

long long planOutputFrames(const EditPlan& plan);
// Same tempo; beat 1 is where the first kept source beat lands in the output.
BeatGrid outputGrid(const BeatGrid& in, const EditPlan& plan, int rate = kEditRate);

} // namespace trakcrate

#endif
