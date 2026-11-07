// Test oracle: runs the GUI's C++ addToDrive (src/trakcrate/exporter.cpp) on a pendrive folder, so the C implementation (tc_add_to_drive, `trakcrate-helper
// add-to-drive`) can be compared with it (src-cli/test/test_add_oracle.sh). Not installed, not part of the packages.
//
//   cxx_add TEMPLATE.pdb DRIVE BACKUPDIR LIBRARY PLAYLIST [--no-artwork]      PLAYLIST is a playlist of the trakcrate library LIBRARY
// stdout: "new N existing M files F". The artwork "encoder" copies the source bytes, so artwork files are compared by name only.
#include "trakcrate/exporter.h"
#include "trakcrate/library.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv)
{
    if (argc < 6) {
        std::fprintf(stderr, "usage: cxx_add TEMPLATE.pdb DRIVE BACKUPDIR LIBRARY PLAYLIST [--no-artwork]\n");
        return 2;
    }
    trakcrate::AddOptions a;
    trakcrate::ExportOptions& o = a.base;
    std::ifstream t(argv[1], std::ios::binary);
    std::stringstream ss;
    ss << t.rdbuf();
    o.templatePdb = ss.str();
    o.dest = argv[2];
    a.backupDir = argv[3];
    o.playlists.push_back(argv[5]);
    bool art = true;
    for (int i = 6; i < argc; ++i)
        if (!std::strcmp(argv[i], "--no-artwork")) art = false;
    if (art) o.encodeArtwork = [](const std::string& src, int, std::string* out, std::string*) { *out = src; return true; };
    trakcrate::Library lib;
    std::string err;
    if (!trakcrate::Library::open(argv[4], &lib, &err)) {
        std::fprintf(stderr, "cxx_add: cannot open the library: %s\n", err.c_str());
        return 2;
    }
    trakcrate::AddResult r = trakcrate::addToDrive(lib, a);
    if (!r.ok) {
        std::fprintf(stderr, "cxx_add: %s\n", r.error.c_str());
        return 1;
    }
    std::printf("new %d existing %d files %d\n", r.newTracks, r.existingTracks, r.filesCopied);
    return 0;
}
