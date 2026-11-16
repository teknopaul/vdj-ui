#ifndef LADSPA_EFFECT_H
#define LADSPA_EFFECT_H

#include <QString>
#include <QStringList>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "ladspa_min.h"

namespace vdj {

// A minimal in-process LADSPA host for one stereo effect: the optional compressor of the recording bar. It looks in LADSPA_PATH (default
// /usr/lib/ladspa:/usr/local/lib/ladspa) for a compressor by label, in order: CompressX2 (caps, stereo), sc4_1882 (swh, stereo),
// dysonCompress_1403 and Compress (mono: two instances). All control ports keep the plugin's default value (the hint's default, or the
// middle of the range); setControl() changes one by (part of) its name.
class LadspaEffect {
public:
    ~LadspaEffect();
    LadspaEffect(const LadspaEffect&) = delete;
    LadspaEffect& operator=(const LadspaEffect&) = delete;

    // The first plugin of `labels` that can be loaded, or nullptr. `libraryDir` overrides LADSPA_PATH (tests).
    static std::unique_ptr<LadspaEffect> create(const QStringList& labels, unsigned long sampleRate, const QString& searchPath = QString(), QString* error = nullptr);
    static QStringList compressorLabels();
    static std::unique_ptr<LadspaEffect> createCompressor(unsigned long sampleRate, QString* error = nullptr);
    // True when createCompressor() would find a plugin (cached after the first call).
    static bool compressorAvailable();

    QString label() const { return label_; }
    bool stereoNative() const { return instances_ == 1; }
    // Sets the first control input port whose name contains `part` (case insensitive); false when there is none.
    bool setControl(const QString& part, float value);
    // Interleaved signed 16 bit stereo, `frames` frames, in and out may be the same buffer.
    void process(const int16_t* in, int16_t* out, size_t frames);

private:
    LadspaEffect() = default;
    bool load(const QString& libPath, const QString& label, unsigned long rate);

    void* lib_ = nullptr;
    const LADSPA_Descriptor* desc_ = nullptr;
    LADSPA_Handle handle_[2] = {nullptr, nullptr};
    int instances_ = 0;
    QString label_;
    std::vector<float> controls_;                    // one slot per port (control ports use theirs)
    std::vector<float> inL_, inR_, outL_, outR_;
    long portIn_[2] = {-1, -1}, portOut_[2] = {-1, -1};
};

} // namespace vdj
#endif
