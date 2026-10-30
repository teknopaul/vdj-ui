#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "beats.h"

//SNIP_beats

#define HOP 110     /* samples per onset frame: 2.5 ms, 401 frames per second */

/*
 * Onset strength per frame: positive rises of the log energy of a low band and of a high-passed signal (weighted 0.3),
 * each scaled by its own mean, minus a moving average. Returns the number of frames in *n_out; NULL (with *n_out 0)
 * when the audio is silent or on out of memory (*oom set).
 */
static double *onset_envelope(const struct tc_pcm *pcm, double *fps, size_t *n_out, int *oom)
{
    const int sr = pcm->sample_rate;
    const size_t n = pcm->frames / HOP;
    double *low_e, *hi_e, *o, *oh, *prefix, *r;
    const double a = 1.0 - exp(-2.0 * M_PI * 200.0 / sr);
    double y1 = 0, y2 = 0, prev = 0, sum_l = 0, sum_h = 0, mean_l, mean_h;
    size_t b, i;
    long w;

    *fps = (double)sr / HOP;
    *n_out = 0;
    *oom = 0;
    if (n < 2) {
        return NULL;
    }
    low_e = calloc(n, sizeof *low_e);
    hi_e = calloc(n, sizeof *hi_e);
    o = calloc(n, sizeof *o);
    oh = calloc(n, sizeof *oh);
    prefix = calloc(n + 1, sizeof *prefix);
    r = calloc(n, sizeof *r);
    if (!low_e || !hi_e || !o || !oh || !prefix || !r) {
        *oom = 1;
        goto fail;
    }
    for (b = 0; b < n; b++) {
        double sl = 0, sh = 0;
        for (i = 0; i < HOP; i++) {
            size_t f = b * HOP + i;
            double x = 0.5 * ((double)pcm->stereo[2 * f] + (double)pcm->stereo[2 * f + 1]);
            double h;
            y1 += a * (x - y1);
            y2 += a * (y1 - y2);
            h = x - prev;
            prev = x;
            sl += y2 * y2;
            sh += h * h;
        }
        low_e[b] = log(1.0 + 100.0 * sqrt(sl / HOP));
        hi_e[b] = log(1.0 + 100.0 * sqrt(sh / HOP));
    }
    for (b = 1; b < n; b++) {
        o[b] = fmax(0.0, low_e[b] - low_e[b - 1]);
        oh[b] = fmax(0.0, hi_e[b] - hi_e[b - 1]);
        sum_l += o[b];
        sum_h += oh[b];
    }
    if (sum_l < 1e-6 && sum_h < 1e-6) {
        goto fail;
    }
    mean_l = sum_l / (double)n;
    mean_h = sum_h / (double)n;
    for (b = 0; b < n; b++) {
        /* kicks (low band) outweigh hats when both fit */
        o[b] = (mean_l > 1e-9 ? o[b] / mean_l : 0.0) + 0.3 * (mean_h > 1e-9 ? oh[b] / mean_h : 0.0);
    }
    /* subtract a moving average (about +-0.25 s) so only peaks above their surroundings remain */
    for (b = 0; b < n; b++) {
        prefix[b + 1] = prefix[b] + o[b];
    }
    w = (long)(*fps * 0.25);
    for (b = 0; b < n; b++) {
        long lo = (long)b - w, hi = (long)b + w + 1;
        double mean;
        if (lo < 0) {
            lo = 0;
        }
        if (hi > (long)n) {
            hi = (long)n;
        }
        mean = (prefix[hi] - prefix[lo]) / (double)(hi - lo);
        r[b] = fmax(0.0, o[b] - mean);
    }
    free(low_e);
    free(hi_e);
    free(o);
    free(oh);
    free(prefix);
    *n_out = n;
    return r;

fail:
    free(low_e);
    free(hi_e);
    free(o);
    free(oh);
    free(prefix);
    free(r);
    return NULL;
}

static double tempo_weight(double bpm, double prior)
{
    double d;

    if (prior > 0) {
        d = log2(bpm / prior);
        return exp(-0.5 * (d / 0.2) * (d / 0.2));
    }
    d = log2(bpm / 125.0);
    return exp(-0.5 * (d / 0.7) * (d / 0.7));
}

/* Folds the onsets modulo `period` frames; returns the best 3 bin sum, where it is, and the sum of all onsets. */
static double fold_score(const double *o, size_t n, double period, int bins, double *hist, int *best_bin, double *total)
{
    double tot = 0, best = -1;
    const double scale = (double)bins / period;
    int bb = 0, b;
    size_t i;

    memset(hist, 0, (size_t)bins * sizeof *hist);
    for (i = 0; i < n; i++) {
        double ph;
        if (o[i] <= 0) {
            continue;
        }
        ph = fmod((double)i, period) * scale;
        hist[(size_t)ph % (size_t)bins] += o[i];
        tot += o[i];
    }
    for (b = 0; b < bins; b++) {
        double s = hist[(b + bins - 1) % bins] + hist[b] + hist[(b + 1) % bins];
        if (s > best) {
            best = s;
            bb = b;
        }
    }
    if (best_bin) {
        *best_bin = bb;
    }
    if (total) {
        *total = tot;
    }
    return best;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;

    return (x > y) - (x < y);
}

int tc_analyse_beats(const struct tc_pcm *pcm, double prior_bpm, double min_bpm, double max_bpm, struct tc_beat_analysis *res)
{
    double fps = 0, *o = NULL, *ac = NULL, *hist = NULL, *strengths = NULL, *pos_arr = NULL, *sorted = NULL;
    long *ks = NULL;
    size_t n = 0, i;
    int oom = 0, rc = 0;
    long lmin, lmax, lag, best_lag = 0;
    double best_score = -1, coarse_lag, coarse_bpm, best_fold = -1, best_period, total = 0, mean3, ratio;
    const int bins = 64;
    int best_bin = 0, pass;
    double a, b, bpm, hop_sec, period_sec, t0;

    memset(res, 0, sizeof *res);
    res->confidence = 0;
    if (tc_pcm_seconds(pcm) < 4.0) {
        return 0;
    }
    o = onset_envelope(pcm, &fps, &n, &oom);
    if (o == NULL) {
        return oom ? -1 : 0;
    }

    /* coarse tempo: autocorrelation of the onsets, with a tempo prior to pick the octave */
    if (prior_bpm > 0) {
        min_bpm = fmin(min_bpm, prior_bpm * 0.8);
        max_bpm = fmax(max_bpm, prior_bpm * 1.2);
    }
    lmin = (long)(fps * 60.0 / max_bpm);
    lmax = (long)(fps * 60.0 / min_bpm);
    if ((long)n < lmax * 4 || lmin < 1) {
        goto out;
    }
    ac = calloc((size_t)lmax * 2 + 2, sizeof *ac);
    hist = calloc((size_t)bins, sizeof *hist);
    if (!ac || !hist) {
        rc = -1;
        goto out;
    }
    for (lag = lmin; lag <= lmax * 2; lag++) {
        double s = 0;
        for (i = 0; i + (size_t)lag < n; i++) {
            s += o[i] * o[i + (size_t)lag];
        }
        ac[lag] = s / (double)(n - (size_t)lag);
    }
    for (lag = lmin; lag <= lmax; lag++) {
        double bpm_l = fps * 60.0 / (double)lag;
        double s = (ac[lag] + 0.5 * ac[lag * 2]) * tempo_weight(bpm_l, prior_bpm);
        if (s > best_score) {
            best_score = s;
            best_lag = lag;
        }
    }
    if (best_lag == 0 || best_score <= 0) {
        goto out;
    }
    coarse_lag = (double)best_lag;
    if (best_lag > lmin && best_lag < lmax) {        /* parabolic interpolation of the peak */
        double l = ac[best_lag - 1], c = ac[best_lag], r = ac[best_lag + 1];
        double den = l - 2 * c + r;
        if (fabs(den) > 1e-12) {
            coarse_lag += 0.5 * (l - r) / den;
        }
    }

    /* fine period and phase: fold at many candidate periods around the coarse one and keep the most coherent */
    best_period = coarse_lag;
    coarse_bpm = fps * 60.0 / coarse_lag;
    for (bpm = coarse_bpm * 0.97; bpm <= coarse_bpm * 1.03; bpm += 0.02) {
        int bb = 0;
        double tot = 0;
        double s = fold_score(o, n, fps * 60.0 / bpm, bins, hist, &bb, &tot);
        if (s > best_fold) {
            best_fold = s;
            best_period = fps * 60.0 / bpm;
            best_bin = bb;
        }
    }
    fold_score(o, n, best_period, bins, hist, &best_bin, &total);
    if (total <= 0) {
        goto out;
    }
    mean3 = 3.0 * total / bins;
    ratio = best_fold / mean3;
    res->confidence = ratio > 1.0 ? 1.0 - 1.0 / ratio : 0.0;
    if (res->confidence < TC_BEATS_MIN_CONFIDENCE) {
        goto out;
    }

    /* least squares fit through the onset peaks near the predicted beats */
    {
        double phase = ((double)best_bin + 0.5) / bins * best_period;
        size_t cap = (size_t)((double)n / (best_period > 1 ? best_period : 1)) + 8;
        a = phase;
        b = best_period;
        ks = malloc(cap * sizeof *ks);
        pos_arr = malloc(cap * sizeof *pos_arr);
        strengths = malloc(cap * sizeof *strengths);
        sorted = malloc(cap * sizeof *sorted);
        if (!ks || !pos_arr || !strengths || !sorted) {
            rc = -1;
            goto out;
        }
        for (pass = 0; pass < 4; pass++) {
            const double win = fmax(2.0, 0.12 * b);
            size_t npicks = 0;
            long k;
            double median, sw = 0, sk = 0, sp = 0, skk = 0, skp = 0, den;
            int used = 0;

            for (k = 0;; k++) {
                double pred = a + b * (double)k, pos;
                long lo, hi, arg, ii;
                if (pred + win >= (double)n) {
                    break;
                }
                if (pred - win < 1) {
                    continue;
                }
                lo = (long)(pred - win);
                hi = (long)(pred + win);
                arg = lo;
                for (ii = lo; ii <= hi; ii++) {
                    if (o[ii] > o[arg]) {
                        arg = ii;
                    }
                }
                if (o[arg] <= 0) {
                    continue;
                }
                pos = (double)arg;
                if (arg > 0 && (size_t)arg + 1 < n) {
                    double l = o[arg - 1], c = o[arg], r2 = o[arg + 1];
                    double d2 = l - 2 * c + r2;
                    if (d2 < -1e-12) {
                        pos += fmax(-0.5, fmin(0.5, 0.5 * (l - r2) / d2));
                    }
                }
                if (npicks < cap) {
                    ks[npicks] = k;
                    pos_arr[npicks] = pos;
                    strengths[npicks] = o[arg];
                    npicks++;
                }
            }
            if (npicks < 8) {
                break;
            }
            memcpy(sorted, strengths, npicks * sizeof *sorted);
            qsort(sorted, npicks, sizeof *sorted, cmp_double);
            median = sorted[npicks / 2];
            for (i = 0; i < npicks; i++) {
                double kk, p, w;
                if (strengths[i] < 0.25 * median) {      /* not a real beat */
                    continue;
                }
                kk = (double)ks[i];
                p = pos_arr[i];
                w = strengths[i];
                if (pass > 0 && fabs(p - (a + b * kk)) > 0.03 * b + 1.0) {      /* outlier */
                    continue;
                }
                sw += w;
                sk += w * kk;
                sp += w * p;
                skk += w * kk * kk;
                skp += w * kk * p;
                used++;
            }
            den = sw * skk - sk * sk;
            if (used < 8 || fabs(den) < 1e-9) {
                break;
            }
            b = (sw * skp - sk * sp) / den;
            a = (sp - b * sk) / sw;
            res->beats_fitted = used;
        }
    }
    if (b <= 0) {
        goto out;
    }

    hop_sec = (double)HOP / pcm->sample_rate;
    period_sec = b * hop_sec;
    t0 = (a + 0.5) * hop_sec;                       /* a frame covers [i, i+1) hops: its middle is the click */
    t0 -= floor(t0 / period_sec) * period_sec;      /* the first grid point at or after the start */
    res->period_sec = period_sec;
    res->grid.bpm = round(60.0 / period_sec * 100.0) / 100.0;
    res->grid.first_beat_us = llround(t0 * 1e6);
    res->grid.confidence = -1;

out:
    free(o);
    free(ac);
    free(hist);
    free(ks);
    free(pos_arr);
    free(strengths);
    free(sorted);
    return rc;
}

//SNIP_beats
