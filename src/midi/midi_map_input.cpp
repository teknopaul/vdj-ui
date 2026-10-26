#include "midi_map_input.h"

#include <alsa/asoundlib.h>
#include <poll.h>

#include <QHash>
#include <QThread>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <QMetaObject>

namespace vdj {

// ── .adjmm parser ─────────────────────────────────────────────────────────────

bool AdjmmMap::isDjtoolzOpName(const QString& name)
{
    static const QRegularExpression strip(QStringLiteral("^strip[1-8]_(mute|solo|rec|select|knob|fader)$"));
    return name == QStringLiteral("track_back") || name == QStringLiteral("track_fwd")
        || name == QStringLiteral("mode_toggle") || strip.match(name).hasMatch();
}

// Parses channel / controller / value into *op; false (with a warning) when a field is invalid.
static bool parseFields(const QString& ch, const QString& ctrl, const QString& val, int lineNo, AdjmmOp* op)
{
    // channel: * = any, number = 1-16
    if (ch == QStringLiteral("*")) {
        op->channel = ADJMM_ANY_CHANNEL;
    } else {
        bool ok; int v = ch.toInt(&ok);
        if (!ok || v < 0 || v > 16) {
            qWarning("adjmm line %d: invalid channel '%s'", lineNo, qPrintable(ch));
            return false;
        }
        op->channel = static_cast<uint8_t>(v);
    }

    // controller: N = NoteOn, O = NoteOff, number = CC (0-127)
    if (ctrl.startsWith('N')) {
        op->controller = ADJMM_NOTEON;
    } else if (ctrl.startsWith('O')) {
        op->controller = ADJMM_NOTEOFF;
    } else {
        bool ok; int v = ctrl.toInt(&ok);
        if (!ok || v < 0 || v > 127) {
            qWarning("adjmm line %d: invalid controller '%s'", lineNo, qPrintable(ctrl));
            return false;
        }
        op->controller = static_cast<uint8_t>(v);
    }

    // value: * = any, ++ / -- / + / - = jog wheel, number = exact
    if (val == QStringLiteral("*"))        op->value = ADJMM_ANY_VALUE;
    else if (val == QStringLiteral("++"))  op->value = ADJMM_WHEEL_PLUS_PLUS;
    else if (val == QStringLiteral("--"))  op->value = ADJMM_WHEEL_MINUS_MINUS;
    else if (val == QStringLiteral("+"))   op->value = ADJMM_WHEEL_PLUS;
    else if (val == QStringLiteral("-"))   op->value = ADJMM_WHEEL_MINUS;
    else {
        bool ok; int v = val.toInt(&ok);
        if (!ok || v < 0 || v > 127) {
            qWarning("adjmm line %d: invalid value '%s'", lineNo, qPrintable(val));
            return false;
        }
        op->value = static_cast<uint8_t>(v);
    }
    return true;
}

static void parseLine(const QString& rawLine, int lineNo, AdjmmMap* map)
{
    QString line = rawLine.trimmed();
    if (line.startsWith(QStringLiteral("#!"))) {            // "#! key value": a setting, not a mapping
        const QString rest = line.mid(2).trimmed();
        const int sp = rest.indexOf(QRegularExpression(QStringLiteral("\\s")));
        const QString key = (sp < 0 ? rest : rest.left(sp)).toLower();
        if (!key.isEmpty())
            map->directives.insert(key, sp < 0 ? QString() : rest.mid(sp).trimmed());
        return;
    }
    int ci = line.indexOf('#');
    if (ci == 0) return;
    if (ci > 0)  line = line.left(ci).trimmed();
    if (line.isEmpty()) return;

    const QStringList p = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                     Qt::SkipEmptyParts);
    if (p.size() < 4) {
        qWarning("adjmm line %d: expected 4 fields, got %d", lineNo, int(p.size()));
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
    else if (AdjmmMap::isDjtoolzOpName(name)) {
        AdjmmOp op;
        if (parseFields(ch, ctrl, val, lineNo, &op)) map->named[name] = op;
        return;
    } else {
        if (name.startsWith(QStringLiteral("strip")))
            qWarning("adjmm line %d: bad DJ Tools op '%s' (strips are 1-8)", lineNo, qPrintable(name));
        // skip unknown names — forward compatibility when new ops are added
        return;
    }

    AdjmmOp op;
    if (parseFields(ch, ctrl, val, lineNo, &op)) map->ops[idx] = op;
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

            // The port is non-blocking: read until the kernel queue is empty. (Asking
            // snd_seq_event_input_pending(handle, 0) first only looks at the library's own buffer, which is empty
            // after a poll(), so no event was ever read.)
            snd_seq_event_t* ev = nullptr;
            for (;;) {
                const int r = snd_seq_event_input(handle_, &ev);
                if (r == -ENOSPC) continue;          // input queue overran: events were lost, carry on
                if (r < 0 || !ev) break;             // -EAGAIN: nothing more to read
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

    void report(const QString& what, int op)
    {
        if (owner_->monitoring())
            emit owner_->midiEvent(what, op);
    }

    // ALSA client name of the sender, looked up once per client id (reader thread only).
    QString sourceName(int client)
    {
        auto it = names_.find(client);
        if (it != names_.end()) return it.value();
        QString name;
        snd_seq_client_info_t* ci;
        snd_seq_client_info_alloca(&ci);
        if (snd_seq_get_any_client_info(handle_, client, ci) >= 0)
            name = QString::fromUtf8(snd_seq_client_info_get_name(ci));
        else
            return name;                                 // not cached: the client may appear later
        names_.insert(client, name);
        return name;
    }

    void dispatch(snd_seq_event_t* ev)
    {
        int op = 0;
        if (ev->type == SND_SEQ_EVENT_CONTROLLER) {
            int ch  = ev->data.control.channel + 1; // ALSA 0-15 → human 1-16
            int par = ev->data.control.param;
            int val = ev->data.control.value;
            const QString src = sourceName(ev->source.client);
            emit owner_->controlEvent(ch, par, val, src);
            if (owner_->isExcluded(src)) return;
            op = matchCtrl(map_, ch, par, val);
            if (op == 0) op = matchWheel(map_, ch, par, val);
            report(QStringLiteral("ch %1  CC %2 = %3").arg(ch).arg(par).arg(val), op);
            if (op == ADJMM_OP_SLIDER) {
                handleSlider(val);
                return;
            }
        } else if (ev->type == SND_SEQ_EVENT_NOTEON) {
            const int ch = ev->data.note.channel + 1;
            const QString src = sourceName(ev->source.client);
            emit owner_->noteEvent(ch, ev->data.note.note, ev->data.note.velocity, true, src);
            if (owner_->isExcluded(src)) return;
            op = matchNote(map_, ch, ev->data.note.note);
            report(QStringLiteral("ch %1  NOTE ON  note %2 velocity %3").arg(ch).arg(ev->data.note.note).arg(ev->data.note.velocity), op);
        } else if (ev->type == SND_SEQ_EVENT_NOTEOFF) {
            emit owner_->noteEvent(ev->data.note.channel + 1, ev->data.note.note, ev->data.note.velocity, false,
                                   sourceName(ev->source.client));
            report(QStringLiteral("ch %1  NOTE OFF note %2 (maps ignore note off)").arg(ev->data.note.channel + 1).arg(ev->data.note.note), 0);
        } else if (ev->type == SND_SEQ_EVENT_PITCHBEND) {
            report(QStringLiteral("ch %1  PITCH BEND %2 (not mappable)").arg(ev->data.control.channel + 1).arg(ev->data.control.value), 0);
        } else if (ev->type == SND_SEQ_EVENT_CLOCK || ev->type == SND_SEQ_EVENT_SENSING) {
            return;                                  // constant noise
        } else {
            report(QStringLiteral("ALSA event type %1 (not mappable)").arg(int(ev->type)), 0);
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
    QHash<int, QString> names_;
};

// ── MidiMapInput public API ───────────────────────────────────────────────────

MidiMapInput::MidiMapInput(QObject* parent)
    : QObject(parent) {}

MidiMapInput::~MidiMapInput()
{
    stop();
    delete map_;
}

QString MidiMapInput::opName(int op)
{
    static const char* const names[ADJMM_MAX_OP] = {
        "start", "stop", "toggle", "q_restart", "nudge_ffwd", "nudge_fwd", "nudge_frew", "nudge_rew", "(unused 9)",
        "tempo_inc_min", "tempo_inc", "tempo_inc_1", "tempo_dec_min", "tempo_dec", "tempo_dec_1",
        "slider", "slider_on", "slider_off", "copy_master", "become_master" };
    return op >= 1 && op <= ADJMM_MAX_OP ? QString::fromLatin1(names[op - 1]) : QString();
}

int MidiMapInput::mappedOps() const
{
    int n = 0;
    if (map_)
        for (const AdjmmOp& op : map_->ops)
            if (op.controller != ADJMM_UNSET) ++n;
    return n;
}

void MidiMapInput::setExcludedSource(const QString& name)
{
    std::lock_guard<std::mutex> lock(excludeMutex_);
    excluded_ = name;
}

bool MidiMapInput::isExcluded(const QString& source) const
{
    std::lock_guard<std::mutex> lock(excludeMutex_);
    return !excluded_.isEmpty() && source.contains(excluded_, Qt::CaseInsensitive);
}

QStringList MidiMapInput::sourceNames() const
{
    QStringList names;
    if (clientId_ < 0) return names;
    snd_seq_t* seq = nullptr;                        // a handle of our own: the reader thread owns the other
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) < 0) return names;
    snd_seq_query_subscribe_t* q;
    snd_seq_query_subscribe_alloca(&q);
    snd_seq_addr_t root;
    root.client = static_cast<unsigned char>(clientId_);
    root.port = static_cast<unsigned char>(portId_);
    snd_seq_query_subscribe_set_root(q, &root);
    snd_seq_query_subscribe_set_type(q, SND_SEQ_QUERY_SUBS_WRITE);
    snd_seq_query_subscribe_set_index(q, 0);
    while (snd_seq_query_port_subscribers(seq, q) >= 0) {
        const snd_seq_addr_t* src = snd_seq_query_subscribe_get_addr(q);
        snd_seq_client_info_t* ci;
        snd_seq_client_info_alloca(&ci);
        if (snd_seq_get_any_client_info(seq, src->client, ci) >= 0)
            names << QStringLiteral("%1:%2 %3").arg(src->client).arg(src->port)
                         .arg(QString::fromUtf8(snd_seq_client_info_get_name(ci)));
        snd_seq_query_subscribe_set_index(q, snd_seq_query_subscribe_get_index(q) + 1);
    }
    snd_seq_close(seq);
    return names;
}

bool MidiMapInput::start(const QString& mapPath)
{
    stop();
    delete map_;
    mapPath_ = mapPath;
    map_ = mapPath.isEmpty() ? new AdjmmMap() : AdjmmMap::readFromFile(mapPath);
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
