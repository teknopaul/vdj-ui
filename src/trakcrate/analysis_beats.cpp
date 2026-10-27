#include "analysis_beats.h"

#include <algorithm>
#include <cmath>

namespace trakcrate {

namespace {

constexpr int kHop = 110;                                  // samples per onset frame: 2.5 ms, 401 frames per second

// Onset strength per frame: positive rises of the log energy of a low band and of a high-passed signal (weighted 0.3), each scaled by its own
// mean, minus a moving average. Empty when the audio is silent.
std::vector<double> onsetEnvelope(const DecodedPcm& pcm, double* fps)
{
    const int sr = pcm.sampleRate;
    *fps = double(sr) / kHop;
    const size_t frames = pcm.frames();
    const size_t n = frames / kHop;
    std::vector<double> lowE(n, 0.0), hiE(n, 0.0);
    const double a = 1.0 - std::exp(-2.0 * M_PI * 200.0 / sr);
    double y1 = 0, y2 = 0, prev = 0;
    for (size_t b = 0; b < n; ++b) {
        double sl = 0, sh = 0;
        for (int i = 0; i < kHop; ++i) {
            size_t f = b * kHop + size_t(i);
            double x = 0.5 * (double(pcm.stereo[2 * f]) + double(pcm.stereo[2 * f + 1]));
            y1 += a * (x - y1);
            y2 += a * (y1 - y2);
            double h = x - prev;
            prev = x;
            sl += y2 * y2;
            sh += h * h;
        }
        lowE[b] = std::log(1.0 + 100.0 * std::sqrt(sl / kHop));
        hiE[b] = std::log(1.0 + 100.0 * std::sqrt(sh / kHop));
    }
    std::vector<double> o(n, 0.0), oh(n, 0.0);
    double sumL = 0, sumH = 0;
    for (size_t b = 1; b < n; ++b) {
        o[b] = std::max(0.0, lowE[b] - lowE[b - 1]);
        oh[b] = std::max(0.0, hiE[b] - hiE[b - 1]);
        sumL += o[b];
        sumH += oh[b];
    }
    if (n < 2 || (sumL < 1e-6 && sumH < 1e-6)) return {};
    const double mL = sumL / double(n), mH = sumH / double(n);
    for (size_t b = 0; b < n; ++b) o[b] = (mL > 1e-9 ? o[b] / mL : 0.0) + 0.3 * (mH > 1e-9 ? oh[b] / mH : 0.0);   // kicks (low band) outweigh hats when both fit
    // subtract a moving average (about +-0.25 s) so only peaks above their surroundings remain
    std::vector<double> prefix(n + 1, 0.0);
    for (size_t b = 0; b < n; ++b) prefix[b + 1] = prefix[b] + o[b];
    const long w = long(*fps * 0.25);
    std::vector<double> r(n);
    for (size_t b = 0; b < n; ++b) {
        long lo = std::max<long>(0, long(b) - w), hi = std::min<long>(long(n), long(b) + w + 1);
        double mean = (prefix[size_t(hi)] - prefix[size_t(lo)]) / double(hi - lo);
        r[b] = std::max(0.0, o[b] - mean);
    }
    return r;
}

double tempoWeight(double bpm, double prior)
{
    if (prior > 0) {
        double d = std::log2(bpm / prior);
        return std::exp(-0.5 * (d / 0.2) * (d / 0.2));
    }
    double d = std::log2(bpm / 125.0);
    return std::exp(-0.5 * (d / 0.7) * (d / 0.7));
}

// Folds the onsets modulo `period` frames; returns the best 3 bin sum and where it is. total receives the sum of all onsets.
double foldScore(const std::vector<double>& o, double period, int bins, std::vector<double>* hist, int* bestBin, double* total)
{
    hist->assign(size_t(bins), 0.0);
    double tot = 0;
    const double scale = double(bins) / period;
    for (size_t i = 0; i < o.size(); ++i) {
        if (o[i] <= 0) continue;
        double ph = std::fmod(double(i), period) * scale;
        (*hist)[size_t(ph) % size_t(bins)] += o[i];
        tot += o[i];
    }
    double best = -1;
    int bb = 0;
    for (int b = 0; b < bins; ++b) {
        double s = (*hist)[size_t((b + bins - 1) % bins)] + (*hist)[size_t(b)] + (*hist)[size_t((b + 1) % bins)];
        if (s > best) { best = s; bb = b; }
    }
    if (bestBin) *bestBin = bb;
    if (total) *total = tot;
    return best;
}

} // namespace

BeatAnalysis analyseBeats(const DecodedPcm& pcm, double priorBpm, double minBpm, double maxBpm)
{
    BeatAnalysis res;
    double fps = 0;
    if (pcm.seconds() < 4.0) return res;
    std::vector<double> o = onsetEnvelope(pcm, &fps);
    if (o.empty()) return res;
    const size_t n = o.size();

    // ---- coarse tempo: autocorrelation of the onsets, with a tempo prior to pick the octave
    if (priorBpm > 0) { minBpm = std::min(minBpm, priorBpm * 0.8); maxBpm = std::max(maxBpm, priorBpm * 1.2); }
    const long lmin = long(fps * 60.0 / maxBpm), lmax = long(fps * 60.0 / minBpm);
    if (long(n) < lmax * 4) return res;
    std::vector<double> ac(size_t(lmax) * 2 + 2, 0.0);
    for (long lag = lmin; lag <= lmax * 2; ++lag) {
        double s = 0;
        for (size_t i = 0; i + size_t(lag) < n; ++i) s += o[i] * o[i + size_t(lag)];
        ac[size_t(lag)] = s / double(n - size_t(lag));
    }
    long bestLag = 0;
    double bestScore = -1;
    for (long lag = lmin; lag <= lmax; ++lag) {
        double bpm = fps * 60.0 / double(lag);
        double s = (ac[size_t(lag)] + 0.5 * ac[size_t(lag) * 2]) * tempoWeight(bpm, priorBpm);
        if (s > bestScore) { bestScore = s; bestLag = lag; }
    }
    if (bestLag == 0 || bestScore <= 0) return res;
    double coarseLag = double(bestLag);
    if (bestLag > lmin && bestLag < lmax) {                  // parabolic interpolation of the peak
        double l = ac[size_t(bestLag) - 1], c = ac[size_t(bestLag)], r = ac[size_t(bestLag) + 1];
        double den = l - 2 * c + r;
        if (std::fabs(den) > 1e-12) coarseLag += 0.5 * (l - r) / den;
    }

    // ---- fine period and phase: fold at many candidate periods around the coarse one and keep the most coherent
    const int bins = 64;
    std::vector<double> hist;
    double bestFold = -1, bestPeriod = coarseLag;
    int bestBin = 0;
    const double coarseBpm = fps * 60.0 / coarseLag;
    for (double bpm = coarseBpm * 0.97; bpm <= coarseBpm * 1.03; bpm += 0.02) {
        int bb = 0;
        double total = 0;
        double s = foldScore(o, fps * 60.0 / bpm, bins, &hist, &bb, &total);
        if (s > bestFold) { bestFold = s; bestPeriod = fps * 60.0 / bpm; bestBin = bb; }
    }
    double total = 0;
    foldScore(o, bestPeriod, bins, &hist, &bestBin, &total);
    if (total <= 0) return res;
    const double mean3 = 3.0 * total / bins;
    const double ratio = bestFold / mean3;
    res.confidence = ratio > 1.0 ? 1.0 - 1.0 / ratio : 0.0;
    if (res.confidence < BeatAnalysis::kMinConfidence) return res;

    // ---- least squares fit through the onset peaks near the predicted beats
    double phase = (double(bestBin) + 0.5) / bins * bestPeriod;           // frame offset of the best bin
    double a = phase, b = bestPeriod;                                    // position(k) = a + b k
    for (int pass = 0; pass < 4; ++pass) {
        std::vector<double> ks, ps, ws;
        const double win = std::max(2.0, 0.12 * b);
        double peakSum = 0;
        std::vector<std::pair<long, double>> picks;                      // (k, position)
        std::vector<double> strengths;
        for (long k = 0;; ++k) {
            double pred = a + b * double(k);
            if (pred + win >= double(n)) break;
            if (pred - win < 1) continue;
            long lo = long(pred - win), hi = long(pred + win);
            long arg = lo;
            for (long i = lo; i <= hi; ++i) if (o[size_t(i)] > o[size_t(arg)]) arg = i;
            if (o[size_t(arg)] <= 0) continue;
            double pos = double(arg);
            if (arg > 0 && size_t(arg) + 1 < n) {
                double l = o[size_t(arg) - 1], c = o[size_t(arg)], r = o[size_t(arg) + 1];
                double den = l - 2 * c + r;
                if (den < -1e-12) pos += std::max(-0.5, std::min(0.5, 0.5 * (l - r) / den));
            }
            picks.emplace_back(k, pos);
            strengths.push_back(o[size_t(arg)]);
            peakSum += o[size_t(arg)];
        }
        if (picks.size() < 8) break;
        std::vector<double> sorted = strengths;
        std::sort(sorted.begin(), sorted.end());
        const double median = sorted[sorted.size() / 2];
        double sw = 0, sk = 0, sp = 0, skk = 0, skp = 0;
        int used = 0;
        for (size_t i = 0; i < picks.size(); ++i) {
            if (strengths[i] < 0.25 * median) continue;                  // not a real beat
            double k = double(picks[i].first), p = picks[i].second, w = strengths[i];
            if (pass > 0 && std::fabs(p - (a + b * k)) > 0.03 * b + 1.0) continue;     // outlier
            sw += w; sk += w * k; sp += w * p; skk += w * k * k; skp += w * k * p;
            ++used;
        }
        double den = sw * skk - sk * sk;
        if (used < 8 || std::fabs(den) < 1e-9) break;
        b = (sw * skp - sk * sp) / den;
        a = (sp - b * sk) / sw;
        res.beatsFitted = used;
    }
    if (b <= 0) return res;

    const double hopSec = double(kHop) / pcm.sampleRate;
    double periodSec = b * hopSec;
    double t0 = (a + 0.5) * hopSec;                                     // a frame covers [i, i+1) hops: its middle is the click
    t0 -= std::floor(t0 / periodSec) * periodSec;                       // the first grid point at or after the start
    res.periodSec = periodSec;
    res.grid.bpm = std::round(60.0 / periodSec * 100.0) / 100.0;
    res.grid.firstBeatUs = (long long)std::llround(t0 * 1e6);
    return res;
}

} // namespace trakcrate
