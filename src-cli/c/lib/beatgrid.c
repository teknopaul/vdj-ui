#include <math.h>
#include <stdio.h>
#include <string.h>

#include "beatgrid.h"

//SNIP_beatgrid

void tc_beatgrid_from(const tc_kvdoc *doc, struct tc_beatgrid *g)
{
    const char *v;

    memset(g, 0, sizeof *g);
    g->bpm = tc_kv_get_double(doc, "bpm", 0);
    if (g->bpm < 0) {
        g->bpm = 0;
    }
    g->first_beat_us = tc_kv_get_int(doc, "first_beat_us", 0);
    g->confidence = tc_kv_get_double(doc, "confidence", -1);
    v = tc_kv_get(doc, "analyser");
    if (v != NULL) {
        snprintf(g->analyser, sizeof g->analyser, "%s", v);
    }
    g->variable_tempo = tc_kv_get_int(doc, "variable_tempo", 0) != 0;
}

int tc_beatgrid_to(tc_kvdoc *doc, const struct tc_beatgrid *g)
{
    char buf[32];

    snprintf(buf, sizeof buf, "%.2f", g->bpm);
    if (tc_kv_set(doc, "bpm", buf) != 0 || tc_kv_set_int(doc, "first_beat_us", g->first_beat_us) != 0) {
        return -1;
    }
    if (g->confidence >= 0) {
        snprintf(buf, sizeof buf, "%.3f", g->confidence);
        if (tc_kv_set(doc, "confidence", buf) != 0) {
            return -1;
        }
    } else {
        tc_kv_remove(doc, "confidence");
    }
    if (g->analyser[0]) {
        if (tc_kv_set(doc, "analyser", g->analyser) != 0) {
            return -1;
        }
    } else {
        tc_kv_remove(doc, "analyser");
    }
    if (g->variable_tempo) {
        if (tc_kv_set(doc, "variable_tempo", "1") != 0) {
            return -1;
        }
    } else {
        tc_kv_remove(doc, "variable_tempo");
    }
    return 0;
}

long long tc_beatgrid_beat_us(const struct tc_beatgrid *g, long n)
{
    if (g->bpm <= 0) {
        return 0;
    }
    return g->first_beat_us + (long long)llround((double)n * 60e6 / g->bpm);
}

//SNIP_beatgrid
