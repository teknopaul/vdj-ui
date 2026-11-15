#ifndef TRAKCRATE_MIX_FILE_H
#define TRAKCRATE_MIX_FILE_H

#include "library.h"
#include "mix_model.h"

#include <string>
#include <vector>

// The .mix XML file of a studio mix (doc/trakcrate/studio-mix.md) and the mixes/ folder of the library.
namespace trakcrate {

constexpr int kMixFileVersion = 1;

// Track files are stored relative to `root` (absolute when outside it).
std::string mixToXml(const Mix& mix, const fs::path& root);
// Unknown elements and attributes are ignored. A track file that does not exist is kept with missing=true and a warning.
bool mixFromXml(const std::string& xml, const fs::path& root, Mix* out, std::vector<std::string>* warnings = nullptr, std::string* err = nullptr);

// relName: "Name" or "Folder/Name" (no extension, no "..", every component a safe name). Atomic; creates folders as needed.
bool isSafeMixName(const std::string& relName);
bool saveMix(const Library& lib, const std::string& relName, const Mix& mix, std::string* err = nullptr);
bool loadMix(const fs::path& file, const fs::path& root, Mix* out, std::vector<std::string>* warnings = nullptr, std::string* err = nullptr);

struct MixTreeEntry {
    std::string name;              // file name without ".mix", or the folder name
    fs::path path;
    bool folder = false;
    std::vector<MixTreeEntry> children;
};
// The mixes/ folder as a tree: folders first, natural order, only *.mix files, hidden entries skipped. An absent mixes/ gives an empty root.
MixTreeEntry listMixes(const Library& lib);

// `base`, then "base 2", "base 3" ... until no mixes/<name>.mix exists.
std::string uniqueMixName(const Library& lib, const std::string& base);

} // namespace trakcrate

#endif
