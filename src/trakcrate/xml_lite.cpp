#include "xml_lite.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace trakcrate {

std::string XmlNode::attr(const std::string& key, const std::string& def) const
{
    auto it = attrs.find(key);
    return it == attrs.end() ? def : it->second;
}

const XmlNode* XmlNode::child(const std::string& childName) const
{
    for (const XmlNode& c : children)
        if (c.name == childName) return &c;
    return nullptr;
}

std::vector<const XmlNode*> XmlNode::childrenNamed(const std::string& childName) const
{
    std::vector<const XmlNode*> v;
    for (const XmlNode& c : children)
        if (c.name == childName) v.push_back(&c);
    return v;
}

namespace {

void escape(const std::string& s, std::string* out)
{
    for (unsigned char c : s) {
        switch (c) {
        case '&': *out += "&amp;"; break;
        case '<': *out += "&lt;"; break;
        case '>': *out += "&gt;"; break;
        case '"': *out += "&quot;"; break;
        case '\'': *out += "&apos;"; break;
        case '\r': *out += "&#13;"; break;
        case '\t': *out += "&#9;"; break;
        default:
            // Newlines stay literal in text; in attributes the parser would keep them too, so only other control characters are escaped.
            if (c < 0x20 && c != '\n') {
                char b[16];
                std::snprintf(b, sizeof b, "&#%d;", c);
                *out += b;
            } else {
                *out += char(c);
            }
        }
    }
}

void write(const XmlNode& n, int depth, std::string* out)
{
    const std::string pad(size_t(depth) * 2, ' ');
    *out += pad + "<" + n.name;
    for (const auto& a : n.attrs) {
        *out += " " + a.first + "=\"";
        escape(a.second, out);
        *out += "\"";
    }
    if (n.children.empty() && n.text.empty()) {
        *out += "/>\n";
    } else if (n.children.empty()) {
        *out += ">";
        escape(n.text, out);
        *out += "</" + n.name + ">\n";
    } else {
        *out += ">\n";
        for (const XmlNode& c : n.children) write(c, depth + 1, out);
        *out += pad + "</" + n.name + ">\n";
    }
}

void appendUtf8(unsigned long cp, std::string* out)
{
    if (cp < 0x80) {
        *out += char(cp);
    } else if (cp < 0x800) {
        *out += char(0xC0 | (cp >> 6));
        *out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        *out += char(0xE0 | (cp >> 12));
        *out += char(0x80 | ((cp >> 6) & 0x3F));
        *out += char(0x80 | (cp & 0x3F));
    } else {
        *out += char(0xF0 | (cp >> 18));
        *out += char(0x80 | ((cp >> 12) & 0x3F));
        *out += char(0x80 | ((cp >> 6) & 0x3F));
        *out += char(0x80 | (cp & 0x3F));
    }
}

class Parser {
public:
    Parser(const std::string& s) : s_(s) {}

    bool parseDocument(XmlNode* out)
    {
        if (s_.compare(0, 3, "\xEF\xBB\xBF") == 0) p_ = 3;
        if (!skipMisc()) return false;
        if (p_ >= s_.size() || s_[p_] != '<') return fail("expected an element");
        if (!element(out, 0)) return false;
        if (!skipMisc()) return false;
        if (p_ < s_.size()) return fail("text after the root element");
        return true;
    }

    std::string error() const { return err_; }

private:
    const std::string& s_;
    size_t p_ = 0;
    std::string err_;

    bool fail(const std::string& m)
    {
        if (!err_.empty()) return false;
        int line = 1;
        for (size_t i = 0; i < p_ && i < s_.size(); ++i)
            if (s_[i] == '\n') ++line;
        err_ = "line " + std::to_string(line) + ": " + m;
        return false;
    }

    bool at(const char* lit) const { return s_.compare(p_, std::char_traits<char>::length(lit), lit) == 0; }

    void skipWs()
    {
        while (p_ < s_.size() && std::isspace((unsigned char)s_[p_])) ++p_;
    }

    // Whitespace, comments, <?...?> and <!DOCTYPE ...> (skipped, never interpreted).
    bool skipMisc()
    {
        for (;;) {
            skipWs();
            if (at("<!--")) {
                const size_t e = s_.find("-->", p_ + 4);
                if (e == std::string::npos) return fail("unterminated comment");
                p_ = e + 3;
            } else if (at("<?")) {
                const size_t e = s_.find("?>", p_ + 2);
                if (e == std::string::npos) return fail("unterminated <? ?>");
                p_ = e + 2;
            } else if (at("<!")) {
                const size_t e = s_.find('>', p_ + 2);
                if (e == std::string::npos) return fail("unterminated <! >");
                p_ = e + 1;
            } else {
                return true;
            }
        }
    }

    static bool nameChar(unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == ':' || c >= 0x80; }

    bool name(std::string* out)
    {
        const size_t b = p_;
        while (p_ < s_.size() && nameChar((unsigned char)s_[p_])) ++p_;
        if (p_ == b || std::isdigit((unsigned char)s_[b]) || s_[b] == '-' || s_[b] == '.') return fail("expected a name");
        *out = s_.substr(b, p_ - b);
        return true;
    }

    // Decodes entities of raw[0..len) into out.
    bool decode(size_t from, size_t to, std::string* out)
    {
        for (size_t i = from; i < to; ++i) {
            const char c = s_[i];
            if (c != '&') {
                *out += c;
                continue;
            }
            const size_t semi = s_.find(';', i);
            if (semi == std::string::npos || semi >= to || semi - i > 12) {
                p_ = i;
                return fail("bad entity");
            }
            const std::string e = s_.substr(i + 1, semi - i - 1);
            if (e == "amp") *out += '&';
            else if (e == "lt") *out += '<';
            else if (e == "gt") *out += '>';
            else if (e == "quot") *out += '"';
            else if (e == "apos") *out += '\'';
            else if (e.size() > 1 && e[0] == '#') {
                char* end = nullptr;
                const bool hex = e[1] == 'x' || e[1] == 'X';
                const std::string digits = e.substr(hex ? 2 : 1);
                const unsigned long cp = digits.empty() ? 0xFFFFFFFFul : std::strtoul(digits.c_str(), &end, hex ? 16 : 10);
                if (digits.empty() || *end || cp > 0x10FFFF || cp == 0) {
                    p_ = i;
                    return fail("bad character reference");
                }
                appendUtf8(cp, out);
            } else {
                p_ = i;
                return fail("unknown entity &" + e + ";");
            }
            i = semi;
        }
        return true;
    }

    bool element(XmlNode* n, int depth)
    {
        if (depth > 200) return fail("elements nested too deep");
        ++p_;                                   // '<'
        if (!name(&n->name)) return false;
        for (;;) {
            skipWs();
            if (p_ >= s_.size()) return fail("unexpected end inside a tag");
            if (s_[p_] == '/') {
                if (!at("/>")) return fail("expected />");
                p_ += 2;
                return true;
            }
            if (s_[p_] == '>') {
                ++p_;
                break;
            }
            std::string key;
            if (!name(&key)) return false;
            skipWs();
            if (p_ >= s_.size() || s_[p_] != '=') return fail("expected = after attribute " + key);
            ++p_;
            skipWs();
            if (p_ >= s_.size() || (s_[p_] != '"' && s_[p_] != '\'')) return fail("attribute values need quotes");
            const char q = s_[p_++];
            const size_t e = s_.find(q, p_);
            if (e == std::string::npos) return fail("unterminated attribute value");
            std::string val;
            if (!decode(p_, e, &val)) return false;
            if (n->attrs.count(key)) return fail("duplicate attribute " + key);
            n->attrs[key] = val;
            p_ = e + 1;
        }
        // content
        std::string text;
        for (;;) {
            const size_t lt = s_.find('<', p_);
            if (lt == std::string::npos) return fail("element <" + n->name + "> is not closed");
            if (!decode(p_, lt, &text)) return false;
            p_ = lt;
            if (at("<!--")) {
                const size_t e = s_.find("-->", p_ + 4);
                if (e == std::string::npos) return fail("unterminated comment");
                p_ = e + 3;
            } else if (at("</")) {
                p_ += 2;
                std::string close;
                if (!name(&close)) return false;
                skipWs();
                if (p_ >= s_.size() || s_[p_] != '>') return fail("expected > in the closing tag");
                ++p_;
                if (close != n->name) return fail("</" + close + "> closes <" + n->name + ">");
                break;
            } else if (at("<?")) {
                const size_t e = s_.find("?>", p_ + 2);
                if (e == std::string::npos) return fail("unterminated <? ?>");
                p_ = e + 2;
            } else {
                XmlNode c;
                if (!element(&c, depth + 1)) return false;
                n->children.push_back(std::move(c));
            }
        }
        if (n->children.empty()) n->text = text;
        return true;
    }
};

} // namespace

std::string xmlWrite(const XmlNode& root)
{
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    write(root, 0, &out);
    return out;
}

bool xmlParse(const std::string& text, XmlNode* out, std::string* err)
{
    Parser p(text);
    XmlNode n;
    if (!p.parseDocument(&n)) {
        if (err) *err = p.error();
        return false;
    }
    *out = std::move(n);
    return true;
}

} // namespace trakcrate
