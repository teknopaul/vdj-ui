#ifndef TC_ARTWORK_H
#define TC_ARTWORK_H

#include <stddef.h>
#include <stdint.h>

/*
 * The artwork file of a rekordbox drive (doc/trakcrate/artwork.md): the image is centre cropped to a square, scaled to size x
 * size, composed over white (alpha) and written as a baseline JPEG with no metadata. Any image libavcodec reads (jpg, png ...).
 * The master copy is never touched; the exporter derives the player files. The same input gives the same bytes on one machine.
 */
#define TC_ARTWORK_SIZE 80          /* a real rekordbox drive (2148 images) holds only a<id>.jpg at 80x80 */

/* where an artwork file lives and what table 13 stores: "/PIONEER/Artwork/00001/a1.jpg", ids go 20 to a directory */
void tc_artwork_path(uint32_t id, char *out, size_t outsz);

/* 0, or -1 with a message in err. *jpeg is malloc'd. */
int tc_artwork_jpeg(const uint8_t *src, size_t len, int size, uint8_t **jpeg, size_t *jpeg_len, char *err, size_t errsz);

#endif /* TC_ARTWORK_H */
