#ifndef LADSPA_HOST_H
#define LADSPA_HOST_H

#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

namespace vdj {

// A control input of a LADSPA plugin (a knob).
struct LadspaControl {
    int port = 0;                        // port index in the plugin
    QString name;
    float lo = 0.0f, hi = 1.0f, def = 0.0f;
    bool logarithmic = false;
    bool integer = false;
    bool toggled = false;
};

// A LADSPA plugin we can use: 1 or 2 audio inputs and 1 or 2 audio outputs (filters, reverbs, delays ...).
struct LadspaPluginInfo {
    QString key;                         // "<file name>|<label>": what a preset stores
    QString file;                        // absolute path of the .so
    QString label;
    QString name;
    QString maker;
    unsigned long index = 0;             // descriptor index in the file
    std::vector<int> audioIn, audioOut;  // port indices
    std::vector<LadspaControl> controls;
};

// Finds and runs LADSPA plugins (the standard Linux plugin format: swh, caps, lsp, Ma* ... are installed in /usr/lib/ladspa).
class LadspaHost {
public:
    // $LADSPA_PATH, else /usr/lib/ladspa, /usr/local/lib/ladspa and ~/.ladspa
    static QStringList searchPath();
    // Every usable plugin found, sorted by name (a file that does not load is skipped). Scans once; rescan() to look again.
    static const std::vector<LadspaPluginInfo>& plugins();
    static void rescan(const QStringList& path = searchPath());
    static bool find(const QString& key, LadspaPluginInfo* out);
};

// One running copy of a plugin for one audio output: stereo float frames in, stereo float frames out. Not thread safe: one
// thread calls process() and setControl().
class LadspaInstance {
public:
    static constexpr int kRate = 44100;
    static constexpr int kMaxFrames = 4096;     // process() works in pieces of this size

    static std::unique_ptr<LadspaInstance> create(const LadspaPluginInfo& info, int rate = kRate);
    ~LadspaInstance();
    LadspaInstance(const LadspaInstance&) = delete;
    LadspaInstance& operator=(const LadspaInstance&) = delete;

    const LadspaPluginInfo& info() const { return info_; }
    void setControl(int controlIndex, float value);        // index in info().controls, clamped to its range
    // In place on interleaved stereo.
    void process(float* stereo, int frames);

private:
    LadspaInstance() = default;
    void run(int frames);

    LadspaPluginInfo info_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The value of a control on a 0..1 knob position (log scale for frequencies ...), and back.
float ladspaControlFromUnit(const LadspaControl& c, double unit);
double ladspaControlToUnit(const LadspaControl& c, float value);

} // namespace vdj

#endif // LADSPA_HOST_H
