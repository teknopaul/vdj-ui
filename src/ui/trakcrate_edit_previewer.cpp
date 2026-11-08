#include "trakcrate_edit_previewer.h"

#include <QThread>

namespace vdj {

EditPreviewer::EditPreviewer(trakcrate::EditRenderCache* cache, QObject* parent) : QObject(parent), cache_(cache)
{
    connect(&player_, &AudioPlayer::positionChanged, this, [this](double ms) {
        if (!playing_) return;
        if (stopOutMs_ >= 0 && ms >= stopOutMs_) {
            stop();
            return;
        }
        emit positionChanged(trakcrate::outputToSourceMs(cache_->plan, ms));
    });
    connect(&player_, &AudioPlayer::finished, this, [this] { stop(); });
}

EditPreviewer::~EditPreviewer()
{
    cancel_ = true;
    if (thread_) {
        thread_->wait();
        delete thread_;
    }
    player_.unload();
}

void EditPreviewer::setPlaying(bool on)
{
    if (on == playing_) return;
    playing_ = on;
    emit stateChanged(on);
}

void EditPreviewer::toggle(const trakcrate::EditRequest& req, double startMs, double stopMs)
{
    if (playing_ || thread_) stop();
    else start(req, startMs, stopMs);
}

void EditPreviewer::stop()
{
    cancel_ = true;
    discard_ = thread_ != nullptr;
    setPlaying(false);                                           // first: stopping the player reports position 0
    player_.stop();
}

void EditPreviewer::start(const trakcrate::EditRequest& req, double startMs, double stopMs)
{
    if (thread_) return;
    if (req.regions.empty()) {
        emit status(QStringLiteral("Add a cut or a loop first"));
        return;
    }
    if (!tmp_.isValid()) {
        emit status(QStringLiteral("Cannot make a temporary folder for the preview"));
        return;
    }
    req_ = req;
    startMs_ = startMs;
    stopMs_ = stopMs;
    cancel_ = false;
    discard_ = false;
    const QString wav = tmp_.filePath(QStringLiteral("preview.wav"));
    const std::shared_ptr<trakcrate::DecodedPcm> held = loaded_;
    emit status(QStringLiteral("Rendering the edit..."));
    thread_ = QThread::create([this, wav, held] {
        std::string err;
        bool ok = trakcrate::ensureRendered(cache_, req_, &err, &cancel_);
        if (ok && cache_->result != held) ok = trakcrate::writePreviewWav(*cache_, wav.toStdString(), &err);      // a new render: a new file
        QMetaObject::invokeMethod(this, "rendered", Qt::QueuedConnection, Q_ARG(bool, ok), Q_ARG(QString, QString::fromStdString(err)));
    });
    thread_->start();
}

void EditPreviewer::rendered(bool ok, const QString& error)
{
    if (thread_) {
        thread_->wait();
        delete thread_;
        thread_ = nullptr;
    }
    if (discard_) return;
    if (!ok) {
        emit status(error);
        return;
    }
    if (cache_->result != loaded_) {
        QString err;
        if (!player_.load(tmp_.filePath(QStringLiteral("preview.wav")), &err)) {
            emit status(QStringLiteral("Cannot play the preview: %1").arg(err));
            return;
        }
        loaded_ = cache_->result;
    }
    stopOutMs_ = stopMs_ >= 0 ? trakcrate::sourceToOutputMs(cache_->plan, stopMs_, true) : -1;
    player_.seekMs(startMs_ >= 0 ? trakcrate::sourceToOutputMs(cache_->plan, startMs_) : 0.0);
    setPlaying(true);
    player_.play();
    emit status(QString());
}

} // namespace vdj
