#include "deck_area.h"
#include "deck_widget.h"
#include <QVBoxLayout>

namespace vdj {

DeckArea::DeckArea(QWidget* parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    container_ = new QWidget(this);
    auto* layout = new QVBoxLayout(container_);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);
    layout->setAlignment(Qt::AlignTop);

    setWidget(container_);
}

DeckWidget* DeckArea::addDeck(int playerId)
{
    // Idempotent: only one deck per player id. Return the existing deck if a
    // real deck joins after a placeholder (or the same player re-joins).
    if (DeckWidget* existing = deckByPlayerId(playerId))
        return existing;

    auto* deck = new DeckWidget(playerId, container_);
    decks_.append(deck);
    container_->layout()->addWidget(deck);

    connect(deck, &DeckWidget::deckClicked, this, &DeckArea::deckClicked);
    connect(deck, &DeckWidget::playPauseClicked, this, &DeckArea::playPauseClicked);
    connect(deck, &DeckWidget::restartClicked, this, &DeckArea::restartClicked);
    connect(deck, &DeckWidget::beatJumpClicked, this, &DeckArea::beatJumpClicked);
    connect(deck, &DeckWidget::syncClicked, this, &DeckArea::syncClicked);
    connect(deck, &DeckWidget::setBpmClicked, this, &DeckArea::setBpmClicked);
    connect(deck, &DeckWidget::becomeMasterClicked, this, &DeckArea::becomeMasterClicked);
    connect(deck, &DeckWidget::bpmFromMasterClicked, this, &DeckArea::bpmFromMasterClicked);
    connect(deck, &DeckWidget::broadcastBeatClicked, this, &DeckArea::broadcastBeatClicked);
    connect(deck, &DeckWidget::pitchChanged, this, &DeckArea::pitchChanged);
    return deck;
}

void DeckArea::removeDeck(int playerId)
{
    for (int i = 0; i < decks_.size(); ++i) {
        if (decks_[i]->playerId() == playerId) {
            auto* d = decks_.takeAt(i);
            container_->layout()->removeWidget(d);
            delete d;
            return;
        }
    }
}

void DeckArea::clearDecks()
{
    for (auto* d : decks_) {
        container_->layout()->removeWidget(d);
        delete d;
    }
    decks_.clear();
}

DeckWidget* DeckArea::deckAt(int index) const
{
    return (index >= 0 && index < decks_.size()) ? decks_[index] : nullptr;
}

DeckWidget* DeckArea::deckByPlayerId(int playerId) const
{
    for (DeckWidget* deck : decks_) {
        if (deck->playerId() == playerId)
            return deck;
    }
    return nullptr;
}

int DeckArea::deckCount() const
{
    return decks_.size();
}

} // namespace vdj
