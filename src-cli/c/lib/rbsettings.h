#ifndef TC_RBSETTINGS_H
#define TC_RBSETTINGS_H

#include <stddef.h>
#include <stdint.h>

/*
 * PIONEER/DEVSETTING.DAT and MYSETTING.DAT (doc/trakcrate/settings-files.md): a 0x60 byte text header (brand, "rekordbox",
 * version), a payload length, the magic 0x12345678, the payload and a CRC-16/XMODEM over the bytes from the magic up to the CRC
 * (stored little endian, followed by two zero bytes).
 */

/* polynomial 0x1021, initial value 0, no reflection */
uint16_t tc_crc16_xmodem(const void *data, size_t len);

/* 0 for a well formed file: size, header length, payload length, magic and CRC all agree; -1 and *why says which check failed */
int tc_settings_verify(const uint8_t *bytes, size_t len, const char **why);
/* recomputes the CRC after the payload was changed; -1 when the file is not the right shape */
int tc_settings_fix_crc(uint8_t *bytes, size_t len);

/* the files copied verbatim from a real drive; both pass tc_settings_verify */
const uint8_t *tc_default_devsetting(size_t *len);
const uint8_t *tc_default_mysetting(size_t *len);
/* defaultfiles.c: the other support files of a drive, compiled in */
const uint8_t *tc_default_djprofile(size_t *len);
const uint8_t *tc_default_rbfltr(size_t *len);
const uint8_t *tc_default_exportext(size_t *len);
/* an empty USBANLZ/USBMNG.DAT (56 byte header, zeroed 200003 slot table), malloc'd; NULL on out of memory */
uint8_t *tc_empty_usbmng(size_t *len);

/* a settings file a user provided as the template; a damaged one is refused with err. *out is malloc'd. */
int tc_settings_load(const char *path, uint8_t **out, size_t *len, char *err, size_t errsz);

#endif /* TC_RBSETTINGS_H */
