#ifndef BACKLINE_MODEL_H
#define BACKLINE_MODEL_H

#include <QAbstractListModel>
#include <array>
#include <cstdint>

extern "C" {
#include <cdj/vdj.h>
}

namespace vdj {

struct DeckInfo {
    uint8_t  playerId = 0;
    float    bpm = 0.0f;
    int32_t  pitch = 0;
    uint8_t  barPos = 0;
    uint8_t  playState = 0;
    uint8_t  masterState = 0;
    bool     active = false;
    bool     known = false;
    bool     onair = false;
    bool     gone = true;
    char     ip[16] = {};
};

class BacklineModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        PlayerIdRole = Qt::UserRole + 1,
        BpmRole,
        PitchRole,
        BarPosRole,
        PlayStateRole,
        MasterStateRole,
        ActiveRole,
        KnownRole,
        OnAirRole,
        GoneRole,
        IpRole,
        NfsStatusRole
    };

    explicit BacklineModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void updateFromBackline(const vdj_backline_t* backline);
    void setNfsStatus(int playerId, bool mounted);
    DeckInfo deckAt(int index) const;
    int deckCount() const;

private:
    std::array<DeckInfo, VDJ_MAX_BACKLINE + 1> decks_;
    std::array<bool, VDJ_MAX_BACKLINE + 1> nfsMounted_ = {};
    int count_ = 0;
};

} // namespace vdj

#endif // BACKLINE_MODEL_H
