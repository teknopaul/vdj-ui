#ifndef NOW_PLAYING_H
#define NOW_PLAYING_H

#include <QWidget>

class QLabel;

namespace vdj {

class NowPlaying : public QWidget {
    Q_OBJECT

public:
    explicit NowPlaying(QWidget* parent = nullptr);

    void setDeck(int playerId);
    int deck() const { return playerId_; }
    void setTrack(const QString& title, const QString& artist,
                  const QString& bpm, const QString& key);
    void setBpm(double bpm);
    void setNfsStatus(bool mounted);

private:
    void updateText();

    int playerId_ = 0;
    QString title_;
    QString artist_;
    QString bpm_;
    QString key_;
    double liveBpm_ = 0.0;
    bool nfsMounted_ = false;
    QLabel* deckLabel_;
    QLabel* trackLabel_;
    QLabel* metaLabel_;
};

} // namespace vdj

#endif // NOW_PLAYING_H