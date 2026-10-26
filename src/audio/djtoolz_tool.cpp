#include "djtoolz_tool.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include "swing.h"

namespace vdj {

static std::atomic<int> g_nextId{ 1 };

DjtoolzTool::DjtoolzTool()
    : id_(g_nextId++)
{
}

ToolSettings DjtoolzTool::settings() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return s_;
}

void DjtoolzTool::setSettings(const ToolSettings& in)
{
    ToolSettings s = in;
    s.offsetMs = std::clamp(s.offsetMs, -kMaxOffsetMs, kMaxOffsetMs);
    s.midiChannel = std::clamp(s.midiChannel, 1, 16);
    s.midiNote = std::clamp(s.midiNote, 0, 127);
    s.midiVelocity = std::clamp(s.midiVelocity, 1, 127);
    s.gain = std::clamp(s.gain, 0.0f, 4.0f);
    s.volume = std::clamp(s.volume, 0, kMaxVolume);
    s.swing = swingClamp(s.swing);
    for (DrumVoice& v : s.drums) {
        v.volume = std::clamp(v.volume, 0, kMaxVolume);
        v.offsetMs = std::clamp(v.offsetMs, -kMaxOffsetMs, kMaxOffsetMs);
        v.chokeSteps = drumChokeClamp(v.chokeSteps);
        for (quint8& st : v.steps)
            st = std::min<quint8>(st, StepHalf);
    }
    s.beatMask &= 0x0F;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        s_ = s;
    }
    settingsApplied(s);
}

void DjtoolzTool::plan(double fromBeat, double toBeat, std::vector<PlannedHit>& out)
{
    if (!periodic())
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    double lo = fromBeat, hi = toBeat;
    if (!s_.enabled) {
        if (oneBarStart_ < 0.0)
            return;
        lo = std::max(lo, oneBarStart_);             // only the one bar
        hi = std::min(hi, oneBarStart_ + 4.0);
    }
    collectHits(lo, hi, s_, out);
}

void DjtoolzTool::collectHits(double lo, double hi, const ToolSettings& s, std::vector<PlannedHit>& out) const
{
    for (long b = long(std::ceil(lo)); double(b) < hi; ++b)
        if (hitsBeat(s, b))
            out.push_back(PlannedHit{ double(b) });
}

bool Dembomatik::stepIsOn(int step)
{
    //                           1 . . 4 . . 7 . 9 . . . . 14 . .
    static const bool kPattern[kSteps] = { true, false, false, true, false, false, true, false,
                                           true, false, false, false, false, true, false, false };
    return kPattern[((step % kSteps) + kSteps) % kSteps];
}

void Dembomatik::collectHits(double lo, double hi, const ToolSettings& s, std::vector<PlannedHit>& out) const
{
    // a step is a quarter of a beat; the bar is 4 beats = 16 steps. Swing delays the odd steps (swing.h).
    for (long k = long(std::ceil(lo * 4.0 - 1e-9)); double(k) / 4.0 < hi - 1e-12; ++k)
        if (stepIsOn(int(k % kSteps)))
            out.push_back(PlannedHit{ double(k) / 4.0 + swingDelayBeats(int(k % kSteps), s.swing) });
}

void DrumMachine::collectHits(double lo, double hi, const ToolSettings& s, std::vector<PlannedHit>& out) const
{
    // a hit is planned once, in the window that holds its grid position; the swing only moves the time it plays at
    for (long k = long(std::ceil(lo * 4.0 - 1e-9)); double(k) / 4.0 < hi - 1e-12; ++k) {
        const int step = int(k % kDrumSteps);
        for (int v = 0; v < kDrumVoices; ++v) {
            if (s.drums[v].steps[step] == StepOff)
                continue;
            PlannedHit h;
            h.beat = double(k) / 4.0 + swingDelayBeats(step, s.swing);
            h.voice = v;
            h.level = drumStepLevel(s.drums[v].steps[step]);
            out.push_back(h);
        }
    }
}

void DjtoolzTool::fireOneBar(double nowBeat)
{
    if (!supportsOneBar())
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (oneBarStart_ >= 0.0 && nowBeat < oneBarStart_ + 4.0) {
        oneBarStart_ = -1.0;                         // armed or playing: cancel
        return;
    }
    oneBarStart_ = (std::floor(nowBeat / 4.0) + 1.0) * 4.0;
}

void DjtoolzTool::finishOneBar(double nowBeat)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (oneBarStart_ >= 0.0 && nowBeat >= oneBarStart_ + 4.0)
        oneBarStart_ = -1.0;
}

void DjtoolzTool::cancelOneBar()
{
    std::lock_guard<std::mutex> lock(mutex_);
    oneBarStart_ = -1.0;
}

OneBarState DjtoolzTool::oneBarState(double nowBeat) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (oneBarStart_ < 0.0)
        return OneBarState::Idle;
    if (nowBeat < oneBarStart_)
        return OneBarState::Armed;
    if (nowBeat < oneBarStart_ + 4.0)
        return OneBarState::Playing;
    return OneBarState::Idle;
}

QString DjtoolzTool::typeKey(Type t)
{
    switch (t) {
    case Bombomatic: return QStringLiteral("bombomatic");
    case StabPlayer: return QStringLiteral("stab");
    case AcidLoop: return QStringLiteral("acid");
    case OneShot: return QStringLiteral("oneshot");
    case MidiMatic: return QStringLiteral("midi");
    case Dembomatik: return QStringLiteral("dembow");
    case DrumMachine: return QStringLiteral("drums");
    case Fx: return QStringLiteral("fx");
    }
    return QString();
}

std::shared_ptr<DjtoolzTool> DjtoolzTool::create(Type type)
{
    std::shared_ptr<DjtoolzTool> m;
    switch (type) {
    case Bombomatic: m = std::make_shared<vdj::Bombomatic>(); break;
    case StabPlayer: m = std::make_shared<vdj::StabPlayer>(); break;
    case AcidLoop: m = std::make_shared<vdj::AcidLoop>(); break;
    case OneShot: m = std::make_shared<vdj::OneShot>(); break;
    case MidiMatic: m = std::make_shared<vdj::MidiMatic>(); break;
    case Dembomatik: m = std::make_shared<vdj::Dembomatik>(); break;
    case DrumMachine: m = std::make_shared<vdj::DrumMachine>(); break;
    case Fx: m = std::make_shared<vdj::FxTool>(); break;
    }
    ToolSettings s;
    s.name = m->typeName();
    m->setSettings(s);
    return m;
}

QVariantMap DjtoolzTool::toVariant() const
{
    const ToolSettings s = settings();
    QVariantMap m;
    m["type"] = typeKey(type());
    m["name"] = s.name;
    m["enabled"] = s.enabled;
    m["sample"] = s.samplePath;
    m["gain"] = double(s.gain);
    m["volume"] = s.volume;
    m["offsetMs"] = s.offsetMs;
    m["beatMask"] = int(s.beatMask);
    m["reverseSync"] = s.reverseSync;
    m["quantize"] = s.quantize;
    m["stretch"] = s.stretch;
    m["manualBpm"] = s.manualBpm;
    m["midiDevice"] = s.midiDevice;
    m["midiChannel"] = s.midiChannel;
    m["midiNote"] = s.midiNote;
    m["midiVelocity"] = s.midiVelocity;
    if (supportsSwing())
        m["swing"] = s.swing;
    if (hasDrumVoices()) {
        QVariantList voices;
        for (const DrumVoice& v : s.drums)
            voices.append(drumVoiceToVariant(v));
        m["voices"] = voices;
    }
    extraToVariant(m);
    return m;
}

std::shared_ptr<DjtoolzTool> DjtoolzTool::fromVariant(const QVariantMap& m)
{
    const QString key = m.value("type").toString();
    for (Type t : { Bombomatic, StabPlayer, AcidLoop, OneShot, MidiMatic, Dembomatik, DrumMachine, Fx }) {
        if (typeKey(t) != key)
            continue;
        auto mod = create(t);
        if (!mod)
            return nullptr;
        ToolSettings s = mod->settings();           // defaults for keys a preset does not have
        s.name = m.value("name", s.name).toString();
        s.enabled = m.value("enabled", s.enabled).toBool();
        s.samplePath = m.value("sample", s.samplePath).toString();
        s.gain = float(m.value("gain", double(s.gain)).toDouble());
        s.volume = m.value("volume", s.volume).toInt();
        s.offsetMs = m.value("offsetMs", s.offsetMs).toDouble();
        s.beatMask = quint8(m.value("beatMask", int(s.beatMask)).toInt());
        s.reverseSync = m.value("reverseSync", s.reverseSync).toBool();
        s.quantize = m.value("quantize", s.quantize).toBool();
        s.stretch = m.value("stretch", s.stretch).toBool();
        s.manualBpm = m.value("manualBpm", s.manualBpm).toDouble();
        s.midiDevice = m.value("midiDevice", s.midiDevice).toString();
        s.midiChannel = m.value("midiChannel", s.midiChannel).toInt();
        s.midiNote = m.value("midiNote", s.midiNote).toInt();
        s.midiVelocity = m.value("midiVelocity", s.midiVelocity).toInt();
        if (mod->supportsSwing())
            s.swing = m.value("swing", s.swing).toInt();
        if (mod->hasDrumVoices()) {
            const QVariantList voices = m.value("voices").toList();
            for (int i = 0; i < kDrumVoices && i < voices.size(); ++i)
                drumVoiceFromVariant(voices[i].toMap(), &s.drums[i]);          // a bad pattern keeps the (empty) default
        }
        mod->setSettings(s);
        mod->extraFromVariant(m);
        return mod;
    }
    return nullptr;
}

FxTool::Spec FxTool::spec() const
{
    std::lock_guard<std::mutex> lock(specMutex_);
    return spec_;
}

void FxTool::setPlugin(const QString& key)
{
    {
        std::lock_guard<std::mutex> lock(specMutex_);
        if (spec_.plugin == key)
            return;
        spec_.plugin = key;
        spec_.values.clear();
        spec_.knob.clear();
        spec_.fader.clear();
    }
    ++revision_;
}

void FxTool::setValue(const QString& control, float value)
{
    {
        std::lock_guard<std::mutex> lock(specMutex_);
        spec_.values[control] = value;
    }
    ++revision_;
}

void FxTool::setKnobControl(const QString& control)
{
    std::lock_guard<std::mutex> lock(specMutex_);
    spec_.knob = control;                            // not used by the audio: no new revision
}

void FxTool::setFaderControl(const QString& control)
{
    std::lock_guard<std::mutex> lock(specMutex_);
    spec_.fader = control;
}

void FxTool::settingsApplied(const ToolSettings& s)
{
    on_ = s.enabled;
}

void FxTool::extraToVariant(QVariantMap& m) const
{
    const Spec sp = spec();
    m["plugin"] = sp.plugin;
    QVariantMap values;
    for (auto it = sp.values.begin(); it != sp.values.end(); ++it)
        values[it.key()] = double(it.value());
    m["values"] = values;
    m["knob"] = sp.knob;
    m["fader"] = sp.fader;
}

void FxTool::extraFromVariant(const QVariantMap& m)
{
    Spec sp;
    sp.plugin = m.value("plugin").toString();
    sp.knob = m.value("knob").toString();
    sp.fader = m.value("fader").toString();
    const QVariantMap values = m.value("values").toMap();
    for (auto it = values.begin(); it != values.end(); ++it)
        sp.values[it.key()] = float(it.value().toDouble());
    {
        std::lock_guard<std::mutex> lock(specMutex_);
        spec_ = sp;
    }
    ++revision_;
}

} // namespace vdj
