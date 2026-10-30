/*
 * The player's support files from a real drive (data/user-settings/), embedded verbatim with the assembler's .incbin so an export always has
 * them: PIONEER/djprofile.nxs, PIONEER/rekordbox/RBFLTR.DAT and PIONEER/rekordbox/exportExt.pdb. Replace the files in that folder to change the
 * defaults; never edit them by hand.
 */
#include "rbsettings.h"

#include <stdlib.h>
#include <string.h>

#ifndef TC_USER_SETTINGS_DIR
#error "TC_USER_SETTINGS_DIR must name the folder holding djprofile.nxs, RBFLTR.DAT and exportExt.pdb"
#endif

#define TC_INCBIN(name, file)                                                                                       \
    __asm__(".pushsection .rodata\n.balign 16\n.global " #name "_start\n" #name "_start:\n.incbin \"" TC_USER_SETTINGS_DIR "/" file "\"\n" \
            ".global " #name "_end\n" #name "_end:\n.byte 0\n.popsection\n");                                       \
    extern const unsigned char name##_start[];                                                                      \
    extern const unsigned char name##_end[]

TC_INCBIN(tc_inc_djprofile, "djprofile.nxs");
TC_INCBIN(tc_inc_rbfltr, "RBFLTR.DAT");
TC_INCBIN(tc_inc_exportext, "exportExt.pdb");

const uint8_t *tc_default_djprofile(size_t *len)
{
    *len = (size_t)(tc_inc_djprofile_end - tc_inc_djprofile_start);
    return tc_inc_djprofile_start;
}

const uint8_t *tc_default_rbfltr(size_t *len)
{
    *len = (size_t)(tc_inc_rbfltr_end - tc_inc_rbfltr_start);
    return tc_inc_rbfltr_start;
}

const uint8_t *tc_default_exportext(size_t *len)
{
    *len = (size_t)(tc_inc_exportext_end - tc_inc_exportext_start);
    return tc_inc_exportext_start;
}

uint8_t *tc_empty_usbmng(size_t *len)
{
    static const uint8_t head[56] = {0x50, 0x4d, 0x4e, 0x47, 0x00, 0x00, 0x00, 0x20, 0x00, 0x06, 0x1a, 0xba, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x54, 0x42, 0x4c, 0x00, 0x00, 0x00, 0x14, 0x00, 0x06, 0x1a, 0x9a, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x0d, 0x43, 0x00, 0x00, 0x00, 0x00};
    uint8_t *b = calloc(1, 400058);

    if (b == NULL) {
        return NULL;
    }
    memcpy(b, head, sizeof head);
    *len = 400058;
    return b;
}
