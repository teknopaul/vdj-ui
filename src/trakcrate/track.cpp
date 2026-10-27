#include "track.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace trakcrate {

// ---- helpers ------------------------------------------------------------------------------------------------------------------------

static std::string trim(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

static std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

static std::string escapeValue(const std::string& v)
{
    std::string o;
    for (char c : v) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') continue;
        else o += c;
    }
    return o;
}

static std::string unescapeValue(const std::string& v)
{
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            ++i;
            o += v[i] == 'n' ? '\n' : v[i];
        } else {
            o += v[i];
        }
    }
    return o;
}

// "12", "12.5", "33.0": at most 4 decimals, trailing zeros trimmed but one decimal kept. NaN is written as "-".
static std::string fmtNum(double v)
{
    if (std::isnan(v)) return "-";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    std::string s = buf;
    size_t dot = s.find('.');
    size_t last = s.find_last_not_of('0');
    if (last != std::string::npos && last > dot) s.resize(last + 1);
    else s.resize(dot + 2);                                        // keep "33.0"
    return s;
}

static double parseNum(const std::string& s)
{
    if (s.empty() || s == "-") return std::nan("");
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    return end == s.c_str() ? std::nan("") : v;
}

// Tab separated when the line has a tab, otherwise whitespace separated (hand written files; only works if no field contains a space).
static std::vector<std::string> splitFields(const std::string& line)
{
    std::vector<std::string> f;
    if (line.find('\t') != std::string::npos) {
        size_t start = 0;
        for (;;) {
            size_t t = line.find('\t', start);
            f.push_back(line.substr(start, t == std::string::npos ? std::string::npos : t - start));
            if (t == std::string::npos) break;
            start = t + 1;
        }
    } else {
        std::istringstream in(line);
        std::string w;
        while (in >> w) f.push_back(w);
    }
    return f;
}

// ---- KvDoc --------------------------------------------------------------------------------------------------------------------------

KvDoc KvDoc::parse(const std::string& text)
{
    KvDoc d;
    for (const std::string& line : splitLines(text)) {
        Line l;
        std::string t = trim(line);
        size_t colon = t.find(':');
        if (t.empty() || t[0] == '#' || colon == std::string::npos || colon == 0) {
            l.raw = line;
        } else {
            l.isKv = true;
            l.key = trim(t.substr(0, colon));
            l.value = unescapeValue(trim(t.substr(colon + 1)));
        }
        d.lines_.push_back(l);
    }
    return d;
}

std::string KvDoc::serialise() const
{
    std::string out;
    for (const Line& l : lines_) {
        if (l.isKv) out += l.key + ": " + escapeValue(l.value) + "\n";
        else out += l.raw + "\n";
    }
    return out;
}

bool KvDoc::has(const std::string& key) const
{
    for (const Line& l : lines_)
        if (l.isKv && l.key == key) return true;
    return false;
}

std::string KvDoc::get(const std::string& key, const std::string& def) const
{
    for (const Line& l : lines_)
        if (l.isKv && l.key == key) return l.value;
    return def;
}

long long KvDoc::getInt(const std::string& key, long long def) const
{
    std::string v = get(key);
    if (v.empty()) return def;
    char* end = nullptr;
    long long n = std::strtoll(v.c_str(), &end, 10);
    return end == v.c_str() ? def : n;
}

double KvDoc::getDouble(const std::string& key, double def) const
{
    std::string v = get(key);
    if (v.empty()) return def;
    char* end = nullptr;
    double n = std::strtod(v.c_str(), &end);
    return end == v.c_str() ? def : n;
}

void KvDoc::set(const std::string& key, const std::string& value)
{
    for (Line& l : lines_) {
        if (l.isKv && l.key == key) { l.value = value; return; }
    }
    Line l;
    l.isKv = true;
    l.key = key;
    l.value = value;
    lines_.push_back(l);
}

void KvDoc::setInt(const std::string& key, long long value) { set(key, std::to_string(value)); }

void KvDoc::remove(const std::string& key)
{
    lines_.erase(std::remove_if(lines_.begin(), lines_.end(), [&](const Line& l) { return l.isKv && l.key == key; }), lines_.end());
}

std::vector<std::string> KvDoc::keys() const
{
    std::vector<std::string> k;
    for (const Line& l : lines_)
        if (l.isKv) k.push_back(l.key);
    return k;
}

void KvDoc::addComment(const std::string& text)
{
    Line l;
    l.raw = "# " + text;
    lines_.push_back(l);
}

// ---- source.txt and beatgrid.txt ----------------------------------------------------------------------------------------------------

std::string origin(const KvDoc& source, const std::string& group) { return source.get("origin." + group, "generated"); }
void setOrigin(KvDoc& source, const std::string& group, const std::string& value) { source.set("origin." + group, value); }

BeatGrid beatGridFrom(const KvDoc& doc)
{
    BeatGrid g;
    g.bpm = doc.getDouble("bpm", 0);
    g.firstBeatUs = doc.getInt("first_beat_us", 0);
    return g;
}

void setBeatGrid(KvDoc& doc, const BeatGrid& g)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", g.bpm);
    doc.set("bpm", buf);
    doc.setInt("first_beat_us", g.firstBeatUs);
}

// ---- cuemarks.txt -------------------------------------------------------------------------------------------------------------------

CuemarkFile parseCuemarks(const std::string& text)
{
    CuemarkFile f;
    bool seenData = false;
    for (const std::string& line : splitLines(text)) {
        std::string t = trim(line);
        if (t.empty()) continue;
        if (t[0] == '#') {
            if (!seenData) f.comments.push_back(line);
            continue;
        }
        seenData = true;
        std::vector<std::string> c = splitFields(line);
        if (c.size() < 2) continue;                                  // not a mark: ignore rather than fail
        Cuemark m;
        m.beat = parseNum(c[0]);
        m.ms = parseNum(c[1]);
        if ((std::isnan(m.beat) && c[0] != "-") || std::isnan(m.ms)) continue;      // not numbers: not a mark
        if (c.size() > 2) m.label = c[2];
        if (c.size() > 3 && !c[3].empty() && c[3] != "-") {
            char h = char(std::toupper(static_cast<unsigned char>(c[3][0])));
            if (h >= 'A' && h <= 'C') m.hot = h;
        }
        if (c.size() > 4 && c[4] != "-") m.colour = c[4];
        for (size_t i = 5; i < c.size(); ++i) m.extra.push_back(c[i]);
        f.marks.push_back(m);
    }
    return f;
}

std::string serialiseCuemarks(const CuemarkFile& f)
{
    std::string out;
    for (const std::string& c : f.comments) out += c + "\n";
    for (const Cuemark& m : f.marks) {
        out += fmtNum(m.beat) + "\t" + fmtNum(m.ms) + "\t" + m.label + "\t";
        out += m.hot ? std::string(1, m.hot) : std::string("-");
        out += "\t" + (m.colour.empty() ? std::string("-") : m.colour);
        for (const std::string& e : m.extra) out += "\t" + e;
        out += "\n";
    }
    return out;
}

// ---- vocals.txt ---------------------------------------------------------------------------------------------------------------------

VocalsFile parseVocals(const std::string& text)
{
    VocalsFile f;
    bool seenData = false;
    for (const std::string& line : splitLines(text)) {
        std::string t = trim(line);
        if (t.empty()) continue;
        if (t[0] == '#') {
            if (!seenData) f.comments.push_back(line);
            continue;
        }
        seenData = true;
        std::vector<std::string> c = splitFields(line);
        if (c.size() < 4) continue;
        VocalRegion r;
        r.startBeat = parseNum(c[0]);
        r.endBeat = parseNum(c[1]);
        r.startMs = parseNum(c[2]);
        r.endMs = parseNum(c[3]);
        if (std::isnan(r.startMs) || std::isnan(r.endMs)) continue;
        if (c.size() > 4) r.kind = c[4];
        if (c.size() > 5) { double v = parseNum(c[5]); r.confidence = std::isnan(v) ? 1 : v; }
        for (size_t i = 6; i < c.size(); ++i) r.extra.push_back(c[i]);
        f.regions.push_back(r);
    }
    return f;
}

std::string serialiseVocals(const VocalsFile& f)
{
    std::string out;
    for (const std::string& c : f.comments) out += c + "\n";
    for (const VocalRegion& r : f.regions) {
        out += fmtNum(r.startBeat) + "\t" + fmtNum(r.endBeat) + "\t" + fmtNum(r.startMs) + "\t" + fmtNum(r.endMs) + "\t" + r.kind + "\t" +
               fmtNum(r.confidence);
        for (const std::string& e : r.extra) out += "\t" + e;
        out += "\n";
    }
    return out;
}

// ---- Track --------------------------------------------------------------------------------------------------------------------------

static bool readIfExists(const fs::path& p, std::string* text, bool* exists, std::string* err)
{
    std::error_code ec;
    *exists = fs::is_regular_file(p, ec);
    if (!*exists) return true;
    return readFile(p, text, err);
}

bool loadTrack(const fs::path& dir, Track* out, std::string* err)
{
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        if (err) *err = "not a track folder: " + dir.string();
        return false;
    }
    Track t;
    t.dir = dir;
    std::string text;
    bool exists = false;
    if (!readIfExists(dir / "meta.txt", &text, &exists, err)) return false;
    if (exists) t.meta = KvDoc::parse(text);
    if (!readIfExists(dir / "source.txt", &text, &exists, err)) return false;
    if (exists) t.source = KvDoc::parse(text);
    if (!readIfExists(dir / "beatgrid.txt", &text, &t.hasGrid, err)) return false;
    if (t.hasGrid) t.grid = KvDoc::parse(text);
    if (!readIfExists(dir / "cuemarks.txt", &text, &t.hasCuemarks, err)) return false;
    if (t.hasCuemarks) t.cuemarks = parseCuemarks(text);
    if (!readIfExists(dir / "vocals.txt", &text, &t.hasVocals, err)) return false;
    if (t.hasVocals) t.vocals = parseVocals(text);
    *out = t;
    return true;
}

bool saveMeta(const Track& t, std::string* err) { return atomicWrite(t.dir / "meta.txt", t.meta.serialise(), err); }
bool saveSource(const Track& t, std::string* err) { return atomicWrite(t.dir / "source.txt", t.source.serialise(), err); }

bool saveBeatGrid(const Track& t, std::string* err)
{
    if (!t.hasGrid) return true;
    return atomicWrite(t.dir / "beatgrid.txt", t.grid.serialise(), err);
}

bool saveCuemarks(const Track& t, std::string* err)
{
    if (!t.hasCuemarks) return true;
    return atomicWrite(t.dir / "cuemarks.txt", serialiseCuemarks(t.cuemarks), err);
}

bool saveVocals(const Track& t, std::string* err)
{
    if (!t.hasVocals) return true;
    return atomicWrite(t.dir / "vocals.txt", serialiseVocals(t.vocals), err);
}

fs::path audioFileOf(const fs::path& trackDir)
{
    std::error_code ec;
    std::vector<fs::path> found;
    for (const fs::directory_entry& e : fs::directory_iterator(trackDir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (ext == ".mp3" || ext == ".wav") found.push_back(e.path());
    }
    if (found.empty()) return fs::path();
    std::sort(found.begin(), found.end());
    return found.front();
}

} // namespace trakcrate
