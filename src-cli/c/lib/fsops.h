#ifndef TC_FSOPS_H
#define TC_FSOPS_H

#include <stddef.h>

/* Directory operations the export needs. */

/* the audio file of a track directory (the first .mp3 or .wav by name, case insensitive extension); 1 found, 0 none or too long */
int tc_find_audio(const char *dir, char *out, size_t outsz);

/* mkdir -p; 0, or -1 with errno set */
int tc_mkdir_p(const char *path);

/* rm -rf of a directory tree (symbolic links are removed, never followed); 0, or -1 */
int tc_rm_rf(const char *path);

/* 1 when the directory holds no entries (or does not exist), 0 otherwise */
int tc_dir_empty(const char *path);

#endif /* TC_FSOPS_H */
