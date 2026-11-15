#include "mix_file.h"

#include "natsort.h"
#include "xml_lite.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace trakcrate {

namespace {

std::string num(double v)
{
    char b[40];
    std::snprintf(b, sizeof b, "%.15g", v);
    if (std::strtod(b, nullptr) != v) std::snprintf(b, sizeof b, "%.17g", v);
    return b;
}

double getNum(const XmlNode& n, const char* key, double def)
{
    const auto it = n.attrs.find(key);
    if (it == n.attrs.end()) return def;
    char* end = nullptr;
    const double v = std::strtod(it->second.c_str(), &end);
    return (end == it->second.c_str() || !std::isfinite(v)) ? def : v;
}

bool getBool(const XmlNode& n, const char* key, bool def)
{
    const auto it = n.attrs.find(key);
    return it == n.attrs.end() ? def : (it->second == "1" || it->second == "true");
}

const char* const kAlgos[] = {"cut", "cross", "fadeout", "tail-overlap", "early-cut"};
const char* algoName(EditAlgo a) { return kAlgos[int(a)]; }
EditAlgo algoFrom(const std::string& s, EditAlgo def)
{
    for (int i = 0; i < 5; ++i)
        if (s == kAlgos[i]) return EditAlgo(i);
    return def;
}

XmlNode stretchNode(const StretchChoice& c)
{
    XmlNode n;
    n.name = "stretch";
    n.attrs["engine"] = c.engine;
    for (const auto& o : c.options) {
        XmlNode on;
        on.name = "option";
        on.attrs["name"] = o.first;
        on.attrs["value"] = o.second;
        n.children.push_back(on);
    }
    return n;
}

StretchChoice stretchFrom(const XmlNode& n)
{
    StretchChoice c;
    c.engine = n.attr("engine");
    for (const XmlNode* o : n.childrenNamed("option")) c.options[o->attr("name")] = o->attr("value");
    return c;
}

XmlNode curveNode(const char* name, const Curve& c)
{
    XmlNode n;
    n.name = "curve";
    n.attrs["name"] = name;
    for (const CurvePoint& p : c.points) {
        XmlNode pn;
        pn.name = "p";
        pn.attrs["b"] = num(p.beat);
        pn.attrs["v"] = num(p.value);
        n.children.push_back(pn);
    }
    return n;
}

void curveFrom(const XmlNode& n, Curve* c)
{
    for (const XmlNode* p : n.childrenNamed("p")) setPoint(c, getNum(*p, "b", 0), getNum(*p, "v", 100));
}

std::string fileText(const fs::path& file, const fs::path& root)
{
    if (file.is_absolute() && !root.empty()) {
        const fs::path rel = file.lexically_relative(root);
        if (!rel.empty() && *rel.begin() != "..") return rel.generic_string();
    }
    return file.generic_string();
}

XmlNode regionNode(const EditRegion& r)
{
    XmlNode n;
    n.name = "region";
    n.attrs["kind"] = r.kind == EditKind::Cut ? "cut" : "loop";
    n.attrs["start"] = std::to_string(r.startBeat);
    n.attrs["len"] = std::to_string(r.lenBeats);
    n.attrs["algo"] = algoName(r.algo);
    n.attrs["param"] = num(r.algoParam);
    n.attrs["times"] = std::to_string(r.loopTimes);
    n.attrs["earlyShift"] = std::to_string(r.earlyShift);
    n.attrs["earlyAfter"] = r.earlyAfter ? "1" : "0";
    n.attrs["earlyJoinAlgo"] = algoName(r.earlyJoinAlgo);
    n.attrs["earlyJoinParam"] = num(r.earlyJoinParam);
    return n;
}

EditRegion regionFrom(const XmlNode& n)
{
    EditRegion r;
    r.kind = n.attr("kind") == "loop" ? EditKind::Loop : EditKind::Cut;
    r.startBeat = int(getNum(n, "start", 0));
    r.lenBeats = int(getNum(n, "len", 4));
    r.algo = algoFrom(n.attr("algo"), EditAlgo::Cut);
    r.algoParam = getNum(n, "param", 10);
    r.loopTimes = int(getNum(n, "times", 2));
    r.earlyShift = int(getNum(n, "earlyShift", 1));
    r.earlyAfter = getBool(n, "earlyAfter", false);
    r.earlyJoinAlgo = algoFrom(n.attr("earlyJoinAlgo"), EditAlgo::Cut);
    r.earlyJoinParam = getNum(n, "earlyJoinParam", 10);
    return r;
}

XmlNode filterNode(const MixFilter& f)
{
    XmlNode n;
    n.name = "filter";
    n.attrs["on"] = f.on ? "1" : "0";
    if (f.kind == MixFilterKind::BuiltinCompressor) {
        n.attrs["kind"] = "builtin-compressor";
        for (const auto& p : f.params) n.attrs[p.first] = num(p.second);
    } else {
        n.attrs["kind"] = "ladspa";
        n.attrs["plugin"] = f.plugin;
        for (const auto& p : f.params) {
            XmlNode c;
            c.name = "control";
            c.attrs["name"] = p.first;
            c.attrs["value"] = num(p.second);
            n.children.push_back(c);
        }
    }
    return n;
}

bool filterFrom(const XmlNode& n, MixFilter* f)
{
    f->on = getBool(n, "on", true);
    const std::string kind = n.attr("kind");
    if (kind == "builtin-compressor") {
        f->kind = MixFilterKind::BuiltinCompressor;
        f->params = defaultFilters().front().params;
        for (auto& p : f->params) p.second = getNum(n, p.first.c_str(), p.second);
        return true;
    }
    if (kind == "ladspa") {
        f->kind = MixFilterKind::Ladspa;
        f->plugin = n.attr("plugin");
        for (const XmlNode* c : n.childrenNamed("control")) f->params[c->attr("name")] = getNum(*c, "value", 0);
        return true;
    }
    return false;
}

void addText(XmlNode* parent, const char* name, const std::string& text)
{
    XmlNode n;
    n.name = name;
    n.text = text;
    parent->children.push_back(n);
}

} // namespace

std::string mixToXml(const Mix& mix, const fs::path& root)
{
    XmlNode m;
    m.name = "mix";
    m.attrs["version"] = std::to_string(kMixFileVersion);
    m.attrs["rate"] = std::to_string(kEditRate);
    addText(&m, "name", mix.name);
    addText(&m, "author", mix.author);
    addText(&m, "image", mix.image);
    addText(&m, "playlist", mix.playlist);
    XmlNode tempo;
    tempo.name = "tempo";
    tempo.attrs["masterBpm"] = num(mix.masterBpm);
    m.children.push_back(tempo);
    if (!mix.stretch.empty()) m.children.push_back(stretchNode(mix.stretch));
    XmlNode eq;
    eq.name = "eq";
    eq.attrs["bassHz"] = num(mix.bassHz);
    eq.attrs["highHz"] = num(mix.highHz);
    eq.attrs["slopeDb"] = std::to_string(mix.slopeDb);
    m.children.push_back(eq);
    XmlNode filters;
    filters.name = "filters";
    for (const MixFilter& f : mix.filters) filters.children.push_back(filterNode(f));
    m.children.push_back(filters);

    XmlNode tracks;
    tracks.name = "tracks";
    for (size_t i = 0; i < mix.tracks.size(); ++i) {
        const MixTrack& t = mix.tracks[i];
        XmlNode tn;
        tn.name = "track";
        tn.attrs["file"] = fileText(t.file, root);
        tn.attrs["trim"] = num(t.trim);
        tn.attrs["gridNudgeMs"] = num(t.gridNudgeMs);
        if (t.stretchOff) tn.attrs["stretch"] = "off";
        else if (!t.stretch.empty()) tn.children.push_back(stretchNode(t.stretch));
        if (!t.regions.empty()) {
            XmlNode rs;
            rs.name = "regions";
            for (const EditRegion& r : t.regions) rs.children.push_back(regionNode(r));
            tn.children.push_back(rs);
        }
        tn.children.push_back(curveNode("volume", t.volume));
        tn.children.push_back(curveNode("bass", t.bass));
        tn.children.push_back(curveNode("high", t.high));
        if (i + 1 < mix.tracks.size()) {
            XmlNode nx;
            nx.name = "next";
            nx.attrs["outStart"] = num(t.next.outStartBeat);
            nx.attrs["inStart"] = num(t.next.inStartBeat);
            nx.attrs["len"] = num(t.next.lenBeats);
            nx.attrs["type"] = mixTypeName(t.next.type);
            tn.children.push_back(nx);
        }
        tracks.children.push_back(tn);
    }
    m.children.push_back(tracks);
    return xmlWrite(m);
}

bool mixFromXml(const std::string& xml, const fs::path& root, Mix* out, std::vector<std::string>* warnings, std::string* err)
{
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    XmlNode doc;
    std::string perr;
    if (!xmlParse(xml, &doc, &perr)) return fail("not a valid mix file: " + perr);
    if (doc.name != "mix") return fail("not a mix file (the root element is <" + doc.name + ">)");
    const int version = int(getNum(doc, "version", 1));
    if (version > kMixFileVersion) return fail("this mix was made by a newer version (file version " + std::to_string(version) + ")");

    Mix mix;
    mix.name = doc.child("name") ? doc.child("name")->text : "";
    mix.author = doc.child("author") ? doc.child("author")->text : "";
    mix.image = doc.child("image") ? doc.child("image")->text : "";
    mix.playlist = doc.child("playlist") ? doc.child("playlist")->text : "";
    if (const XmlNode* t = doc.child("tempo")) mix.masterBpm = getNum(*t, "masterBpm", mix.masterBpm);
    if (const XmlNode* s = doc.child("stretch")) mix.stretch = stretchFrom(*s);
    if (const XmlNode* e = doc.child("eq")) {
        mix.bassHz = getNum(*e, "bassHz", mix.bassHz);
        mix.highHz = getNum(*e, "highHz", mix.highHz);
        mix.slopeDb = int(getNum(*e, "slopeDb", mix.slopeDb));
    }
    if (const XmlNode* fl = doc.child("filters")) {
        mix.filters.clear();                                   // a present <filters> is the whole list, even when empty
        for (const XmlNode* fn : fl->childrenNamed("filter")) {
            MixFilter f;
            if (filterFrom(*fn, &f)) mix.filters.push_back(f);
            else if (warnings) warnings->push_back("unknown filter kind '" + fn->attr("kind") + "' ignored");
        }
    }
    if (const XmlNode* ts = doc.child("tracks")) {
        for (const XmlNode* tn : ts->childrenNamed("track")) {
            MixTrack t;
            const std::string f = tn->attr("file");
            t.file = fs::path(f).is_absolute() ? fs::path(f) : root / f;
            t.trim = getNum(*tn, "trim", 100);
            t.gridNudgeMs = getNum(*tn, "gridNudgeMs", 0);
            t.stretchOff = tn->attr("stretch") == "off";
            if (const XmlNode* s = tn->child("stretch")) t.stretch = stretchFrom(*s);
            if (const XmlNode* rs = tn->child("regions"))
                for (const XmlNode* r : rs->childrenNamed("region")) t.regions.push_back(regionFrom(*r));
            for (const XmlNode* c : tn->childrenNamed("curve")) {
                const std::string cn = c->attr("name");
                if (cn == "volume") curveFrom(*c, &t.volume);
                else if (cn == "bass") curveFrom(*c, &t.bass);
                else if (cn == "high") curveFrom(*c, &t.high);
            }
            if (const XmlNode* nx = tn->child("next")) {
                t.next.outStartBeat = getNum(*nx, "outStart", 0);
                t.next.inStartBeat = getNum(*nx, "inStart", 0);
                t.next.lenBeats = getNum(*nx, "len", 32);
                if (!mixTypeFromName(nx->attr("type"), &t.next.type) && warnings)
                    warnings->push_back("unknown mix type '" + nx->attr("type") + "' replaced by cross-fade");
            }
            std::error_code ec;
            if (f.empty() || !fs::exists(t.file, ec)) {
                t.missing = true;
                if (warnings) warnings->push_back("track " + std::to_string(mix.tracks.size() + 1) + " is missing: " + f);
            }
            mix.tracks.push_back(t);
        }
    }
    *out = std::move(mix);
    return true;
}

bool isSafeMixName(const std::string& relName)
{
    if (relName.empty() || relName.front() == '/' || relName.back() == '/') return false;
    size_t b = 0;
    while (b <= relName.size()) {
        size_t e = relName.find('/', b);
        if (e == std::string::npos) e = relName.size();
        const std::string part = relName.substr(b, e - b);
        if (!isSafeName(part) || part.front() == '.') return false;
        b = e + 1;
    }
    return true;
}

bool saveMix(const Library& lib, const std::string& relName, const Mix& mix, std::string* err)
{
    if (!isSafeMixName(relName)) {
        if (err) *err = "bad mix name: " + relName;
        return false;
    }
    const fs::path file = lib.mixesDir() / (relName + ".mix");
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec) {
        if (err) *err = "cannot create " + file.parent_path().string() + ": " + ec.message();
        return false;
    }
    return atomicWrite(file, mixToXml(mix, lib.root()), err);
}

bool loadMix(const fs::path& file, const fs::path& root, Mix* out, std::vector<std::string>* warnings, std::string* err)
{
    std::string xml;
    if (!readFile(file, &xml, err)) return false;
    return mixFromXml(xml, root, out, warnings, err);
}

static void listDir(const fs::path& dir, MixTreeEntry* into)
{
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        const std::string n = e.path().filename().string();
        if (n.empty() || n[0] == '.') continue;
        MixTreeEntry c;
        c.path = e.path();
        if (e.is_directory(ec)) {
            c.name = n;
            c.folder = true;
            listDir(e.path(), &c);
        } else if (e.is_regular_file(ec) && e.path().extension() == ".mix") {
            c.name = e.path().stem().string();
        } else {
            continue;
        }
        into->children.push_back(std::move(c));
    }
    std::sort(into->children.begin(), into->children.end(), [](const MixTreeEntry& a, const MixTreeEntry& b) {
        if (a.folder != b.folder) return a.folder;
        return naturalLess(a.name, b.name);
    });
}

MixTreeEntry listMixes(const Library& lib)
{
    MixTreeEntry root;
    root.name = "mixes";
    root.path = lib.mixesDir();
    root.folder = true;
    listDir(root.path, &root);
    return root;
}

std::string uniqueMixName(const Library& lib, const std::string& base)
{
    std::error_code ec;
    for (int i = 1; i < 100000; ++i) {
        const std::string n = i == 1 ? base : base + " " + std::to_string(i);
        if (!fs::exists(lib.mixesDir() / (n + ".mix"), ec)) return n;
    }
    return base;
}

} // namespace trakcrate
