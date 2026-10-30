#ifndef TC_PCM_H
#define TC_PCM_H

#include <stdlib.h>

/* Decoded audio: interleaved stereo float at 44100 Hz (-1..1). Made by tc_decode (decode.h), consumed by the analysers. */
#define TC_SAMPLE_RATE 44100

struct tc_pcm {
    float *stereo;          /* L R L R ... */
    size_t frames;
    int sample_rate;        /* TC_SAMPLE_RATE */
};

static inline double tc_pcm_seconds(const struct tc_pcm *pcm)
{
    return (double)pcm->frames / pcm->sample_rate;
}

static inline void tc_pcm_free(struct tc_pcm *pcm)
{
    free(pcm->stereo);
    pcm->stereo = NULL;
    pcm->frames = 0;
}

#endif /* TC_PCM_H */
