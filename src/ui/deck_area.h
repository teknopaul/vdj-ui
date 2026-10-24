#ifndef DECK_AREA_H
#define DECK_AREA_H

#include <QScrollArea>
#include <QVector>

namespace vdj {

class DeckWidget;

class DeckArea : public QScrollArea {
    Q_OBJECT

public:
    explicit DeckArea(QWidget* parent = nullptr);

    DeckWidget* addDeck(int playerId);
    void removeDeck(int playerId);
    void clearDecks();
    DeckWidget* deckAt(int index) const;
    DeckWidget* deckByPlayerId(int playerId) const;
    int deckCount() const;

signals:
    void deckClicked(int playerId);
    void playPauseClicked(int playerId, bool play);
    void syncClicked(int playerId);
    void setBpmClicked(int playerId, double bpm);
    void becomeMasterClicked(int playerId);
    void bpmFromMasterClicked(int playerId);
    void broadcastBeatClicked(int playerId, double bpm, int barPos);
    void pitchChanged(int playerId, double percentage);

private:
    QWidget* container_;
    QVector<DeckWidget*> decks_;
};

} // namespace vdj

#endif // DECK_AREA_H
