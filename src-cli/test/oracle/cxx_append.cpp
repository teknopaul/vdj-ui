// Test oracle: runs the GUI's C++ database editor (src/trakcrate/pdb_append.cpp) on a plan, so the C implementation (src-cli/c/lib/rbappend.c) can be
// compared with it (src-cli/test/test_append_oracle.sh). Not installed, not part of the packages.
//
//   cxx_append IN.pdb TEMPLATE.pdb OUT.pdb PLAN...
// Each PLAN file is added in turn to the result of the one before; OUT.pdb is the final database. One line per plan on stdout:
//   plan N: rows R pages P playlist ID tracks ID:new|old:ANALYZEPATH ... artwork PATH ...
// PLAN files are TSV: "T title artist album genre label file_path artwork_key artwork_path file_size" per track,
// "P name i,j,k" per playlist (indexes of the T lines, 0 based).
#include "trakcrate/pdb_append.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace trakcrate;

static std::string slurp(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> v;
    size_t b = 0;
    for (;;) {
        const size_t e = s.find(sep, b);
        v.push_back(s.substr(b, e == std::string::npos ? std::string::npos : e - b));
        if (e == std::string::npos) break;
        b = e + 1;
    }
    return v;
}

int main(int argc, char** argv)
{
    if (argc < 5) {
        std::fprintf(stderr, "usage: cxx_append IN.pdb TEMPLATE.pdb OUT.pdb PLAN...\n");
        return 2;
    }
    std::string bytes = slurp(argv[1]);
    AppendOptions ao;
    ao.templatePdb = slurp(argv[2]);
    for (int a = 4; a < argc; ++a) {
        ExportPlan plan;
        std::ifstream in(argv[a]);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            const std::vector<std::string> f = split(line, '\t');
            if (f[0] == "T" && f.size() >= 10) {
                PlanTrack t;
                t.title = f[1]; t.artist = f[2]; t.album = f[3]; t.genre = f[4]; t.label = f[5];
                t.filePath = f[6]; t.artworkKey = f[7]; t.artworkPath = f[8];
                t.fileSize = uint32_t(std::stoul(f[9]));
                t.fileName = t.filePath.substr(t.filePath.rfind('/') + 1);
                t.dateAdded = "2026-11-07";
                plan.tracks.push_back(t);
            } else if (f[0] == "P" && f.size() >= 3) {
                PlanPlaylist p;
                p.name = f[1];
                for (const std::string& i : split(f[2], ',')) if (!i.empty()) p.tracks.push_back(size_t(std::stoul(i)));
                plan.playlists.push_back(p);
            }
        }
        DriveDb db;
        std::vector<std::string> problems;
        if (!loadDriveDb(bytes, &db, &problems)) {
            std::printf("error: the database cannot be added to: %s\n", problems.empty() ? "?" : problems[0].c_str());
            return 1;
        }
        AppendResult r;
        std::string err;
        if (!appendPlaylist(db, plan, ao, &r, &err)) {
            std::printf("error: %s\n", err.c_str());
            return 1;
        }
        const std::vector<std::string> diff = verifyAppended(bytes, r.bytes, plan, r);
        if (!diff.empty()) {
            std::printf("error: verify: %s\n", diff[0].c_str());
            return 1;
        }
        std::printf("plan %d: rows %d pages %d playlist %u tracks", a - 3, r.rowsAdded, r.pagesAdded, r.playlistId);
        for (size_t i = 0; i < r.trackIds.size(); ++i) std::printf(" %u:%s:%s", r.trackIds[i], r.trackIsNew[i] ? "new" : "old", r.analyzePaths[i].c_str());
        std::printf(" artwork");
        for (const std::string& p : r.artworkPaths) std::printf(" %s", p.c_str());
        std::printf("\n");
        bytes = r.bytes;
    }
    std::ofstream out(argv[3], std::ios::binary);
    out << bytes;
    return 0;
}
