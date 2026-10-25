#include "sampler_module.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace vdj {

static std::atomic<int> g_nextId{ 1 };

SamplerModule::SamplerModule()
    : id_(g_nextId++)
{
}

ModuleSettings SamplerModule::settings() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return s_;
}

void SamplerModule::setSettings(const ModuleSettings& in)
{
    ModuleSettings s = in;
    s.offsetMs = std::clamp(s.offsetMs, -kMaxOffsetMs, kMaxOffsetMs);
    s.midiChannel = std::clamp(s.midiChannel, 1, 16);
    s.midiNote = std::clamp(s.midiNote, 0, 127);
    s.midiVelocity = std::clamp(s.midiVelocity, 1, 127);
    s.gain = std::clamp(s.gain, 0.0f, 4.0f);
    s.volume = std::clamp(s.volume, 0, kMaxVolume);
    s.beatMask &= 0x0F;
    std::lock_guard<std::mutex> lock(mutex_);
    s_ = s;
}

void SamplerModule::plan(double fromBeat, double toBeat, std::vector<PlannedHit>& out)
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

void SamplerModule::collectHits(double lo, double hi, const ModuleSettings& s, std::vector<PlannedHit>& out) const
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

void Dembomatik::collectHits(double lo, double hi, const ModuleSettings&, std::vector<PlannedHit>& out) const
{
    // a step is a quarter of a beat; the bar is 4 beats = 16 steps
    for (long k = long(std::ceil(lo * 4.0 - 1e-9)); double(k) / 4.0 < hi - 1e-12; ++k)
        if (stepIsOn(int(k % kSteps)))
            out.push_back(PlannedHit{ double(k) / 4.0 });
}

void SamplerModule::fireOneBar(double nowBeat)
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

void SamplerModule::finishOneBar(double nowBeat)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (oneBarStart_ >= 0.0 && nowBeat >= oneBarStart_ + 4.0)
        oneBarStart_ = -1.0;
}

void SamplerModule::cancelOneBar()
{
    std::lock_guard<std::mutex> lock(mutex_);
    oneBarStart_ = -1.0;
}

OneBarState SamplerModule::oneBarState(double nowBeat) const
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

QString SamplerModule::typeKey(Type t)
{
    switch (t) {
    case Bombomatic: return QStringLiteral("bombomatic");
    case StabPlayer: return QStringLiteral("stab");
    case AcidLoop: return QStringLiteral("acid");
    case OneShot: return QStringLiteral("oneshot");
    case MidiMatic: return QStringLiteral("midi");
    case Dembomatik: return QStringLiteral("dembow");
    }
    return QString();
}

std::shared_ptr<SamplerModule> SamplerModule::create(Type type)
{
    std::shared_ptr<SamplerModule> m;
    switch (type) {
    case Bombomatic: m = std::make_shared<vdj::Bombomatic>(); break;
    case StabPlayer: m = std::make_shared<vdj::StabPlayer>(); break;
    case AcidLoop: m = std::make_shared<vdj::AcidLoop>(); break;
    case OneShot: m = std::make_shared<vdj::OneShot>(); break;
    case MidiMatic: m = std::make_shared<vdj::MidiMatic>(); break;
    case Dembomatik: m = std::make_shared<vdj::Dembomatik>(); break;
    }
    ModuleSettings s;
    s.name = m->typeName();
    m->setSettings(s);
    return m;
}

QVariantMap SamplerModule::toVariant() const
{
    const ModuleSettings s = settings();
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
    return m;
}

std::shared_ptr<SamplerModule> SamplerModule::fromVariant(const QVariantMap& m)
{
    const QString key = m.value("type").toString();
    for (Type t : { Bombomatic, StabPlayer, AcidLoop, OneShot, MidiMatic, Dembomatik }) {
        if (typeKey(t) != key)
            continue;
        auto mod = create(t);
        if (!mod)
            return nullptr;
        ModuleSettings s = mod->settings();           // defaults for keys a preset does not have
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
        mod->setSettings(s);
        return mod;
    }
    return nullptr;
}

} // namespace vdj
