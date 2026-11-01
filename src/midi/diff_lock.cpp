#include "diff_lock.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vdj {

void DiffLock::reset()
{
    std::memset(last_, 0, sizeof(last_));
    std::memset(set_, 0, sizeof(set_));
    std::memset(pos_, 0, sizeof(pos_));
    std::memset(count_, 0, sizeof(count_));
}

int DiffLock::limit(int ms)
{
    return std::clamp(ms, -kLimitMs, kLimitMs);
}

bool DiffLock::lock(int player)
{
    if (player < 1 || player > kMaxPlayers)
        return false;
    player_ = player;
    if (master_ != player)
        followMaster_ = false;                  // an explicit lock on another deck stops following the master
    return true;
}

void DiffLock::off()
{
    player_ = 0;
    hold_ = false;
    reset();
}

void DiffLock::rebase(int player)
{
    if (player < 1 || player > kMaxPlayers)
        return;
    const int i = player - 1;
    last_[i] = 0.0;
    std::memset(set_[i], 0, sizeof(set_[i]));
    pos_[i] = 0;
    count_[i] = 0;
    player_ = player;
    hold_ = true;
}

void DiffLock::setFollowMaster(bool on)
{
    followMaster_ = on;
    if (on && master_ >= 1)
        player_ = master_;                      // adj waits for the next change; locking now is more useful
}

void DiffLock::masterChanged(int player)
{
    if (player < 1 || player == master_)
        return;
    master_ = player;
    if (followMaster_)
        player_ = master_;
}

double DiffLock::lastDiff(int player) const
{
    return (player >= 1 && player <= kMaxPlayers) ? last_[player - 1] : 0.0;
}

bool DiffLock::hasAverage(int player) const
{
    return player >= 1 && player <= kMaxPlayers && count_[player - 1] >= kAvgSize;
}

double DiffLock::avgDiff(int player) const
{
    if (!hasAverage(player))
        return 0.0;
    double sum = 0.0;
    for (int i = 0; i < kAvgSize; ++i)
        sum += set_[player - 1][i];
    return sum / kAvgSize;
}

int DiffLock::onBeat(int player, int barPos, double diffMs)
{
    if (player < 1 || player > kMaxPlayers || std::fabs(diffMs) > kMaxDiffMs)
        return 0;
    const int i = player - 1;
    last_[i] = diffMs;
    set_[i][pos_[i]] = diffMs;
    pos_[i] = (pos_[i] + 1) % kAvgSize;
    ++count_[i];

    if (fast_ && player == player_ && !hold_ && count_[i] >= 2) {
        const int prev = (pos_[i] + kAvgSize - 2) % kAvgSize;       // the beat before this one
        const double mean = (diffMs + set_[i][prev]) / 2.0;
        const int n = int(std::lround(kFastGain * (mean - targetMs_)));
        return std::clamp(n, -kFastLimitMs, kFastLimitMs);
    }

    // auto nudge on the last beat of the bar, so the next downbeat should then be in time
    if (player != player_ || barPos != 4 || hold_ || !hasAverage(player))
        return 0;
    return limit(int(std::lround(avgDiff(player) - targetMs_)));
}

} // namespace vdj
