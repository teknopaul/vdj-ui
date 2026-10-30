#ifndef TC_DECODE_H
#define TC_DECODE_H

#include <stddef.h>

#include "pcm.h"

/*
 * A whole track decoded to interleaved stereo float at 44100 Hz (-1..1) with ffmpeg's libavformat, libavcodec and
 * libswresample. A mono file is duplicated to both channels at its original level.
 */
/* 0 on success; -1 with a message in err. Never crashes on garbage. */
int tc_decode(const char *path, struct tc_pcm *out, char *err, size_t errsz);

#endif /* TC_DECODE_H */
