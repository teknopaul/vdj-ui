#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "wave.h"

//SNIP_wave

int tc_height_from_peak(double peak)
{
    long h;

    if (peak <= 0) {
        return 0;
    }
    h = lround(31.0 * fmin(1.0, peak));
    return h < 0 ? 0 : (h > 31 ? 31 : (int)h);
}

int tc_band_level(double peak)
{
    long l = lround(7.0 * fmin(1.0, peak * 2.0));

    return l < 0 ? 0 : (l > 7 ? 7 : (int)l);
}

/* the preview of `count` columns made from the detail columns: peak of the heights, mean whiteness */
static void slice(size_t cols, const uint8_t *height, const uint8_t *white, int count, int mean_white, uint8_t *h_out, uint8_t *w_out)
{
    int i;
    size_t k;

    for (i = 0; i < count; i++) {
        size_t a, b;
        int h_max = 0, w_sum = 0;
        h_out[i] = 0;
        w_out[i] = 0;
        if (cols == 0) {
            continue;
        }
        a = (size_t)i * cols / (size_t)count;
        b = (size_t)(i + 1) * cols / (size_t)count;
        if (b < a + 1) {
            b = a + 1;
        }
        if (b > cols) {
            b = cols;
        }
        if (a >= cols) {
            a = cols - 1;
        }
        for (k = a; k < b; k++) {
            if (height[k] > h_max) {
                h_max = height[k];
            }
            w_sum += white[k];
        }
        h_out[i] = (uint8_t)h_max;
        w_out[i] = (uint8_t)(mean_white ? w_sum / (int)(b - a) : 0);
    }
}

int tc_analyse_waveform(const struct tc_pcm *pcm, struct tc_waveform *w)
{
    const size_t col_len = (size_t)(pcm->sample_rate / TC_WAVE_COLUMNS_PER_SECOND);     /* 294 samples at 44.1 kHz */
    const size_t frames = pcm->frames;
    const size_t cols = (frames + col_len - 1) / col_len;
    uint8_t *height = NULL, *white = NULL, h400[TC_WAVE_PREVIEW_COLUMNS], w400[TC_WAVE_PREVIEW_COLUMNS];
    uint8_t h100[TC_WAVE_TINY_COLUMNS], w100[TC_WAVE_TINY_COLUMNS];
    const double a_low = 1.0 - exp(-2.0 * M_PI * 250.0 / pcm->sample_rate);
    const double a_mid = 1.0 - exp(-2.0 * M_PI * 2500.0 / pcm->sample_rate);
    double l1 = 0, l2 = 0, m1 = 0, m2 = 0;
    size_t c, f;
    int i;

    memset(w, 0, sizeof *w);
    w->columns = cols;
    w->pwv3 = calloc(cols ? cols : 1, 1);
    w->pwv5 = calloc(cols ? cols : 1, sizeof *w->pwv5);
    height = calloc(cols ? cols : 1, 1);
    white = calloc(cols ? cols : 1, 1);
    if (!w->pwv3 || !w->pwv5 || !height || !white) {
        free(height);
        free(white);
        tc_waveform_free(w);
        return -1;
    }
    for (c = 0; c < cols; c++) {
        double peak = 0, p_low = 0, p_mid = 0, p_high = 0, sum;
        size_t end = (c + 1) * col_len < frames ? (c + 1) * col_len : frames;
        for (f = c * col_len; f < end; f++) {
            double L = pcm->stereo[2 * f], R = pcm->stereo[2 * f + 1], x;
            peak = fmax(peak, fmax(fabs(L), fabs(R)));
            x = 0.5 * (L + R);
            l1 += a_low * (x - l1);
            l2 += a_low * (l1 - l2);                    /* below ~250 Hz */
            m1 += a_mid * (x - m1);
            m2 += a_mid * (m1 - m2);                    /* below ~2.5 kHz */
            p_low = fmax(p_low, fabs(l2));
            p_mid = fmax(p_mid, fabs(m2 - l2));
            p_high = fmax(p_high, fabs(x - m2));
        }
        height[c] = (uint8_t)tc_height_from_peak(peak);
        sum = p_low + p_mid + p_high;
        {
            long wh = sum > 1e-6 ? lround(7.0 * p_high / sum) : 0;
            white[c] = (uint8_t)(wh < 0 ? 0 : (wh > 7 ? 7 : wh));
        }
        w->pwv3[c] = (uint8_t)((white[c] << 5) | height[c]);
        w->pwv5[c] = (uint16_t)((tc_band_level(p_low) << 13) | (tc_band_level(p_mid) << 10) | (tc_band_level(p_high) << 7) | (height[c] << 2));
    }
    slice(cols, height, white, TC_WAVE_PREVIEW_COLUMNS, 1, h400, w400);
    slice(cols, height, white, TC_WAVE_TINY_COLUMNS, 0, h100, w100);
    for (i = 0; i < TC_WAVE_PREVIEW_COLUMNS; i++) {
        w->pwav[i] = (uint8_t)((w400[i] << 5) | h400[i]);
    }
    for (i = 0; i < TC_WAVE_TINY_COLUMNS; i++) {
        w->pwv2[i] = (uint8_t)(h100[i] >> 1);
    }
    free(height);
    free(white);
    return 0;
}

void tc_waveform_free(struct tc_waveform *w)
{
    free(w->pwv3);
    free(w->pwv5);
    memset(w, 0, sizeof *w);
}

static uint8_t *put32(uint8_t *p, uint32_t v)
{
    int i;

    for (i = 0; i < 4; i++) {
        *p++ = (uint8_t)(v >> (8 * i));
    }
    return p;
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint8_t *tc_waveform_serialise(const struct tc_waveform *w, size_t *len)
{
    size_t total = 12 + (8 + TC_WAVE_PREVIEW_COLUMNS) + (8 + TC_WAVE_TINY_COLUMNS) + (8 + w->columns) + (8 + 2 * w->columns), i;
    uint8_t *out = malloc(total), *p;

    if (out == NULL) {
        return NULL;
    }
    p = out;
    memcpy(p, "TCWV", 4);
    p = put32(p + 4, 1);
    p = put32(p, (uint32_t)w->columns);
    memcpy(p, "PWAV", 4);
    p = put32(p + 4, TC_WAVE_PREVIEW_COLUMNS);
    memcpy(p, w->pwav, TC_WAVE_PREVIEW_COLUMNS);
    p += TC_WAVE_PREVIEW_COLUMNS;
    memcpy(p, "PWV2", 4);
    p = put32(p + 4, TC_WAVE_TINY_COLUMNS);
    memcpy(p, w->pwv2, TC_WAVE_TINY_COLUMNS);
    p += TC_WAVE_TINY_COLUMNS;
    memcpy(p, "PWV3", 4);
    p = put32(p + 4, (uint32_t)w->columns);
    if (w->columns) {
        memcpy(p, w->pwv3, w->columns);
    }
    p += w->columns;
    memcpy(p, "PWV5", 4);
    p = put32(p + 4, (uint32_t)(2 * w->columns));
    for (i = 0; i < w->columns; i++) {
        *p++ = (uint8_t)(w->pwv5[i] >> 8);
        *p++ = (uint8_t)(w->pwv5[i] & 0xff);
    }
    if (len) {
        *len = (size_t)(p - out);
    }
    return out;
}

int tc_waveform_parse(const uint8_t *data, size_t len, struct tc_waveform *w)
{
    size_t at = 12, columns, pwv3_len = 0, pwv5_len = 0, i;
    const uint8_t *pwv3 = NULL, *pwv5 = NULL;
    int have_pwav = 0, have_pwv2 = 0;

    memset(w, 0, sizeof *w);
    if (len < 12 || memcmp(data, "TCWV", 4) != 0 || get32(data + 4) != 1) {
        return -1;
    }
    columns = get32(data + 8);
    while (at + 8 <= len) {
        const uint8_t *tag = data + at;
        size_t slen = get32(data + at + 4);
        at += 8;
        if (slen > len - at) {
            return -1;
        }
        if (memcmp(tag, "PWAV", 4) == 0 && slen == TC_WAVE_PREVIEW_COLUMNS) {
            memcpy(w->pwav, data + at, slen);
            have_pwav = 1;
        } else if (memcmp(tag, "PWV2", 4) == 0 && slen == TC_WAVE_TINY_COLUMNS) {
            memcpy(w->pwv2, data + at, slen);
            have_pwv2 = 1;
        } else if (memcmp(tag, "PWV3", 4) == 0) {
            pwv3 = data + at;
            pwv3_len = slen;
        } else if (memcmp(tag, "PWV5", 4) == 0) {
            if (slen % 2) {
                return -1;
            }
            pwv5 = data + at;
            pwv5_len = slen / 2;
        }
        at += slen;
    }
    (void)have_pwav;
    (void)have_pwv2;
    if (pwv3 == NULL || pwv5 == NULL || pwv3_len != columns || pwv5_len != columns) {
        return -1;
    }
    w->columns = columns;
    w->pwv3 = malloc(columns ? columns : 1);
    w->pwv5 = malloc((columns ? columns : 1) * sizeof *w->pwv5);
    if (!w->pwv3 || !w->pwv5) {
        tc_waveform_free(w);
        return -1;
    }
    if (columns) {
        memcpy(w->pwv3, pwv3, columns);
    }
    for (i = 0; i < columns; i++) {
        w->pwv5[i] = (uint16_t)((pwv5[2 * i] << 8) | pwv5[2 * i + 1]);
    }
    return 0;
}

void tc_measure_loudness(const struct tc_pcm *pcm, double *peak_db, double *rms_db)
{
    double peak = 0, sum = 0, rms;
    size_t i, n = pcm->frames * 2;

    *peak_db = -120;
    *rms_db = -120;
    if (n == 0) {
        return;
    }
    for (i = 0; i < n; i++) {
        double v = pcm->stereo[i];
        peak = fmax(peak, fabs(v));
        sum += v * v;
    }
    rms = sqrt(sum / (double)n);
    *peak_db = peak > 1e-6 ? 20.0 * log10(peak) : -120.0;
    *rms_db = rms > 1e-6 ? 20.0 * log10(rms) : -120.0;
}

//SNIP_wave
