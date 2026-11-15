#pragma once
// The user's real home directory, and where a trakcrate library lives by default.
//
// Inside a Snap, $HOME and QDir::homePath() point at the private ~/snap/<name>/<rev>. The Snap's `home` plug gives access to the
// visible files of the real home ($SNAP_REAL_HOME), so the library (~/trakcrate) is found from there. The same folder is the
// default for the deb and the command line tools, so all of them share one library.
#include <pwd.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>

namespace trakcrate {

inline std::filesystem::path realHome()
{
    if (const char* s = std::getenv("SNAP_REAL_HOME"); s && *s) return s;
    if (const passwd* pw = ::getpwuid(::getuid()); pw && pw->pw_dir && *pw->pw_dir) return pw->pw_dir;
    if (const char* h = std::getenv("HOME"); h && *h) return h;
    return "/";
}

// The user name for /media/<user> and /run/media/<user>: from the account, not from the (Snap rewritten) home folder name.
inline std::string realUserName()
{
    if (const passwd* pw = ::getpwuid(::getuid()); pw && pw->pw_name && *pw->pw_name) return pw->pw_name;
    if (const char* u = std::getenv("USER"); u && *u) return u;
    if (const char* u = std::getenv("LOGNAME"); u && *u) return u;
    return {};
}

inline std::filesystem::path defaultLibraryDir() { return realHome() / "trakcrate"; }

}  // namespace trakcrate
