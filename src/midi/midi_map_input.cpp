#include "midi_map_input.h"

#include <alsa/asoundlib.h>
#include <poll.h>

#include <QThread>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <QMetaObject>

namespace vdj {

// ── .adjmm parser ─────────────────────────────────────────────────────────────

static void parseLine(const QString& rawLine, int lineNo, AdjmmMap* map)
{
    QString line = rawLine.trimmed();
    int ci = line.indexOf('#');
    if (ci == 0) return;
    if (ci > 0)  line = line.left(ci).trimmed();
    if (line.isEmpty()) return;

    const QStringList p = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                     Qt::SkipEmptyParts);
    if (p.size() < 4) {
        qWarning("adjmm line %d: expected 4 fields, got %d", lineNo, p.size());
        return;
    }

    const QString& name = p[0];
    const QString& ch   = p[1];
    const QString& ctrl = p[2];
    const QString& val  = p[3];

    int idx = -1;
    if      (name == QStringLiteral("start"))         idx = ADJMM_OP_START - 1;
    else if (name == QStringLiteral("stop"))          idx = ADJMM_OP_STOP - 1;
    else if (name == QStringLiteral("toggle"))        idx = ADJMM_OP_TOGGLE - 1;
    else if (name == QStringLiteral("q_restart"))     idx = ADJMM_OP_Q_RESTART - 1;
    else if (name == QStringLiteral("nudge_fwd"))     idx = ADJMM_OP_NUDGE_FWD - 1;
    else if (name == QStringLiteral("nudge_ffwd"))    idx = ADJMM_OP_NUDGE_FFWD - 1;
    else if (name == QStringLiteral("nudge_rew"))     idx = ADJMM_OP_NUDGE_REW - 1;
    else if (name == QStringLiteral("nudge_frew"))    idx = ADJMM_OP_NUDGE_FREW - 1;
    else if (name == QStringLiteral("tempo_inc_min")) idx = ADJMM_OP_TEMPO_INC_MIN - 1;
    else if (name == QStringLiteral("tempo_inc"))     idx = ADJMM_OP_TEMPO_INC - 1;
    else if (name == QStringLiteral("tempo_inc_1"))   idx = ADJMM_OP_TEMPO_INC_1 - 1;
    else if (name == QStringLiteral("tempo_dec_min")) idx = ADJMM_OP_TEMPO_DEC_MIN - 1;
    else if (name == QStringLiteral("tempo_dec"))     idx = ADJMM_OP_TEMPO_DEC - 1;
    else if (name == QStringLiteral("tempo_dec_1"))   idx = ADJMM_OP_TEMPO_DEC_1 - 1;
    else if (name == QStringLiteral("slider"))        idx = ADJMM_OP_SLIDER - 1;
    else if (name == QStringLiteral("slider_on"))     idx = ADJMM_OP_SLIDER_ON - 1;
    else if (name == QStringLiteral("slider_off"))    idx = ADJMM_OP_SLIDER_OFF - 1;
    else if (name == QStringLiteral("copy_master"))   idx = ADJMM_OP_COPY_MASTER - 1;
    else if (name == QStringLiteral("become_master")) idx = ADJMM_OP_BECOME_MASTER - 1;
    else {
        // skip unknown names — forward compatibility when new ops are added
        return;
    }

    // channel: * = any, number = 1-16
    if (ch == QStringLiteral("*")) {
        map->ops[idx].channel = ADJMM_ANY_CHANNEL;
    } else {
        bool ok; int v = ch.toInt(&ok);
        if (!ok || v < 0 || v > 16) {
            qWarning("adjmm line %d: invalid channel '%s'", lineNo, qPrintable(ch));
            return;
        }
        map->ops[idx].channel = static_cast<uint8_t>(v);
    }

    // controller: N = NoteOn, O = NoteOff, number = CC (0-127)
    if (ctrl.startsWith('N')) {
        map->ops[idx].controller = ADJMM_NOTEON;
    } else if (ctrl.startsWith('O')) {
        map->ops[idx].controller = ADJMM_NOTEOFF;
    } else {
        bool ok; int v = ctrl.toInt(&ok);
        if (!ok || v < 0 || v > 127) {
            qWarning("adjmm line %d: invalid controller '%s'", lineNo, qPrintable(ctrl));
            return;
        }
        map->ops[idx].controller = static_cast<uint8_t>(v);
    }

    // value: * = any, ++ / -- / + / - = jog wheel, number = exact
    if (val == QStringLiteral("*"))        map->ops[idx].value = ADJMM_ANY_VALUE;
    else if (val == QStringLiteral("++"))  map->ops[idx].value = ADJMM_WHEEL_PLUS_PLUS;
    else if (val == QStringLiteral("--"))  map->ops[idx].value = ADJMM_WHEEL_MINUS_MINUS;
    else if (val == QStringLiteral("+"))   map->ops[idx].value = ADJMM_WHEEL_PLUS;
    else if (val == QStringLiteral("-"))   map->ops[idx].value = ADJMM_WHEEL_MINUS;
    else {
        bool ok; int v = val.toInt(&ok);
        if (!ok || v < 0 || v > 127) {
            qWarning("adjmm line %d: invalid value '%s'", lineNo, qPrintable(val));
            return;
        }
        map->ops[idx].value = static_cast<uint8_t>(v);
    }
}

AdjmmMap* AdjmmMap::readFromFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("adjmm: cannot open '%s'", qPrintable(path));
        return nullptr;
    }

    auto* map = new AdjmmMap();
    QTextStream in(&f);
    for (int lineNo = 1; !in.atEnd(); ++lineNo)
        parseLine(in.readLine(), lineNo, map);

    return map;
}

// ── Background worker ─────────────────────────────────────────────────────────

class MidiMapInput::Worker : public QObject {
    Q_OBJECT
public:
    explicit Worker(AdjmmMap* map, MidiMapInput* owner)
        : map_(map), owner_(owner) {}

    bool init()
    {
        if (snd_seq_open(&handle_, "default", SND_SEQ_OPEN_INPUT,
                         SND_SEQ_NONBLOCK) < 0)
            return false;
        snd_seq_set_client_name(handle_, "vdj-midi-in");
        port_ = snd_seq_create_simple_port(
            handle_, "midi-in",
            SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
            SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_MIDI_GENERIC);
        client_ = snd_seq_client_id(handle_);
        return port_ >= 0;
    }

    int clientId() const { return client_; }
    int portId() const { return port_; }

    void requestStop() { running_ = false; }

public slots:
    void run()
    {
        int npfds = snd_seq_poll_descriptors_count(handle_, POLLIN);
        auto* pfds = new pollfd[npfds];
        snd_seq_poll_descriptors(handle_, pfds, npfds, POLLIN);

        while (running_) {
            if (poll(pfds, npfds, 100) <= 0) continue; // 100 ms timeout → clean stop

            snd_seq_event_t* ev = nullptr;
            while (snd_seq_event_input_pending(handle_, 0) > 0) {
                if (snd_seq_event_input(handle_, &ev) >= 0)
                    dispatch(ev);
            }
        }

        delete[] pfds;
        snd_seq_close(handle_);
        handle_ = nullptr;
    }

private:
    static int matchCtrl(const AdjmmMap* m, int ch, int param, int val)
    {
        for (int i = 0; i < ADJMM_MAX_OP; ++i) {
            const auto& op = m->ops[i];
            if (op.controller == ADJMM_UNSET) continue;
            if (op.channel != ADJMM_ANY_CHANNEL && op.channel != uint8_t(ch)) continue;
            if (op.controller != uint8_t(param)) continue;
            if (op.value == ADJMM_ANY_VALUE || op.value == uint8_t(val)) return i + 1;
        }
        return 0;
    }

    // Relative jog-wheel CC (center = 64).
    static int matchWheel(const AdjmmMap* m, int ch, int param, int val)
    {
        if (val == 64) return 0;
        for (int i = 0; i < ADJMM_MAX_OP; ++i) {
            const auto& op = m->ops[i];
            if (op.controller == ADJMM_UNSET) continue;
            if (op.controller != uint8_t(param)) continue;
            if (op.channel != ADJMM_ANY_CHANNEL && op.channel != uint8_t(ch)) continue;
            if ((op.value == ADJMM_WHEEL_PLUS_PLUS   && val > 80)  ||
                (op.value == ADJMM_WHEEL_MINUS_MINUS && val < 48)  ||
                (op.value == ADJMM_WHEEL_PLUS        && val > 64)  ||
                (op.value == ADJMM_WHEEL_MINUS       && val < 64))
                return i + 1;
        }
        return 0;
    }

    static int matchNote(const AdjmmMap* m, int ch, int note)
    {
        for (int i = 0; i < ADJMM_MAX_OP; ++i) {
            const auto& op = m->ops[i];
            if (op.controller != ADJMM_NOTEON) continue;
            if (op.channel != ADJMM_ANY_CHANNEL && op.channel != uint8_t(ch)) continue;
            if (op.value == ADJMM_ANY_VALUE || op.value == uint8_t(note)) return i + 1;
        }
        return 0;
    }

    void dispatch(snd_seq_event_t* ev)
    {
        int op = 0;
        if (ev->type == SND_SEQ_EVENT_CONTROLLER) {
            int ch  = ev->data.control.channel + 1; // ALSA 0-15 → human 1-16
            int par = ev->data.control.param;
            int val = ev->data.control.value;
            op = matchCtrl(map_, ch, par, val);
            if (op == 0) op = matchWheel(map_, ch, par, val);
            if (op == ADJMM_OP_SLIDER) {
                handleSlider(val);
                return;
            }
        } else if (ev->type == SND_SEQ_EVENT_NOTEON) {
            op = matchNote(map_, ev->data.note.channel + 1, ev->data.note.note);
        }

        if (op == 0) return;
        routeOp(op);
    }

    void handleSlider(int val)
    {
        if (!sliderOn_) return;
        if (sliderValue_ == -1) { sliderValue_ = val; return; }
        if (val == sliderValue_)  return;
        double delta = (val - sliderValue_) * 0.1;
        emit owner_->tempoAdjustRequested(delta);
        sliderValue_ = val;
    }

    void routeOp(int op)
    {
        switch (op) {
            case ADJMM_OP_START:
                emit owner_->startRequested();
                break;
            case ADJMM_OP_STOP:
                emit owner_->stopRequested();
                break;
            case ADJMM_OP_TOGGLE:
                emit owner_->toggleRequested();
                break;
            case ADJMM_OP_Q_RESTART:
                emit owner_->restartRequested();
                break;
            case ADJMM_OP_NUDGE_FWD:
                emit owner_->nudgeRequested(10);
                break;
            case ADJMM_OP_NUDGE_FFWD:
                emit owner_->nudgeRequested(20);
                break;
            case ADJMM_OP_NUDGE_REW:
                emit owner_->nudgeRequested(-10);
                break;
            case ADJMM_OP_NUDGE_FREW:
                emit owner_->nudgeRequested(-20);
                break;
            case ADJMM_OP_TEMPO_INC_MIN:
                emit owner_->tempoAdjustRequested(0.01);
                break;
            case ADJMM_OP_TEMPO_INC:
                emit owner_->tempoAdjustRequested(0.1);
                break;
            case ADJMM_OP_TEMPO_INC_1:
                emit owner_->tempoAdjustRequested(1.0);
                break;
            case ADJMM_OP_TEMPO_DEC_MIN:
                emit owner_->tempoAdjustRequested(-0.01);
                break;
            case ADJMM_OP_TEMPO_DEC:
                emit owner_->tempoAdjustRequested(-0.1);
                break;
            case ADJMM_OP_TEMPO_DEC_1:
                emit owner_->tempoAdjustRequested(-1.0);
                break;
            case ADJMM_OP_SLIDER_ON:
                sliderOn_    = true;
                sliderValue_ = -1;
                break;
            case ADJMM_OP_SLIDER_OFF:
                sliderOn_    = false;
                sliderValue_ = -1;
                break;
            case ADJMM_OP_COPY_MASTER:
                emit owner_->copyMasterRequested();
                break;
            case ADJMM_OP_BECOME_MASTER:
                emit owner_->becomeMasterRequested();
                break;
            default:
                break;
        }
    }

    AdjmmMap*       map_;
    MidiMapInput*   owner_;
    snd_seq_t*      handle_      = nullptr;
    int             client_      = -1;
    int             port_        = -1;
    volatile bool   running_     = true;
    bool            sliderOn_    = false;
    int             sliderValue_ = -1;
};

// ── MidiMapInput public API ───────────────────────────────────────────────────

MidiMapInput::MidiMapInput(QObject* parent)
    : QObject(parent) {}

MidiMapInput::~MidiMapInput()
{
    stop();
    delete map_;
}

bool MidiMapInput::start(const QString& mapPath)
{
    stop();
    delete map_;
    map_ = AdjmmMap::readFromFile(mapPath);
    if (!map_) {
        emit errorOccurred(QStringLiteral("Cannot read map file: ") + mapPath);
        return false;
    }

    worker_ = new Worker(map_, this);
    if (!worker_->init()) {
        emit errorOccurred(QStringLiteral("ALSA MIDI input init failed"));
        delete worker_;
        worker_ = nullptr;
        return false;
    }

    clientId_ = worker_->clientId();
    portId_   = worker_->portId();

    thread_ = new QThread(this);
    worker_->moveToThread(thread_);
    connect(thread_, &QThread::started,  worker_, &Worker::run);
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
    thread_->start();
    return true;
}

void MidiMapInput::stop()
{
    if (worker_) worker_->requestStop();
    if (thread_ && thread_->isRunning()) {
        thread_->quit();
        thread_->wait(1000);
    }
    if (thread_) { thread_->deleteLater(); thread_ = nullptr; }
    worker_ = nullptr;
}

bool MidiMapInput::isRunning() const
{
    return thread_ && thread_->isRunning();
}

} // namespace vdj

#include "midi_map_input.moc"
