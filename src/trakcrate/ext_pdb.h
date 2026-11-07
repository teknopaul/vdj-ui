#ifndef TRAKCRATE_EXT_PDB_H
#define TRAKCRATE_EXT_PDB_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// exportExt.pdb is the rekordbox "My Tags" database (doc/my-tags.md): table 3 holds the categories and tags, table 4 the links from tracks
// to tags. A link names a track id of the export.pdb it was exported with, and rekordbox numbers the tracks afresh on every export, so a saved
// copy of the file points at tracks that do not exist (or at the wrong ones) on any other drive.
namespace trakcrate {

struct ExtPdbStats {
    size_t tags = 0;             // live rows of the tag table (categories and tags)
    size_t deletedTags = 0;      // rows rekordbox marked deleted but left in the page
    size_t trackLinks = 0;       // live rows of the track to tag table
};

// False (with err) when the bytes are not a pdb this program can read, or have no tag table.
bool extPdbStats(const std::string& bytes, ExtPdbStats* out, std::string* err = nullptr);

// A category or a tag of the My Tags database.
struct ExtTag {
    uint32_t id = 0;
    uint32_t parent = 0;         // the id of the category a tag is in; 0 for a category
    uint32_t position = 0;       // the place of a tag inside its category, of a category in the list
    bool category = false;
    std::string name;            // UTF-8; "?" when the row's strings cannot be read
};

// The live categories and tags, categories by position, each followed by its tags by position. A tag whose category is missing comes last.
bool readExtTags(const std::string& bytes, std::vector<ExtTag>* out, std::string* err = nullptr);

// The same database without any link from a track to a tag: the tags and categories stay, the track to tag table is emptied the way rekordbox
// writes an empty table, and the tag rows rekordbox marked deleted are dropped (a fresh export from rekordbox has none), so the result is as
// clean as a file can be. Nothing else changes. Giving it a file that is already clean returns the same bytes.
bool withoutTrackLinks(const std::string& bytes, std::string* out, std::string* err = nullptr);

} // namespace trakcrate

#endif
