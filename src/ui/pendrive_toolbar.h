#ifndef PENDRIVE_TOOLBAR_H
#define PENDRIVE_TOOLBAR_H

#include <QList>
#include <QString>
#include <QWidget>

class QComboBox;
class QToolButton;

namespace vdj {

class PendriveRegistry;

// One slim line above the drive viewer: a list of the configured and plugged-in pendrives (choosing one opens it in the viewer only)
// and the two deck-like buttons: USB STOP (eject the exported pendrive) and Insert (export the chosen one to the decks).
class PendriveToolBar : public QWidget {
    Q_OBJECT

public:
    // A deck on the network that may have a rekordbox database to look at.
    struct RemoteDeck {
        int playerId = 0;
        QString ip;
        QString label;                 // "Deck 2  CDJ-3000"
        bool operator==(const RemoteDeck& o) const { return playerId == o.playerId && ip == o.ip && label == o.label; }
    };

    explicit PendriveToolBar(PendriveRegistry* registry, QWidget* parent = nullptr);

    // The pendrive the viewer shows now (selects it in the list when it is one of them, else nothing is selected).
    void setViewed(const QString& folder);
    // The pendrive the decks see now (marked in the list); empty = nothing.
    void setExported(const QString& folder);
    // Dim USB STOP when pressing it will not eject (the button stays clickable: the decks show who uses the stick).
    void setStopHint(bool works, const QString& why);

    // The decks listed under the pendrives (empty when not connected). Choosing one asks to look at its database: deckRequested.
    void setRemoteDecks(const QList<RemoteDeck>& decks);
    // The viewer shows this deck's database (0 = a local pendrive or folder). Insert and USB STOP only make sense for a local one, so they
    // are hidden while a deck is shown.
    void setRemoteViewed(int playerId);
    int remoteViewed() const { return remotePid_; }
    bool buttonsShown() const;

    QString selectedFolder() const;
    QComboBox* combo() const { return combo_; }

signals:
    void openRequested(const QString& folder);
    void insertRequested(const QString& folder, const QString& name);
    void stopRequested();
    void deckRequested(int playerId, const QString& ip);

private:
    void rebuild();
    void updateButtons();

    PendriveRegistry* registry_;
    QComboBox* combo_;
    QToolButton* stopBtn_;
    QToolButton* insertBtn_;
    QString viewed_, exported_;
    QList<RemoteDeck> decks_;
    int remotePid_ = 0;
};

} // namespace vdj

#endif // PENDRIVE_TOOLBAR_H
