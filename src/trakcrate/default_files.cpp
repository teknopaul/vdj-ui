// The player's support files from a real drive (data/trakcrate/user-settings/), embedded verbatim with the assembler's .incbin so a pendrive
// always gets them. Replace the files in that folder to change the defaults; never edit them by hand.
#include "settings_files.h"

#ifndef TC_USER_SETTINGS_DIR
#error "TC_USER_SETTINGS_DIR must name the folder holding djprofile.nxs, RBFLTR.DAT and exportExt.pdb"
#endif

#define TC_INCBIN(name, file)                                                                                       \
    __asm__(".pushsection .rodata\n.balign 16\n.global " #name "_start\n" #name "_start:\n.incbin \"" TC_USER_SETTINGS_DIR "/" file "\"\n" \
            ".global " #name "_end\n" #name "_end:\n.byte 0\n.popsection\n");                                         \
    extern "C" const unsigned char name##_start[];                                                                  \
    extern "C" const unsigned char name##_end[]

TC_INCBIN(tc_inc_djprofile, "djprofile.nxs");
TC_INCBIN(tc_inc_rbfltr, "RBFLTR.DAT");
TC_INCBIN(tc_inc_exportext, "exportExt.pdb");

namespace trakcrate {

const std::string& defaultDjprofile()
{
    static const std::string s(reinterpret_cast<const char*>(tc_inc_djprofile_start), size_t(tc_inc_djprofile_end - tc_inc_djprofile_start));
    return s;
}

const std::string& defaultRbfltr()
{
    static const std::string s(reinterpret_cast<const char*>(tc_inc_rbfltr_start), size_t(tc_inc_rbfltr_end - tc_inc_rbfltr_start));
    return s;
}

const std::string& defaultExportExt()
{
    static const std::string s(reinterpret_cast<const char*>(tc_inc_exportext_start), size_t(tc_inc_exportext_end - tc_inc_exportext_start));
    return s;
}

std::string emptyUsbmng()
{
    static const unsigned char head[56] = {0x50, 0x4d, 0x4e, 0x47, 0x00, 0x00, 0x00, 0x20, 0x00, 0x06, 0x1a, 0xba, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x54, 0x42, 0x4c, 0x00, 0x00, 0x00, 0x14, 0x00, 0x06, 0x1a, 0x9a, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x0d, 0x43, 0x00, 0x00, 0x00, 0x00};
    std::string s(reinterpret_cast<const char*>(head), sizeof head);
    s.resize(400058, '\0');
    return s;
}

} // namespace trakcrate
