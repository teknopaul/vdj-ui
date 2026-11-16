#include "ladspa_effect.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <dlfcn.h>

namespace vdj {

namespace {

float defaultFor(const LADSPA_PortRangeHint& h)
{
    const int d = h.HintDescriptor & LADSPA_HINT_DEFAULT_MASK;
    const float lo = h.LowerBound, hi = h.UpperBound;
    const bool log = (h.HintDescriptor & LADSPA_HINT_LOGARITHMIC) != 0 && lo > 0 && hi > 0;
    auto at = [&](float f) { return log ? std::exp(std::log(lo) * (1 - f) + std::log(hi) * f) : lo * (1 - f) + hi * f; };
    switch (d) {
    case LADSPA_HINT_DEFAULT_MINIMUM: return lo;
    case LADSPA_HINT_DEFAULT_LOW:     return at(0.25f);
    case LADSPA_HINT_DEFAULT_MIDDLE:  return at(0.5f);
    case LADSPA_HINT_DEFAULT_HIGH:    return at(0.75f);
    case LADSPA_HINT_DEFAULT_MAXIMUM: return hi;
    case LADSPA_HINT_DEFAULT_0:       return 0.f;
    case LADSPA_HINT_DEFAULT_1:       return 1.f;
    case LADSPA_HINT_DEFAULT_100:     return 100.f;
    case LADSPA_HINT_DEFAULT_440:     return 440.f;
    default:
        if ((h.HintDescriptor & LADSPA_HINT_BOUNDED_BELOW) && (h.HintDescriptor & LADSPA_HINT_BOUNDED_ABOVE)) return at(0.5f);
        if (h.HintDescriptor & LADSPA_HINT_BOUNDED_BELOW) return lo;
        return 0.f;
    }
}

QStringList searchDirs(const QString& override)
{
    QString p = override;
    if (p.isEmpty()) p = QString::fromLocal8Bit(qgetenv("LADSPA_PATH"));
    if (p.isEmpty()) p = QStringLiteral("/usr/lib/ladspa:/usr/local/lib/ladspa");
    return p.split(QLatin1Char(':'), Qt::SkipEmptyParts);
}

} // namespace

LadspaEffect::~LadspaEffect()
{
    for (int i = 0; i < instances_; ++i) {
        if (!handle_[i] || !desc_) continue;
        if (desc_->deactivate) desc_->deactivate(handle_[i]);
        if (desc_->cleanup) desc_->cleanup(handle_[i]);
    }
    if (lib_) dlclose(lib_);
}

bool LadspaEffect::load(const QString& libPath, const QString& label, unsigned long rate)
{
    void* lib = dlopen(libPath.toLocal8Bit().constData(), RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
    auto fn = reinterpret_cast<LADSPA_Descriptor_Function>(dlsym(lib, "ladspa_descriptor"));
    const LADSPA_Descriptor* d = nullptr;
    for (unsigned long i = 0; fn && (d = fn(i)); ++i)
        if (label == QLatin1String(d->Label)) break;
    if (!d || !d->instantiate || !d->connect_port || !d->run) { dlclose(lib); return false; }

    // audio ports: the first two inputs and the first two outputs (one of each = a mono plugin, run once per channel)
    long in[2] = {-1, -1}, out[2] = {-1, -1};
    int nIn = 0, nOut = 0;
    for (unsigned long p = 0; p < d->PortCount; ++p) {
        const int pd = d->PortDescriptors[p];
        if (!(pd & LADSPA_PORT_AUDIO)) continue;
        if ((pd & LADSPA_PORT_INPUT) && nIn < 2) in[nIn++] = long(p);
        if ((pd & LADSPA_PORT_OUTPUT) && nOut < 2) out[nOut++] = long(p);
    }
    if (nIn == 0 || nIn != nOut) { dlclose(lib); return false; }

    lib_ = lib;
    desc_ = d;
    label_ = label;
    instances_ = nIn == 2 ? 1 : 2;
    for (int i = 0; i < 2; ++i) { portIn_[i] = in[nIn == 2 ? i : 0]; portOut_[i] = out[nIn == 2 ? i : 0]; }
    controls_.assign(d->PortCount * 2, 0.f);              // instance 1 (mono plugins) uses the second half
    for (int inst = 0; inst < instances_; ++inst) {
        handle_[inst] = d->instantiate(d, rate);
        if (!handle_[inst]) { instances_ = inst; return false; }       // the destructor releases what exists
        for (unsigned long p = 0; p < d->PortCount; ++p) {
            if (!(d->PortDescriptors[p] & LADSPA_PORT_CONTROL)) continue;
            float& c = controls_[p + (inst ? d->PortCount : 0)];
            c = (d->PortDescriptors[p] & LADSPA_PORT_INPUT) ? defaultFor(d->PortRangeHints[p]) : 0.f;
            d->connect_port(handle_[inst], p, &c);
        }
        if (d->activate) d->activate(handle_[inst]);
    }
    return true;
}

bool LadspaEffect::setControl(const QString& part, float value)
{
    if (!desc_) return false;
    for (unsigned long p = 0; p < desc_->PortCount; ++p) {
        const int pd = desc_->PortDescriptors[p];
        if (!(pd & LADSPA_PORT_CONTROL) || !(pd & LADSPA_PORT_INPUT)) continue;
        if (!QString::fromLatin1(desc_->PortNames[p]).contains(part, Qt::CaseInsensitive)) continue;
        controls_[p] = value;
        controls_[p + desc_->PortCount] = value;
        return true;
    }
    return false;
}

void LadspaEffect::process(const int16_t* in, int16_t* out, size_t frames)
{
    if (!desc_ || frames == 0) return;
    inL_.resize(frames); inR_.resize(frames); outL_.assign(frames, 0.f); outR_.assign(frames, 0.f);
    for (size_t i = 0; i < frames; ++i) { inL_[i] = in[2 * i] / 32768.f; inR_[i] = in[2 * i + 1] / 32768.f; }
    if (instances_ == 1) {
        desc_->connect_port(handle_[0], portIn_[0], inL_.data());
        desc_->connect_port(handle_[0], portIn_[1], inR_.data());
        desc_->connect_port(handle_[0], portOut_[0], outL_.data());
        desc_->connect_port(handle_[0], portOut_[1], outR_.data());
        desc_->run(handle_[0], frames);
    } else {
        desc_->connect_port(handle_[0], portIn_[0], inL_.data());
        desc_->connect_port(handle_[0], portOut_[0], outL_.data());
        desc_->run(handle_[0], frames);
        desc_->connect_port(handle_[1], portIn_[0], inR_.data());
        desc_->connect_port(handle_[1], portOut_[0], outR_.data());
        desc_->run(handle_[1], frames);
    }
    auto toS16 = [](float v) { return int16_t(std::lrintf(std::max(-1.f, std::min(1.f, v)) * 32767.f)); };
    for (size_t i = 0; i < frames; ++i) { out[2 * i] = toS16(outL_[i]); out[2 * i + 1] = toS16(outR_[i]); }
}

QStringList LadspaEffect::compressorLabels()
{
    return {QStringLiteral("CompressX2"), QStringLiteral("sc4_1882"), QStringLiteral("dysonCompress_1403"), QStringLiteral("Compress")};
}

std::unique_ptr<LadspaEffect> LadspaEffect::create(const QStringList& labels, unsigned long sampleRate, const QString& searchPath, QString* error)
{
    for (const QString& label : labels) {
        for (const QString& dir : searchDirs(searchPath)) {
            const auto files = QDir(dir).entryList({QStringLiteral("*.so")}, QDir::Files);
            for (const QString& f : files) {
                std::unique_ptr<LadspaEffect> e(new LadspaEffect);
                if (e->load(QDir(dir).filePath(f), label, sampleRate)) return e;
            }
        }
    }
    if (error) *error = QStringLiteral("no LADSPA plugin found for %1 (looked in %2)").arg(labels.join(QLatin1Char('/')), searchDirs(searchPath).join(QLatin1Char(':')));
    return nullptr;
}

std::unique_ptr<LadspaEffect> LadspaEffect::createCompressor(unsigned long sampleRate, QString* error)
{
    return create(compressorLabels(), sampleRate, QString(), error);
}

bool LadspaEffect::compressorAvailable()
{
    static const bool ok = createCompressor(44100) != nullptr;
    return ok;
}

} // namespace vdj
