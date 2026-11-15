#include "mix_waveform.h"

#include <algorithm>
#include <cmath>

namespace vdj {

QVector<quint16> editedColorWaveform(const QVector<quint16>& color, const trakcrate::EditPlan& plan, double ratio, long long stretchedFrames)
{
    constexpr double kCols = 150.0;
    const int rate = trakcrate::kEditRate;
    trakcrate::EditPlan p = plan;
    if (p.segments.empty()) p.segments.push_back({0, stretchedFrames});
    QVector<quint16> out;
    if (color.isEmpty() || ratio <= 0) return out;
    for (const trakcrate::EditSegment& s : p.segments) {
        // output columns of this segment; each takes the loudest source column of the span it covers
        const long long c0 = (long long)std::llround(double(s.start) / rate * kCols), c1 = (long long)std::llround(double(s.end) / rate * kCols);
        for (long long c = c0; c < c1; ++c) {
            const double a = double(c) * ratio, b = double(c + 1) * ratio;           // stretched column -> original column
            const int ia = int(a), ib = std::max(ia + 1, int(b));
            quint16 best = 0;
            int bestH = -1;
            for (int i = ia; i < ib && i < color.size(); ++i) {
                if (i < 0) continue;
                const int h = (color[i] >> 2) & 31;
                if (h > bestH) { bestH = h; best = color[i]; }
            }
            out.push_back(best);
        }
    }
    return out;
}

} // namespace vdj
