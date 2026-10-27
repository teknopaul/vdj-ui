#include "cuemark_render.h"

#include <algorithm>
#include <cmath>

namespace trakcrate {

size_t detailColumn(double ms, size_t columns)
{
    if (columns == 0) return 0;
    const double c = std::floor(std::max(0.0, ms) * Waveform::kColumnsPerSecond / 1000.0 + 0.5);
    return size_t(std::min<double>(c, double(columns - 1)));
}

size_t previewColumn(double ms, double durationMs, size_t columns)
{
    if (columns == 0) return 0;
    if (durationMs <= 0) return 0;
    const double c = std::floor(std::max(0.0, ms) / durationMs * double(columns));
    return size_t(std::min<double>(c, double(columns - 1)));
}

Waveform renderCuemarks(const Waveform& w, const std::vector<Cuemark>& marks, double durationMs, const CuemarkRenderOptions& o)
{
    Waveform out = w;
    if (!o.enabled) return out;
    const size_t width = size_t(std::max(1, o.widthColumns));
    for (const Cuemark& m : marks) {
        if (m.hot >= 'A' && m.hot <= 'C' && !o.drawHot) continue;
        if (!out.pwv3.empty()) {
            size_t c = detailColumn(m.ms, out.pwv3.size());
            if (c + width > out.pwv3.size()) c = out.pwv3.size() > width ? out.pwv3.size() - width : 0;
            for (size_t k = c; k < c + width && k < out.pwv3.size(); ++k) out.pwv3[k] = kSpikeMono;
        }
        if (!out.pwv5.empty()) {
            size_t c = detailColumn(m.ms, out.pwv5.size());
            if (c + width > out.pwv5.size()) c = out.pwv5.size() > width ? out.pwv5.size() - width : 0;
            for (size_t k = c; k < c + width && k < out.pwv5.size(); ++k) out.pwv5[k] = o.green ? kSpikeGreen5 : kSpikeWhite5;
        }
        if (!out.pwav.empty()) out.pwav[previewColumn(m.ms, durationMs, out.pwav.size())] = kSpikeMono;
        if (!out.pwv2.empty()) out.pwv2[previewColumn(m.ms, durationMs, out.pwv2.size())] = kSpikeTiny;
    }
    return out;
}

} // namespace trakcrate
