// External command engines: a *.stretch text file describes a program that stretches a WAV file (doc/trakcrate/example.stretch).
#include "stretch_engine.h"

#include "track.h"

#include <algorithm>
#include <cstdlib>

namespace trakcrate {

namespace {

// Splits on spaces; double quotes group words ("a b" is one argument).
std::vector<std::string> splitCommand(const std::string& s, bool* ok)
{
    std::vector<std::string> v;
    std::string cur;
    bool inq = false, has = false;
    for (char c : s) {
        if (c == '"') {
            inq = !inq;
            has = true;
        } else if (c == ' ' && !inq) {
            if (has) v.push_back(cur);
            cur.clear();
            has = false;
        } else {
            cur += c;
            has = true;
        }
    }
    if (has) v.push_back(cur);
    *ok = !inq;
    return v;
}

std::string trim(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t"), e = s.find_last_not_of(" \t");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

std::string num(double v)
{
    char b[40];
    std::snprintf(b, sizeof b, "%.9g", v);
    return b;
}

class ExternalEngine : public StretchEngine {
public:
    std::string id_, name_, desc_, command_;
    bool pitch_ = true;
    std::vector<StretchOption> opts_;
    std::vector<std::string> argv_;

    std::string id() const override { return id_; }
    std::string name() const override { return name_; }
    std::string description() const override { return desc_; }
    std::vector<StretchOption> options() const override { return opts_; }
    bool preservesPitch() const override { return pitch_; }
    std::string fingerprint() const override { return command_; }
    bool available(std::string* why) const override
    {
        if (commandExists(argv_[0])) return true;
        if (why) *why = argv_[0] + " is not installed";
        return false;
    }

    bool stretch(const DecodedPcm& in, double ratio, const StretchOptions& o, DecodedPcm* out, const StretchProgress& progress,
                 const std::atomic<bool>* cancel, std::string* err) override
    {
        if (!(ratio >= 0.5 - 1e-9 && ratio <= 2.0 + 1e-9)) {
            if (err) *err = "the stretch ratio must be between 0.5 and 2";
            return false;
        }
        fs::path dir = makeTempDir("stretch-ext");
        if (dir.empty()) {
            if (err) *err = "cannot create a temporary folder";
            return false;
        }
        struct Cleanup {
            fs::path d;
            ~Cleanup() { std::error_code ec; fs::remove_all(d, ec); }
        } cleanup{dir};
        const std::string inPath = (dir / "in.wav").string(), outPath = (dir / "out.wav").string();
        if (!writeWavFloat(inPath, in, err)) return false;
        std::vector<std::string> argv;
        for (std::string a : argv_) {
            auto rep = [&](const std::string& key, const std::string& val) {
                for (size_t p; (p = a.find(key)) != std::string::npos;) a.replace(p, key.size(), val);
            };
            rep("{in}", inPath);
            rep("{out}", outPath);
            rep("{ratio}", num(ratio));
            rep("{bpm_from}", "100");                       // only the ratio is known: tempos are given as 100 -> 100 * ratio
            rep("{bpm_to}", num(100 * ratio));
            for (const StretchOption& so : opts_) rep("{opt:" + so.name + "}", optionValue(o, so.name, so.def));
            argv.push_back(a);
        }
        if (progress) progress(0.1);
        std::string log;
        if (!runCommand(argv, cancel, &log, err)) return false;
        DecodedPcm res;
        if (!decodeAudio(outPath, &res, err, cancel)) return false;
        fitFrames(&res, stretchedFrames(in.frames(), ratio));
        *out = std::move(res);
        if (progress) progress(1);
        return true;
    }
};

} // namespace

std::unique_ptr<StretchEngine> parseExternalEngine(const std::string& text, std::string* err)
{
    auto fail = [&](const std::string& m) -> std::unique_ptr<StretchEngine> {
        if (err) *err = m;
        return nullptr;
    };
    const KvDoc doc = KvDoc::parse(text);
    auto e = std::make_unique<ExternalEngine>();
    e->id_ = trim(doc.get("id"));
    e->name_ = trim(doc.get("name"));
    e->desc_ = doc.get("description");
    e->command_ = trim(doc.get("command"));
    if (e->id_.empty()) return fail("no 'id:' line");
    for (char c : e->id_)
        if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) return fail("the id may only contain letters, digits, - _ .");
    if (e->name_.empty()) e->name_ = e->id_;
    if (e->command_.empty()) return fail("no 'command:' line");
    bool ok = true;
    e->argv_ = splitCommand(e->command_, &ok);
    if (!ok || e->argv_.empty()) return fail("the command has an unbalanced double quote");
    const std::string pitch = trim(doc.get("pitch", "preserved"));
    if (pitch != "preserved" && pitch != "follows") return fail("pitch: must be 'preserved' or 'follows'");
    e->pitch_ = pitch == "preserved";
    bool hasIn = false, hasOut = false;
    for (const std::string& a : e->argv_) {
        hasIn = hasIn || a.find("{in}") != std::string::npos;
        hasOut = hasOut || a.find("{out}") != std::string::npos;
    }
    if (!hasIn || !hasOut) return fail("the command needs {in} and {out}");
    for (const std::string& k : doc.keys()) {
        if (k.rfind("option.", 0) != 0) continue;
        StretchOption o;
        o.name = k.substr(7);
        o.label = o.name;
        const std::string spec = trim(doc.get(k));
        const size_t dots = spec.find("..");
        if (dots != std::string::npos) {
            // lo..hi or lo..hi=default
            o.kind = StretchOption::Number;
            const size_t eq = spec.find('=');
            char* end = nullptr;
            o.lo = std::strtod(spec.c_str(), &end);
            o.hi = std::strtod(spec.c_str() + dots + 2, &end);
            if (!(o.hi >= o.lo)) return fail(k + ": a number range is lo..hi");
            o.def = eq == std::string::npos ? num(o.lo) : trim(spec.substr(eq + 1));
        } else {
            o.kind = StretchOption::Enum;
            size_t b = 0;
            while (b <= spec.size()) {
                size_t p = spec.find('|', b);
                if (p == std::string::npos) p = spec.size();
                const std::string c = trim(spec.substr(b, p - b));
                if (!c.empty()) o.choices.push_back(c);
                b = p + 1;
            }
            if (o.choices.empty()) return fail(k + ": no values");
            o.def = o.choices.front();
        }
        e->opts_.push_back(o);
    }
    return e;
}

std::vector<std::string> defaultStretchDirs(const fs::path& libraryRoot)
{
    std::vector<std::string> d;
    if (!libraryRoot.empty()) d.push_back((libraryRoot / "stretchers").string());
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) d.push_back((fs::path(xdg) / "vdj-ui" / "stretchers").string());
    else if (const char* home = std::getenv("HOME")) d.push_back((fs::path(home) / ".config" / "vdj-ui" / "stretchers").string());
    return d;
}

void loadExternalEngines(const std::vector<fs::path>& dirs, StretchRegistry* reg, std::vector<std::string>* problems)
{
    for (const fs::path& dir : dirs) {
        std::error_code ec;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && e.path().extension() == ".stretch") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const fs::path& f : files) {
            std::string text, err;
            if (!readFile(f, &text, &err)) {
                if (problems) problems->push_back(f.string() + ": " + err);
                continue;
            }
            auto engine = parseExternalEngine(text, &err);
            if (!engine) {
                if (problems) problems->push_back(f.string() + ": " + err);
                continue;
            }
            const std::string id = engine->id();
            if (!reg->add(std::move(engine)) && problems) problems->push_back(f.string() + ": an engine with the id '" + id + "' already exists");
        }
    }
}

} // namespace trakcrate
