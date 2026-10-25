#include "drum_pattern.h"

#include <algorithm>
#include <cstdlib>

namespace vdj {

const char* const kDrumVoiceNames[kDrumVoices] = { "BD", "SD", "SY", "CL", "HH", "OH" };

int drumChokeClamp(int steps)
{
    int best = kDrumChokeValues[0];
    for (int v : kDrumChokeValues)
        if (std::abs(v - steps) < std::abs(best - steps))
            best = v;                                // strict <: a tie keeps the lower value
    return best;
}

int drumChokeNext(int current, int dir)
{
    const int cur = drumChokeClamp(current);
    int i = 0;
    while (i < 5 && kDrumChokeValues[i] != cur)
        ++i;
    if (dir > 0)
        i = std::min(i + 1, 5);
    else if (dir < 0)
        i = std::max(i - 1, 0);
    return kDrumChokeValues[i];
}

bool DrumVoice::operator==(const DrumVoice& o) const
{
    if (samplePath != o.samplePath || volume != o.volume || offsetMs != o.offsetMs || chokeSteps != o.chokeSteps || muted != o.muted)
        return false;
    return std::equal(steps, steps + kDrumSteps, o.steps);
}

QString drumPatternToString(const quint8 steps[kDrumSteps])
{
    QString s;
    for (int i = 0; i < kDrumSteps; ++i)
        s += QLatin1Char(char('0' + std::min<int>(steps[i], StepHalf)));
    return s;
}

bool drumPatternFromString(const QString& text, quint8 out[kDrumSteps])
{
    if (text.size() != kDrumSteps)
        return false;
    quint8 tmp[kDrumSteps];
    for (int i = 0; i < kDrumSteps; ++i) {
        const QChar c = text[i];
        if (c < QLatin1Char('0') || c > QLatin1Char('2'))
            return false;
        tmp[i] = quint8(c.unicode() - '0');
    }
    std::copy(tmp, tmp + kDrumSteps, out);
    return true;
}

QVariantMap drumVoiceToVariant(const DrumVoice& v, bool withMute)
{
    QVariantMap m;
    m["sample"] = v.samplePath;
    m["volume"] = v.volume;
    m["offsetMs"] = v.offsetMs;
    m["choke"] = v.chokeSteps;
    m["pattern"] = drumPatternToString(v.steps);
    if (withMute && v.muted)
        m["muted"] = true;
    return m;
}

void drumVoiceFromVariant(const QVariantMap& m, DrumVoice* v)
{
    v->samplePath = m.value("sample", v->samplePath).toString();
    v->volume = m.value("volume", v->volume).toInt();
    v->offsetMs = m.value("offsetMs", v->offsetMs).toDouble();
    v->chokeSteps = m.value("choke", v->chokeSteps).toInt();
    v->muted = m.value("muted", v->muted).toBool();
    drumPatternFromString(m.value("pattern").toString(), v->steps);
}

float drumStepLevel(quint8 step)
{
    switch (step) {
    case StepOn: return 1.0f;
    case StepHalf: return 0.5f;
    default: return 0.0f;
    }
}

double drumStepBeat(int step)
{
    return double(step) / 4.0;
}

} // namespace vdj
