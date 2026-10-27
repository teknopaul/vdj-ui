#include "fsutil.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace trakcrate {

static void setErr(std::string* err, const std::string& what, const fs::path& path)
{
    if (err) *err = what + " " + path.string() + ": " + std::strerror(errno);
}

bool readFile(const fs::path& path, std::string* out, std::string* err)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) { setErr(err, "cannot open", path); return false; }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) { setErr(err, "cannot read", path); return false; }
    *out = ss.str();
    return true;
}

bool atomicWrite(const fs::path& path, const std::string& data, std::string* err)
{
    fs::path tmp = path.parent_path() / ("." + path.filename().string() + ".tmp." + std::to_string(getpid()));
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { setErr(err, "cannot create", tmp); return false; }
    size_t done = 0;
    while (done < data.size()) {
        ssize_t n = ::write(fd, data.data() + done, data.size() - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            setErr(err, "cannot write", tmp);
            ::close(fd);
            ::unlink(tmp.c_str());
            return false;
        }
        done += size_t(n);
    }
    if (::fsync(fd) != 0 || ::close(fd) != 0) {
        setErr(err, "cannot sync", tmp);
        ::unlink(tmp.c_str());
        return false;
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        setErr(err, "cannot rename to", path);
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

fs::path makeTempDir(const std::string& prefix, const fs::path& base, std::string* err)
{
    std::error_code ec;
    fs::path dir = base.empty() ? fs::temp_directory_path(ec) : base;
    if (ec) { if (err) *err = "no temp directory: " + ec.message(); return fs::path(); }
    std::string tmpl = (dir / (prefix + "-XXXXXX")).string();
    if (!mkdtemp(&tmpl[0])) { setErr(err, "cannot create a temp directory in", dir); return fs::path(); }
    return fs::path(tmpl);
}

bool isSafeName(const std::string& name)
{
    if (name.empty() || name == "." || name == "..") return false;
    return name.find('/') == std::string::npos && name.find('\0') == std::string::npos;
}

std::string sanitizeName(const std::string& name)
{
    std::string s;
    for (unsigned char c : name) s += (c < 0x20 || c == 0x7f || c == '/') ? '_' : char(c);
    size_t b = s.find_first_not_of(" .");
    if (b == std::string::npos) return "_";
    size_t e = s.find_last_not_of(' ');
    s = s.substr(b, e - b + 1);
    if (s.size() > 200) {
        size_t cut = 200;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;   // do not cut inside a UTF-8 sequence
        s.resize(cut);
    }
    return s.empty() ? "_" : s;
}

} // namespace trakcrate
