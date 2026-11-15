#include "stretch_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace trakcrate {

size_t stretchedFrames(size_t frames, double ratio) { return size_t(std::llround(double(frames) / ratio)); }

void fitFrames(DecodedPcm* pcm, size_t frames) { pcm->stereo.resize(frames * 2, 0.0f); }

std::string optionValue(const StretchOptions& o, const std::string& name, const std::string& def)
{
    auto it = o.find(name);
    return it == o.end() ? def : it->second;
}

std::string choiceKey(const StretchChoice& c)
{
    std::string k = c.engine;
    for (const auto& o : c.options) k += "|" + o.first + "=" + o.second;
    return k;
}

// ---- registry --------------------------------------------------------------------------------------------------------------------

StretchRegistry& StretchRegistry::instance()
{
    static StretchRegistry r;
    r.registerBuiltins();
    return r;
}

void StretchRegistry::registerBuiltins()
{
    {
        std::lock_guard<std::mutex> g(mu_);
        if (builtins_) return;
        builtins_ = true;
    }
    registerBuiltinStretchEngines(this);
}

bool StretchRegistry::add(std::unique_ptr<StretchEngine> engine)
{
    std::lock_guard<std::mutex> g(mu_);
    for (const auto& e : engines_)
        if (e->id() == engine->id()) return false;
    engines_.push_back(std::shared_ptr<StretchEngine>(std::move(engine)));
    return true;
}

bool StretchRegistry::remove(const std::string& id)
{
    std::lock_guard<std::mutex> g(mu_);
    for (auto it = engines_.begin(); it != engines_.end(); ++it) {
        if ((*it)->id() == id) {
            engines_.erase(it);
            return true;
        }
    }
    return false;
}

StretchEngine* StretchRegistry::find(const std::string& id) const
{
    std::lock_guard<std::mutex> g(mu_);
    for (const auto& e : engines_)
        if (e->id() == id) return e.get();
    return nullptr;
}

std::vector<StretchRegistry::Entry> StretchRegistry::list() const
{
    std::vector<std::shared_ptr<StretchEngine>> copy;
    {
        std::lock_guard<std::mutex> g(mu_);
        copy = engines_;
    }
    std::vector<Entry> out;
    for (const auto& e : copy) {
        Entry en{e.get(), true, std::string()};
        en.available = e->available(&en.why);
        out.push_back(en);
    }
    return out;
}

std::string StretchRegistry::defaultEngineId() const
{
    static const char* const pref[] = {"rubberband-lib-r3", "rubberband-r3", "rubberband-lib-r2", "rubberband-r2", "soundtouch", "sox-tempo", "ffmpeg-atempo", "wsola"};
    for (const char* id : pref) {
        StretchEngine* e = find(id);
        if (e && e->available(nullptr)) return id;
    }
    for (const Entry& e : list())
        if (e.available) return e.engine->id();
    return "wsola";
}

StretchChoice StretchRegistry::normalize(const StretchChoice& c) const
{
    StretchEngine* e = find(c.engine);
    if (!e) return c;
    StretchChoice out;
    out.engine = c.engine;
    for (const StretchOption& o : e->options()) out.options[o.name] = optionValue(c.options, o.name, o.def);
    return out;
}

StretchChoice StretchRegistry::resolveChoice(const StretchChoice& track, const StretchChoice& mixDefault) const
{
    if (!track.engine.empty()) return track;
    if (!mixDefault.engine.empty()) return mixDefault;
    StretchChoice c;
    c.engine = defaultEngineId();
    return c;
}

// ---- helpers -------------------------------------------------------------------------------------------------------------------

bool commandExists(const std::string& name)
{
    if (name.find('/') != std::string::npos) return access(name.c_str(), X_OK) == 0;
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::string p = path;
    size_t b = 0;
    while (b <= p.size()) {
        size_t e = p.find(':', b);
        if (e == std::string::npos) e = p.size();
        const std::string dir = e > b ? p.substr(b, e - b) : ".";
        if (access((dir + "/" + name).c_str(), X_OK) == 0) return true;
        b = e + 1;
    }
    return false;
}

bool runCommand(const std::vector<std::string>& argv, const std::atomic<bool>* cancel, std::string* output, std::string* err)
{
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (argv.empty()) return fail("empty command");
    int pipefd[2];
    if (pipe(pipefd) != 0) return fail("pipe failed");
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], 1);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], 2);
    posix_spawn_file_actions_addclose(&fa, pipefd[0]);
    posix_spawn_file_actions_addclose(&fa, pipefd[1]);
    std::vector<char*> av;
    for (const std::string& a : argv) av.push_back(const_cast<char*>(a.c_str()));
    av.push_back(nullptr);
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, av[0], &fa, nullptr, av.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(pipefd[1]);
    if (rc != 0) {
        close(pipefd[0]);
        return fail("cannot run " + argv[0] + ": " + std::strerror(rc));
    }
    std::string out;
    bool cancelled = false;
    for (;;) {
        if (cancel && cancel->load()) {
            kill(pid, SIGKILL);
            cancelled = true;
            break;
        }
        pollfd p{pipefd[0], POLLIN, 0};
        const int n = poll(&p, 1, 50);
        if (n > 0) {
            char buf[4096];
            const ssize_t r = read(pipefd[0], buf, sizeof buf);
            if (r <= 0) break;                           // EOF: the process closed its output (normally by exiting)
            out.append(buf, size_t(r));
            if (out.size() > (1u << 20)) out.erase(0, out.size() - (1u << 19));
        }
    }
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (output) *output = out;
    if (cancelled) return fail("cancelled");
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
    // the last non empty line
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    const size_t nl = out.find_last_of('\n');
    const std::string last = nl == std::string::npos ? out : out.substr(nl + 1);
    return fail(argv[0] + " failed" + (last.empty() ? std::string() : ": " + last));
}

namespace {
void put32(std::string& s, uint32_t v)
{
    for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xFF);
}
void put16(std::string& s, uint16_t v)
{
    s += char(v & 0xFF);
    s += char(v >> 8);
}
} // namespace

bool writeWavFloat(const fs::path& path, const DecodedPcm& pcm, std::string* err)
{
    const uint32_t dataBytes = uint32_t(pcm.stereo.size() * 4);
    std::string h = "RIFF";
    put32(h, 4 + 8 + 16 + 8 + 4 + 8 + dataBytes);
    h += "WAVEfmt ";
    put32(h, 16);
    put16(h, 3);                                    // IEEE float
    put16(h, 2);
    put32(h, uint32_t(pcm.sampleRate));
    put32(h, uint32_t(pcm.sampleRate) * 8);
    put16(h, 8);
    put16(h, 32);
    h += "fact";
    put32(h, 4);
    put32(h, uint32_t(pcm.frames()));
    h += "data";
    put32(h, dataBytes);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (err) *err = "cannot write " + path.string();
        return false;
    }
    bool ok = std::fwrite(h.data(), 1, h.size(), f) == h.size();
    ok = ok && std::fwrite(pcm.stereo.data(), 4, pcm.stereo.size(), f) == pcm.stereo.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok && err) *err = "write failed: " + path.string();
    return ok;
}

} // namespace trakcrate
