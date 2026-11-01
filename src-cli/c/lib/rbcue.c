#include "rbcue.h"

#include <math.h>
#include <string.h>

//SNIP_rbcue

/* column of a time in the detail waveforms (150 per second), clamped to `columns` */
static size_t detail_column(double ms, size_t columns)
{
    double c;

    if (columns == 0) {
        return 0;
    }
    c = floor((ms > 0 ? ms : 0) * TC_WAVE_COLUMNS_PER_SECOND / 1000.0 + 0.5);
    return c > (double)(columns - 1) ? columns - 1 : (size_t)c;
}

/* column of a time in a preview of `columns` columns over duration_ms, clamped */
static size_t preview_column(double ms, double duration_ms, size_t columns)
{
    double c;

    if (columns == 0 || duration_ms <= 0) {
        return 0;
    }
    c = floor((ms > 0 ? ms : 0) / duration_ms * (double)columns);
    return c > (double)(columns - 1) ? columns - 1 : (size_t)c;
}

int tc_cuerender(const struct tc_waveform *w, const struct tc_cuemarks *cm, double duration_ms, const struct tc_cuerender *o,
                 struct tc_waveform *out)
{
    size_t width = o->width_columns < 1 ? 1 : (size_t)o->width_columns, i, k, c;

    *out = *w;
    out->pwv3 = NULL;
    out->pwv5 = NULL;
    if (w->columns > 0) {
        out->pwv3 = malloc(w->columns);
        out->pwv5 = malloc((w->columns + w->pwv5_extra) * sizeof *out->pwv5);
        if (out->pwv3 == NULL || out->pwv5 == NULL) {
            tc_waveform_free(out);
            return -1;
        }
        memcpy(out->pwv3, w->pwv3, w->columns);
        memcpy(out->pwv5, w->pwv5, (w->columns + w->pwv5_extra) * sizeof *out->pwv5);
    }
    if (!o->enabled) {
        return 0;
    }
    for (i = 0; i < cm->n; i++) {
        const struct tc_cuemark *m = &cm->marks[i];

        if (m->hot >= 'A' && m->hot <= 'C' && !o->draw_hot) {
            continue;
        }
        if (out->columns > 0) {
            c = detail_column(m->ms, out->columns);
            if (c + width > out->columns) {
                c = out->columns > width ? out->columns - width : 0;
            }
            for (k = c; k < c + width && k < out->columns; k++) {
                out->pwv3[k] = TC_SPIKE_MONO;
            }
            for (k = c; k < c + width && k < out->columns; k++) {
                out->pwv5[k] = o->green ? TC_SPIKE_GREEN5 : TC_SPIKE_WHITE5;
            }
        }
        out->pwav[preview_column(m->ms, duration_ms, TC_WAVE_PREVIEW_COLUMNS)] = TC_SPIKE_MONO;
        out->pwv2[preview_column(m->ms, duration_ms, TC_WAVE_TINY_COLUMNS)] = TC_SPIKE_TINY;
    }
    return 0;
}

//SNIP_rbcue
