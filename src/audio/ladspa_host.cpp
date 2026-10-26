#include "ladspa_host.h"

#include "ladspa/ladspa.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <map>
#include <mutex>

namespace vdj {

QStringList LadspaHost::searchPath()
{
    const QByteArray env = qgetenv("LADSPA_PATH");
    QStringList dirs;
    if (!env.isEmpty()) {
        dirs = QString::fromLocal8Bit(env).split(':', Qt::SkipEmptyParts);
    } else {
        dirs << QStringLiteral("/usr/lib/ladspa") << QStringLiteral("/usr/local/lib/ladspa")
             << QStringLiteral("/usr/lib/x86_64-linux-gnu/ladspa") << QDir::homePath() + QStringLiteral("/.ladspa");
    }
    if (QCoreApplication::instance())                // the plugins built with vdj-ui (<build>/src/ladspa)
        dirs << QCoreApplication::applicationDirPath() + QStringLiteral("/ladspa");
    return dirs;
}

namespace {

std::mutex g_mutex;
std::vector<LadspaPluginInfo> g_plugins;
bool g_scanned = false;
std::map<QString, void*> g_handles;          // dlopen'ed once and kept: instances run code from the library until the end

void* openLibrary(const QString& file)
{
    auto it = g_handles.find(file);
    if (it != g_handles.end())
        return it->second;
    void* h = dlopen(file.toLocal8Bit().constData(), RTLD_NOW | RTLD_LOCAL);
    g_handles[file] = h;
    return h;
}

const LADSPA_Descriptor* descriptorOf(const LadspaPluginInfo& info)
{
    void* h = openLibrary(info.file);
    if (!h)
        return nullptr;
    auto fn = reinterpret_cast<LADSPA_Descriptor_Function>(dlsym(h, "ladspa_descriptor"));
    return fn ? fn(info.index) : nullptr;
}

// The default of a control, from the hints (LADSPA 1.1 "default" rules)
float defaultOf(const LADSPA_PortRangeHint& h, float lo, float hi, bool log)
{
    const LADSPA_PortRangeHintDescriptor d = h.HintDescriptor;
    auto blend = [&](double t) -> float {
        if (log && lo > 0 && hi > 0)
            return float(std::exp(std::log(double(lo)) * (1.0 - t) + std::log(double(hi)) * t));
        return float(double(lo) * (1.0 - t) + double(hi) * t);
    };
    if (LADSPA_IS_HINT_HAS_DEFAULT(d)) {
        if (LADSPA_IS_HINT_DEFAULT_MINIMUM(d)) return lo;
        if (LADSPA_IS_HINT_DEFAULT_LOW(d)) return blend(0.25);
        if (LADSPA_IS_HINT_DEFAULT_MIDDLE(d)) return blend(0.5);
        if (LADSPA_IS_HINT_DEFAULT_HIGH(d)) return blend(0.75);
        if (LADSPA_IS_HINT_DEFAULT_MAXIMUM(d)) return hi;
        if (LADSPA_IS_HINT_DEFAULT_0(d)) return 0.0f;
        if (LADSPA_IS_HINT_DEFAULT_1(d)) return 1.0f;
        if (LADSPA_IS_HINT_DEFAULT_100(d)) return 100.0f;
        if (LADSPA_IS_HINT_DEFAULT_440(d)) return 440.0f;
    }
    return std::clamp(0.0f, lo, hi);
}

bool describe(const QString& file, unsigned long index, const LADSPA_Descriptor* d, LadspaPluginInfo* out)
{
    LadspaPluginInfo info;
    info.file = file;
    info.index = index;
    info.label = QString::fromLocal8Bit(d->Label);
    info.name = QString::fromLocal8Bit(d->Name);
    info.maker = QString::fromLocal8Bit(d->Maker ? d->Maker : "");
    info.key = QFileInfo(file).fileName() + QLatin1Char('|') + info.label;
    for (unsigned long p = 0; p < d->PortCount; ++p) {
        const LADSPA_PortDescriptor pd = d->PortDescriptors[p];
        if (LADSPA_IS_PORT_AUDIO(pd)) {
            (LADSPA_IS_PORT_INPUT(pd) ? info.audioIn : info.audioOut).push_back(int(p));
            continue;
        }
        if (!LADSPA_IS_PORT_CONTROL(pd) || !LADSPA_IS_PORT_INPUT(pd))
            continue;
        const LADSPA_PortRangeHint& h = d->PortRangeHints[p];
        LadspaControl c;
        c.port = int(p);
        c.name = QString::fromLocal8Bit(d->PortNames[p]);
        const double scale = LADSPA_IS_HINT_SAMPLE_RATE(h.HintDescriptor) ? double(LadspaInstance::kRate) : 1.0;
        c.lo = LADSPA_IS_HINT_BOUNDED_BELOW(h.HintDescriptor) ? float(h.LowerBound * scale) : 0.0f;
        c.hi = LADSPA_IS_HINT_BOUNDED_ABOVE(h.HintDescriptor) ? float(h.UpperBound * scale) : std::max(c.lo + 1.0f, 1.0f);
        if (c.hi < c.lo)
            std::swap(c.lo, c.hi);
        c.logarithmic = LADSPA_IS_HINT_LOGARITHMIC(h.HintDescriptor) && c.lo > 0.0f;
        c.integer = LADSPA_IS_HINT_INTEGER(h.HintDescriptor);
        c.toggled = LADSPA_IS_HINT_TOGGLED(h.HintDescriptor);
        if (c.toggled) {
            c.lo = 0.0f;
            c.hi = 1.0f;
        }
        c.def = std::clamp(defaultOf(h, c.lo, c.hi, c.logarithmic), c.lo, c.hi);
        info.controls.push_back(c);
    }
    const size_t in = info.audioIn.size(), outs = info.audioOut.size();
    if (in < 1 || in > 2 || outs < 1 || outs > 2)
        return false;                                // generators, splitters and multichannel plugins are not effects
    if (!d->instantiate || !d->connect_port || !d->run)
        return false;
    *out = info;
    return true;
}

void scanInto(const QStringList& path, std::vector<LadspaPluginInfo>* out)
{
    for (const QString& dir : path) {
        const QFileInfoList files = QDir(dir).entryInfoList(QStringList() << QStringLiteral("*.so"), QDir::Files, QDir::Name);
        for (const QFileInfo& fi : files) {
            void* h = openLibrary(fi.absoluteFilePath());
            if (!h)
                continue;
            auto fn = reinterpret_cast<LADSPA_Descriptor_Function>(dlsym(h, "ladspa_descriptor"));
            if (!fn)
                continue;
            for (unsigned long i = 0; const LADSPA_Descriptor* d = fn(i); ++i) {
                LadspaPluginInfo info;
                if (describe(fi.absoluteFilePath(), i, d, &info))
                    out->push_back(info);
            }
        }
    }
    std::sort(out->begin(), out->end(), [](const LadspaPluginInfo& a, const LadspaPluginInfo& b) {
        const int c = a.name.compare(b.name, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a.key < b.key;
    });
}

} // namespace

const std::vector<LadspaPluginInfo>& LadspaHost::plugins()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_scanned) {
        scanInto(searchPath(), &g_plugins);
        g_scanned = true;
    }
    return g_plugins;
}

void LadspaHost::rescan(const QStringList& path)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_plugins.clear();
    scanInto(path, &g_plugins);
    g_scanned = true;
}

bool LadspaHost::find(const QString& key, LadspaPluginInfo* out)
{
    for (const LadspaPluginInfo& p : plugins())
        if (p.key == key) {
            if (out) *out = p;
            return true;
        }
    return false;
}

// ---- instance ---------------------------------------------------------------------------------------------------

struct LadspaInstance::Impl {
    const LADSPA_Descriptor* desc = nullptr;
    std::vector<LADSPA_Handle> handles;        // 2 when a mono plugin runs once per channel, else 1
    bool perChannel = false;
    std::vector<float> controlValues;          // one per info.controls
    std::vector<float> outputControls;         // connected, never read: control outputs (latency ...)
    std::vector<float> inL, inR, outL, outR;   // kMaxFrames each
    std::vector<float> outL2, outR2;           // per channel mode: the second instance's buffers
};

std::unique_ptr<LadspaInstance> LadspaInstance::create(const LadspaPluginInfo& info, int rate)
{
    const LADSPA_Descriptor* d = descriptorOf(info);
    if (!d)
        return nullptr;
    std::unique_ptr<LadspaInstance> self(new LadspaInstance);
    self->info_ = info;
    auto impl = std::make_unique<Impl>();
    impl->desc = d;
    impl->perChannel = info.audioIn.size() == 1 && info.audioOut.size() == 1;
    const int copies = impl->perChannel ? 2 : 1;
    for (int i = 0; i < copies; ++i) {
        LADSPA_Handle h = d->instantiate(d, (unsigned long)rate);
        if (!h) {
            for (LADSPA_Handle done : impl->handles) {
                if (d->cleanup) d->cleanup(done);
            }
            return nullptr;
        }
        impl->handles.push_back(h);
    }
    impl->controlValues.resize(info.controls.size());
    for (size_t i = 0; i < info.controls.size(); ++i)
        impl->controlValues[i] = info.controls[i].def;
    impl->outputControls.assign(d->PortCount, 0.0f);
    for (auto* v : { &impl->inL, &impl->inR, &impl->outL, &impl->outR, &impl->outL2, &impl->outR2 })
        v->assign(size_t(kMaxFrames), 0.0f);
    for (size_t c = 0; c < impl->handles.size(); ++c) {
        LADSPA_Handle h = impl->handles[c];
        for (unsigned long p = 0; p < d->PortCount; ++p) {
            if (LADSPA_IS_PORT_CONTROL(d->PortDescriptors[p]) && LADSPA_IS_PORT_OUTPUT(d->PortDescriptors[p]))
                d->connect_port(h, p, &impl->outputControls[p]);
        }
        for (size_t i = 0; i < info.controls.size(); ++i)
            d->connect_port(h, (unsigned long)info.controls[i].port, &impl->controlValues[i]);
        if (impl->perChannel) {
            d->connect_port(h, (unsigned long)info.audioIn[0], c == 0 ? impl->inL.data() : impl->inR.data());
            d->connect_port(h, (unsigned long)info.audioOut[0], c == 0 ? impl->outL.data() : impl->outR.data());
        } else {
            d->connect_port(h, (unsigned long)info.audioIn[0], impl->inL.data());
            if (info.audioIn.size() > 1)
                d->connect_port(h, (unsigned long)info.audioIn[1], impl->inR.data());
            d->connect_port(h, (unsigned long)info.audioOut[0], impl->outL.data());
            if (info.audioOut.size() > 1)
                d->connect_port(h, (unsigned long)info.audioOut[1], impl->outR.data());
        }
        if (d->activate)
            d->activate(h);
    }
    self->impl_ = std::move(impl);
    return self;
}

LadspaInstance::~LadspaInstance()
{
    if (!impl_)
        return;
    for (LADSPA_Handle h : impl_->handles) {
        if (impl_->desc->deactivate) impl_->desc->deactivate(h);
        if (impl_->desc->cleanup) impl_->desc->cleanup(h);
    }
}

void LadspaInstance::setControl(int i, float v)
{
    if (i < 0 || size_t(i) >= info_.controls.size())
        return;
    const LadspaControl& c = info_.controls[size_t(i)];
    impl_->controlValues[size_t(i)] = std::clamp(v, c.lo, c.hi);
}

void LadspaInstance::run(int frames)
{
    for (LADSPA_Handle h : impl_->handles)
        impl_->desc->run(h, (unsigned long)frames);
}

void LadspaInstance::process(float* stereo, int frames)
{
    Impl& m = *impl_;
    for (int done = 0; done < frames;) {
        const int n = std::min(kMaxFrames, frames - done);
        float* io = stereo + size_t(done) * 2;
        const bool monoIn = info_.audioIn.size() == 1;
        for (int i = 0; i < n; ++i) {
            const float l = io[2 * i], r = io[2 * i + 1];
            m.inL[size_t(i)] = (monoIn && !m.perChannel) ? 0.5f * (l + r) : l;
            m.inR[size_t(i)] = r;
        }
        run(n);
        const bool stereoOut = info_.audioOut.size() == 2;
        for (int i = 0; i < n; ++i) {
            const float l = m.outL[size_t(i)];
            const float r = m.perChannel ? m.outR[size_t(i)] : stereoOut ? m.outR[size_t(i)] : l;
            io[2 * i] = l;
            io[2 * i + 1] = r;
        }
        done += n;
    }
}

float ladspaControlFromUnit(const LadspaControl& c, double unit)
{
    unit = std::clamp(unit, 0.0, 1.0);
    double v;
    if (c.logarithmic && c.lo > 0.0f)
        v = std::exp(std::log(double(c.lo)) + (std::log(double(c.hi)) - std::log(double(c.lo))) * unit);
    else
        v = double(c.lo) + (double(c.hi) - double(c.lo)) * unit;
    if (c.integer || c.toggled)
        v = std::round(v);
    return std::clamp(float(v), c.lo, c.hi);
}

double ladspaControlToUnit(const LadspaControl& c, float value)
{
    const double v = std::clamp(value, c.lo, c.hi);
    if (c.hi <= c.lo)
        return 0.0;
    if (c.logarithmic && c.lo > 0.0f)
        return (std::log(v) - std::log(double(c.lo))) / (std::log(double(c.hi)) - std::log(double(c.lo)));
    return (v - double(c.lo)) / (double(c.hi) - double(c.lo));
}

} // namespace vdj
