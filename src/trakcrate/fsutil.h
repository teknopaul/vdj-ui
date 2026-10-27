#ifndef TRAKCRATE_FSUTIL_H
#define TRAKCRATE_FSUTIL_H

#include <filesystem>
#include <string>

// Plain C++17 helpers for the trakcrate library files. No Qt: this module is meant to move into the command line tools later.
namespace trakcrate {

namespace fs = std::filesystem;

bool readFile(const fs::path& path, std::string* out, std::string* err = nullptr);

// Write data to a temporary file in the same directory, fsync it, then rename it over path. A crash leaves the old file or the new one.
bool atomicWrite(const fs::path& path, const std::string& data, std::string* err = nullptr);

// A new empty directory under the system temp directory (or under base when given). Empty path on failure.
fs::path makeTempDir(const std::string& prefix, const fs::path& base = fs::path(), std::string* err = nullptr);

// A single path component made of any text: '/' and control characters replaced, leading dots removed, trimmed, at most 200 bytes
// (never cutting a UTF-8 sequence). Never empty ("_" at worst).
std::string sanitizeName(const std::string& name);

// True for a name that is one plain path component: not empty, not "." or "..", no '/' or NUL.
bool isSafeName(const std::string& name);

} // namespace trakcrate

#endif
