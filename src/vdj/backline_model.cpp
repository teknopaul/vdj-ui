#include "backline_model.h"
#include <cstring>
#include <arpa/inet.h>

namespace vdj {

BacklineModel::BacklineModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int BacklineModel::rowCount(const QModelIndex& /*parent*/) const
{
    return count_;
}

QVariant BacklineModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= count_)
        return {};

    const auto& d = decks_[index.row()];
    switch (role) {
    case PlayerIdRole:   return d.playerId;
    case BpmRole:        return static_cast<double>(d.bpm);
    case PitchRole:      return d.pitch;
    case BarPosRole:     return d.barPos;
    case PlayStateRole:  return d.playState;
    case MasterStateRole: return d.masterState;
    case ActiveRole:     return d.active;
    case KnownRole:      return d.known;
    case OnAirRole:      return d.onair;
    case GoneRole:       return d.gone;
    case IpRole:         return QString(d.ip);
    case NfsStatusRole: {
        int pid = d.playerId;
        return pid >= 0 && pid <= VDJ_MAX_BACKLINE ? nfsMounted_[pid] : false;
    }
    }
    return {};
}

QHash<int, QByteArray> BacklineModel::roleNames() const
{
    return {
        { PlayerIdRole,   "playerId" },
        { BpmRole,        "bpm" },
        { PitchRole,      "pitch" },
        { BarPosRole,     "barPos" },
        { PlayStateRole,  "playState" },
        { MasterStateRole, "masterState" },
        { ActiveRole,     "active" },
        { KnownRole,      "known" },
        { OnAirRole,      "onair" },
        { GoneRole,       "gone" },
        { IpRole,         "ip" },
        { NfsStatusRole,  "nfsMounted" }
    };
}

void BacklineModel::updateFromBackline(const vdj_backline_t* backline)
{
    if (!backline) return;

    beginResetModel();
    count_ = 0;

    for (int i = 1; i <= VDJ_MAX_BACKLINE; i++) {
        const vdj_link_member_t* m = backline->link_members[i];
        if (!m) continue;

        auto& d = decks_[count_];
        d.playerId    = m->player_id;
        d.bpm         = m->bpm;
        d.pitch       = m->pitch;
        d.barPos      = m->bar_pos;
        d.playState   = m->play_state;
        d.masterState = m->master_state;
        d.active      = m->active;
        d.known       = m->known;
        d.onair       = m->onair;
        d.gone        = m->gone;

        if (m->ip_addr) {
            inet_ntop(AF_INET, &m->ip_addr->sin_addr, d.ip, sizeof(d.ip));
        } else {
            d.ip[0] = '\0';
        }

        count_++;
    }

    endResetModel();
}

DeckInfo BacklineModel::deckAt(int index) const
{
    if (index >= 0 && index < count_)
        return decks_[index];
    return {};
}

void BacklineModel::setNfsStatus(int playerId, bool mounted)
{
    if (playerId < 0 || playerId > VDJ_MAX_BACKLINE)
        return;
    if (nfsMounted_[playerId] == mounted)
        return;
    nfsMounted_[playerId] = mounted;
    // emit a row refresh for whichever deck holds this player id
    for (int i = 0; i < count_; ++i) {
        if (decks_[i].playerId == playerId) {
            emit dataChanged(index(i), index(i), { NfsStatusRole });
            return;
        }
    }
}

int BacklineModel::deckCount() const
{
    return count_;
}

} // namespace vdj
