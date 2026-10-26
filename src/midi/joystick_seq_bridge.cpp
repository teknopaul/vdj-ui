#include "joystick_seq_bridge.h"
#include "joystick_map.h"
#include "config/config_manager.h"

#include <QThread>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <linux/joystick.h>

namespace vdj {

// ── Worker ────────────────────────────────────────────────────────────────────

class JoystickSeqBridge::Worker : public QObject {
    Q_OBJECT
public:
    explicit Worker(const QString& device) : device_(device) {}

    void requestStop() { running_ = false; }

signals:
    void action(int kind, double value);     // queued to the bridge in the GUI thread

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
            const unsigned int type = ev.type & ~JS_EVENT_INIT;
            if (type != JS_EVENT_AXIS && type != JS_EVENT_BUTTON) continue;
            if (ev.type & JS_EVENT_INIT) continue;          // initial state dump on open: not a user action
            const JoystickAction a = mapJoystickEvent(type == JS_EVENT_AXIS, ev.number, ev.value);
            if (a.kind != JoystickAction::None)
                emit action(int(a.kind), a.value);
        }
        ::close(fd);
    }

private:
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

void JoystickSeqBridge::start()
{
    if (worker_) return;
    QString device = config_
        ? config_->loadValue(QStringLiteral("joystick"), QStringLiteral("device"))
        : QString();

    if (device.isEmpty()) device = QStringLiteral("/dev/input/js0");

    // Skip silently if device is absent
    if (::access(device.toLocal8Bit().constData(), R_OK) != 0) return;

    thread_ = new QThread(this);
    worker_ = new Worker(device);
    worker_->moveToThread(thread_);
    connect(thread_, &QThread::started,  worker_, &Worker::run);
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &Worker::action, this, [this](int kind, double value) {
        switch (kind) {
        case JoystickAction::Toggle:           emit toggleRequested(); break;
        case JoystickAction::Stop:             emit stopRequested(); break;
        case JoystickAction::QuantizedRestart: emit quantizedRestartRequested(); break;
        case JoystickAction::Nudge:            emit nudgeRequested(int(value)); break;
        case JoystickAction::TempoAdjust:      emit tempoAdjustRequested(value); break;
        default: break;
        }
    });
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
