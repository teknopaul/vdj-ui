#include "joystick_seq_bridge.h"
#include "alsa_sequencer.h"
#include "config/config_manager.h"

#include <QThread>
#include <fcntl.h>
#include <unistd.h>
#include <linux/joystick.h>

namespace vdj {

// ── Worker ────────────────────────────────────────────────────────────────────

class JoystickSeqBridge::Worker : public QObject {
    Q_OBJECT
public:
    explicit Worker(AlsaSequencer* seq, const QString& device)
        : seq_(seq), device_(device) {}

    void requestStop() { running_ = false; }
    void setSeq(AlsaSequencer* s) { seq_ = s; }

public slots:
    void run()
    {
        int fd = ::open(device_.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) return;

        running_ = true;
        struct js_event ev;

        while (running_) {
            ssize_t n = ::read(fd, &ev, sizeof(ev));
            if (n != sizeof(ev)) {
                struct timespec sl { 0, 5000000L }; // 5 ms poll
                nanosleep(&sl, nullptr);
                continue;
            }

            if (!seq_) continue;

            unsigned int type = ev.type & ~JS_EVENT_INIT;

            if (type == JS_EVENT_AXIS) {
                if (ev.value == 0) continue;
                // Default Logitech mapping (mirrors adj_js.c)
                switch (ev.number) {
                    case 0: seq_->adjustTempo(ev.value > 0 ?  0.1 : -0.1); break;
                    case 1: seq_->adjustTempo(ev.value < 0 ?  1.0 : -1.0); break;
                    case 2: seq_->nudge(ev.value > 0 ?  10 : -10);         break;
                    case 3: seq_->nudge(ev.value > 0 ?   1 : -1);          break;
                    default: break;
                }
            } else if (type == JS_EVENT_BUTTON && ev.value == 1) {
                switch (ev.number) {
                    case 8:  seq_->toggle();            break; // top-left trigger
                    case 9:  seq_->stop();              break; // bottom-left trigger
                    case 10: seq_->quantizedRestart();  break; // top-right trigger
                    case 11: seq_->quantizedRestart();  break; // bottom-right trigger
                    default: break;
                }
            }
        }
        ::close(fd);
    }

private:
    AlsaSequencer* seq_;
    QString        device_;
    volatile bool  running_ = false;
};

// ── JoystickSeqBridge ─────────────────────────────────────────────────────────

JoystickSeqBridge::JoystickSeqBridge(ConfigManager* config, QObject* parent)
    : QObject(parent), config_(config) {}

JoystickSeqBridge::~JoystickSeqBridge()
{
    stop();
}

void JoystickSeqBridge::setSequencer(AlsaSequencer* seq)
{
    seq_ = seq;
    if (worker_) worker_->setSeq(seq);
}

void JoystickSeqBridge::start()
{
    QString device = config_
        ? config_->loadValue(QStringLiteral("joystick"), QStringLiteral("device"))
        : QString();

    if (device.isEmpty()) device = QStringLiteral("/dev/input/js0");

    // Skip silently if device is absent
    if (::access(device.toLocal8Bit().constData(), R_OK) != 0) return;

    thread_ = new QThread(this);
    worker_ = new Worker(seq_, device);
    worker_->moveToThread(thread_);
    connect(thread_, &QThread::started,  worker_, &Worker::run);
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
    thread_->start();
}

void JoystickSeqBridge::stop()
{
    if (worker_) worker_->requestStop();
    if (thread_ && thread_->isRunning()) {
        thread_->quit();
        thread_->wait(1000);
    }
    worker_ = nullptr;
}

} // namespace vdj

#include "joystick_seq_bridge.moc"
