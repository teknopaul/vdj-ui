#include "now_playing.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace vdj {

NowPlaying::NowPlaying(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(54);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(2);

    auto* top = new QHBoxLayout();
    deckLabel_ = new QLabel(QStringLiteral("DECK 0"), this);
    deckLabel_->setStyleSheet(QStringLiteral("color: #55cc77; font-weight: bold;"));
    top->addWidget(deckLabel_);
    top->addStretch(1);
    layout->addLayout(top);

    trackLabel_ = new QLabel(QStringLiteral("-- no track --"), this);
    trackLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(trackLabel_);

    metaLabel_ = new QLabel(this);
    metaLabel_->setStyleSheet(QStringLiteral("color: #999999;"));
    layout->addWidget(metaLabel_);

    updateText();
}

void NowPlaying::setDeck(int playerId)
{
    playerId_ = playerId;
    updateText();
}

void NowPlaying::setTrack(const QString& title, const QString& artist,
                          const QString& bpm, const QString& key)
{
    title_ = title;
    artist_ = artist;
    bpm_ = bpm;
    key_ = key;
    updateText();
}

void NowPlaying::setBpm(double bpm)
{
    liveBpm_ = bpm;
    updateText();
}

void NowPlaying::setNfsStatus(bool mounted)
{
    nfsMounted_ = mounted;
    updateText();
}

void NowPlaying::updateText()
{
    deckLabel_->setText(QStringLiteral("DECK %1").arg(playerId_));

    if (title_.isEmpty()) {
        trackLabel_->setText(QStringLiteral("-- no track --"));
        metaLabel_->setText(QString());
        return;
    }

    QString title = title_;
    if (!artist_.isEmpty())
        title += QStringLiteral("  —  ") + artist_;

    QString meta;
    if (liveBpm_ > 0)
        meta += QStringLiteral("BPM %1").arg(liveBpm_, 0, 'f', 2);
    else if (!bpm_.isEmpty())
        meta += QStringLiteral("BPM %1").arg(bpm_);
    if (!key_.isEmpty()) {
        if (!meta.isEmpty()) meta += QStringLiteral("   ");
        meta += QStringLiteral("Key %1").arg(key_);
    }
    if (nfsMounted_)
        meta += QStringLiteral("   [NFS]");

    trackLabel_->setText(title);
    metaLabel_->setText(meta);
}

} // namespace vdj