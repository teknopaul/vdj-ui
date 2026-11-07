// Test oracle: runs the GUI's C++ exporter (src/trakcrate/exporter.cpp) on lists of track directories, so the C implementation can be
// compared with it (src-cli/test/test_export_oracle.sh). Not installed, not part of the packages.
//
//   cxx_export TEMPLATE.pdb DEST [--no-artwork] < LISTS      LISTS: NAME TAB TRACKDIR per line, as `trakcrate-helper export` reads them
// The artwork "encoder" copies the source bytes: the JPEG encoders differ, so artwork files are compared by name only.
#include "trakcrate/exporter.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: cxx_export TEMPLATE.pdb DEST [--no-artwork] < LISTS\n");
        return 2;
    }
    trakcrate::ExportOptions o;
    std::ifstream t(argv[1], std::ios::binary);
    std::stringstream ss;
    ss << t.rdbuf();
    o.templatePdb = ss.str();
    o.dest = argv[2];
    o.writeManifest = true;                        // the C helper always writes SHA256SUMS and the manifest
    bool art = true;
    for (int i = 3; i < argc; ++i)
        if (!std::strcmp(argv[i], "--no-artwork")) art = false;
    if (art) o.encodeArtwork = [](const std::string& src, int, std::string* out, std::string*) { *out = src; return true; };
    std::vector<trakcrate::ExportList> lists;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        const size_t tab = line.find('\t');
        const std::string name = line.substr(0, tab), dir = line.substr(tab + 1);
        trakcrate::ExportList* l = nullptr;
        for (auto& x : lists) if (x.name == name) l = &x;
        if (!l) { lists.push_back({name, {}}); l = &lists.back(); }
        if (!dir.empty()) l->tracks.push_back(dir);
    }
    o.lists = lists;
    trakcrate::Library lib;
    trakcrate::ExportResult r = trakcrate::exportDrive(lib, o);
    if (!r.ok) {
        std::fprintf(stderr, "cxx_export: %s\n", r.error.c_str());
        return 1;
    }
    return 0;
}
