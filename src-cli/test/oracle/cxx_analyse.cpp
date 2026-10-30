// Test oracle: runs the GUI's C++ analyser (src/trakcrate/analysis.cpp) on a track directory, so the C implementation of the
// command line tools can be compared with it (src-cli/test/test_oracle.sh). Not installed, not part of the packages.
//
//   cxx_analyse TRACKDIR       writes beatgrid.txt, waveform.bin, meta.txt, source.txt in TRACKDIR like `trakcrate-helper analyse`
#include "trakcrate/analysis.h"

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: cxx_analyse TRACKDIR\n");
        return 2;
    }
    trakcrate::AnalyseResult r = trakcrate::analyseTrack(argv[1]);
    if (!r.ok) {
        std::fprintf(stderr, "cxx_analyse: %s\n", r.error.c_str());
        return 1;
    }
    return 0;
}
