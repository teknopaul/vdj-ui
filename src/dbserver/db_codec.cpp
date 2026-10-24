#include "db_codec.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace vdj {
namespace db {

namespace {

constexpr uint8_t kTagU8 = 0x0f;
constexpr uint8_t kTagU16 = 0x10;
constexpr uint8_t kTagU32 = 0x11;
constexpr uint8_t kTagBlob = 0x14;
constexpr uint8_t kTagString = 0x26;

void putU32(std::vector<uint8_t>& o, uint32_t v)
{
    o.push_back(uint8_t(v >> 24));
    o.push_back(uint8_t(v >> 16));
    o.push_back(uint8_t(v >> 8));
    o.push_back(uint8_t(v));
}

void putNumberField(std::vector<uint8_t>& o, uint32_t v)
{
    o.push_back(kTagU32);
    putU32(o, v);
}

uint32_t getU32(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

uint8_t argTag(const Arg& a)
{
    switch (a.kind) {
    case Arg::String: return kArgString;
    case Arg::Blob:   return kArgBlob;
    default:          return kArgNumber;
    }
}

// Cursor over a buffer that reports "need more" separately from "bad data".
struct Reader {
    const uint8_t* p;
    size_t         len;
    size_t         pos = 0;
    size_t         need = 0;        // total bytes (from the start of the message) required when ok() fails with Short
    enum State { Ok, Short, Bad } state = Ok;
    size_t         badAt = 0;

    bool ok() const { return state == Ok; }
    size_t left() const { return len - pos; }

    bool want(size_t n)
    {
        if (state != Ok) return false;
        if (left() < n) { state = Short; need = pos + n; return false; }
        return true;
    }
    void bad() { if (state == Ok) { state = Bad; badAt = pos; } }

    uint32_t numberField()      // 11 + u32
    {
        if (!want(5)) return 0;
        if (p[pos] != kTagU32) { bad(); return 0; }
        uint32_t v = getU32(p + pos + 1);
        pos += 5;
        return v;
    }
    uint32_t u16Field()
    {
        if (!want(3)) return 0;
        if (p[pos] != kTagU16) { bad(); return 0; }
        uint32_t v = (uint32_t(p[pos + 1]) << 8) | p[pos + 2];
        pos += 3;
        return v;
    }
    uint32_t u8Field()
    {
        if (!want(2)) return 0;
        if (p[pos] != kTagU8) { bad(); return 0; }
        uint32_t v = p[pos + 1];
        pos += 2;
        return v;
    }
};

} // namespace

// ---------------------------------------------------------------------------------------------------------------------

Arg Arg::number(uint32_t n)
{
    Arg a;
    a.kind = Number;
    a.num = n;
    return a;
}

Arg Arg::string(const std::u16string& s)
{
    Arg a;
    a.kind = String;
    a.str = s;
    return a;
}

Arg Arg::stringUtf8(const std::string& s)
{
    return string(utf8ToUtf16(s));
}

Arg Arg::blobArg(const std::vector<uint8_t>& b)
{
    Arg a;
    a.kind = Blob;
    a.blob = b;
    return a;
}

bool Arg::operator==(const Arg& o) const
{
    if (kind != o.kind) return false;
    switch (kind) {
    case Number: return num == o.num;
    case String: return str == o.str && term == o.term;
    default:     return blob == o.blob;
    }
}

bool Message::operator==(const Message& o) const
{
    return txid == o.txid && type == o.type && args == o.args;
}

// ---------------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> encodeGreeting()
{
    std::vector<uint8_t> g;
    putNumberField(g, 1);
    return g;
}

void encodeAppend(const Message& m, std::vector<uint8_t>& o)
{
    if (m.args.size() > kMaxArgs) throw std::invalid_argument("dbserver message has more than 12 arguments");

    putNumberField(o, kMagic);
    putNumberField(o, m.txid);
    o.push_back(kTagU16);
    o.push_back(uint8_t(m.type >> 8));
    o.push_back(uint8_t(m.type));
    o.push_back(kTagU8);
    o.push_back(uint8_t(m.args.size()));

    o.push_back(kTagBlob);
    putU32(o, 12);
    for (size_t i = 0; i < kMaxArgs; ++i) o.push_back(i < m.args.size() ? argTag(m.args[i]) : kArgNone);

    for (size_t i = 0; i < m.args.size(); ++i) {
        const Arg& a = m.args[i];
        switch (a.kind) {
        case Arg::Number:
            putNumberField(o, a.num);
            break;
        case Arg::String:
            o.push_back(kTagString);
            putU32(o, uint32_t(a.str.size() + 1));
            for (char16_t c : a.str) { o.push_back(uint8_t(c >> 8)); o.push_back(uint8_t(c)); }
            o.push_back(uint8_t(a.term >> 8));
            o.push_back(uint8_t(a.term));
            break;
        case Arg::Blob:
            if (i == 0 || m.args[i - 1].kind != Arg::Number || m.args[i - 1].num != a.blob.size())
                throw std::invalid_argument("dbserver blob argument must follow a number argument holding its length");
            if (!a.blob.empty()) {
                o.push_back(kTagBlob);
                putU32(o, uint32_t(a.blob.size()));
                o.insert(o.end(), a.blob.begin(), a.blob.end());
            }
            break;
        }
    }
}

std::vector<uint8_t> encode(const Message& m)
{
    std::vector<uint8_t> o;
    encodeAppend(m, o);
    return o;
}

// ---------------------------------------------------------------------------------------------------------------------

void StreamParser::feed(const uint8_t* data, size_t len)
{
    if (failed_ || len == 0) return;
    if (pos_ > 0 && pos_ >= buf_.size() / 2) {      // drop what has been consumed
        buf_.erase(buf_.begin(), buf_.begin() + pos_);
        base_ += pos_;
        pos_ = 0;
    }
    buf_.insert(buf_.end(), data, data + len);
}

StreamParser::Status StreamParser::next(Message& out)
{
    if (failed_) return Error;
    if (buffered() < 5 || buffered() < waitFor_) return None;

    const uint8_t* p = buf_.data() + pos_;
    size_t         avail = buffered();

    // greeting: 11 00 00 00 01
    if (p[0] == kTagU32 && getU32(p + 1) == 1) {
        pos_ += 5;
        waitFor_ = 0;
        return Greeting;
    }

    Reader r{p, avail};
    if (r.numberField() != kMagic && r.ok()) r.bad();
    Message m;
    m.txid = r.numberField();
    m.type = uint16_t(r.u16Field());
    uint32_t argc = r.u8Field();
    if (r.ok() && argc > kMaxArgs) r.bad();

    uint8_t tags[kMaxArgs] = {0};
    if (r.want(5)) {
        if (p[r.pos] != kTagBlob || getU32(p + r.pos + 1) != 12) r.bad();
        else {
            r.pos += 5;
            if (r.want(12)) {
                std::memcpy(tags, p + r.pos, 12);
                r.pos += 12;
            }
        }
    }

    for (uint32_t i = 0; i < argc && r.ok(); ++i) {
        Arg a;
        switch (tags[i]) {
        case kArgNumber:
            a = Arg::number(r.numberField());
            break;
        case kArgString: {
            if (!r.want(5)) break;
            if (p[r.pos] != kTagString) { r.bad(); break; }
            uint32_t chars = getU32(p + r.pos + 1);
            if (chars == 0 || chars > kMaxFieldBytes / 2) { r.bad(); break; }
            r.pos += 5;
            if (!r.want(size_t(chars) * 2)) break;
            a.kind = Arg::String;
            a.str.reserve(chars);
            for (uint32_t c = 0; c + 1 < chars; ++c)
                a.str.push_back(char16_t((p[r.pos + 2 * c] << 8) | p[r.pos + 2 * c + 1]));
            a.term = char16_t((p[r.pos + 2 * (chars - 1)] << 8) | p[r.pos + 2 * (chars - 1) + 1]);
            r.pos += size_t(chars) * 2;     // the final char is normally the NUL terminator
            break;
        }
        case kArgBlob: {
            a.kind = Arg::Blob;
            if (m.args.empty() || m.args.back().kind != Arg::Number) { r.bad(); break; }
            if (m.args.back().num == 0) break;          // zero length: blob omitted from the wire
            if (!r.want(5)) break;
            if (p[r.pos] != kTagBlob) { r.bad(); break; }
            uint32_t n = getU32(p + r.pos + 1);
            if (n > kMaxFieldBytes) { r.bad(); break; }
            r.pos += 5;
            if (!r.want(n)) break;
            a.blob.assign(p + r.pos, p + r.pos + n);
            r.pos += n;
            break;
        }
        default:
            r.bad();
        }
        if (r.ok()) m.args.push_back(std::move(a));
    }

    if (r.state == Reader::Short) {
        waitFor_ = r.need > avail ? r.need : avail + 1;
        return None;
    }
    if (r.state == Reader::Bad) {
        failed_ = true;
        errorOffset_ = base_ + pos_ + r.badAt;
        return Error;
    }

    pos_ += r.pos;
    waitFor_ = 0;
    out = std::move(m);
    return MessageReady;
}

// ---------------------------------------------------------------------------------------------------------------------

std::string hex(const uint8_t* data, size_t len, size_t maxBytes)
{
    std::string s;
    char        b[4];
    for (size_t i = 0; i < len && i < maxBytes; ++i) {
        std::snprintf(b, sizeof b, "%02x", data[i]);
        if (i) s += ' ';
        s += b;
    }
    if (len > maxBytes) s += " ...";
    return s;
}

std::string utf16ToUtf8(const std::u16string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        uint32_t c = s[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < s.size() && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000)
            c = 0x10000 + ((c - 0xd800) << 10) + (s[++i] - 0xdc00);
        if (c < 0x80) o.push_back(char(c));
        else if (c < 0x800) { o.push_back(char(0xc0 | (c >> 6))); o.push_back(char(0x80 | (c & 0x3f))); }
        else if (c < 0x10000) {
            o.push_back(char(0xe0 | (c >> 12)));
            o.push_back(char(0x80 | ((c >> 6) & 0x3f)));
            o.push_back(char(0x80 | (c & 0x3f)));
        } else {
            o.push_back(char(0xf0 | (c >> 18)));
            o.push_back(char(0x80 | ((c >> 12) & 0x3f)));
            o.push_back(char(0x80 | ((c >> 6) & 0x3f)));
            o.push_back(char(0x80 | (c & 0x3f)));
        }
    }
    return o;
}

std::u16string utf8ToUtf16(const std::string& s)
{
    std::u16string o;
    for (size_t i = 0; i < s.size();) {
        uint8_t  b = uint8_t(s[i]);
        uint32_t c;
        size_t   n;
        if (b < 0x80) { c = b; n = 1; }
        else if ((b & 0xe0) == 0xc0) { c = b & 0x1f; n = 2; }
        else if ((b & 0xf0) == 0xe0) { c = b & 0x0f; n = 3; }
        else if ((b & 0xf8) == 0xf0) { c = b & 0x07; n = 4; }
        else { c = 0xfffd; n = 1; }
        if (i + n > s.size()) { c = 0xfffd; n = s.size() - i; }
        else for (size_t k = 1; k < n; ++k) c = (c << 6) | (uint8_t(s[i + k]) & 0x3f);
        i += n;
        if (c >= 0x10000) {
            c -= 0x10000;
            o.push_back(char16_t(0xd800 + (c >> 10)));
            o.push_back(char16_t(0xdc00 + (c & 0x3ff)));
        } else {
            o.push_back(char16_t(c));
        }
    }
    return o;
}

std::string describe(const Message& m)
{
    char head[64];
    std::snprintf(head, sizeof head, "tx=0x%08x type=0x%04x [", m.txid, m.type);
    std::string s = head;
    for (size_t i = 0; i < m.args.size(); ++i) {
        const Arg& a = m.args[i];
        char       b[48];
        if (i) s += ' ';
        std::snprintf(b, sizeof b, "#%zu ", i + 1);
        s += b;
        switch (a.kind) {
        case Arg::Number:
            std::snprintf(b, sizeof b, "0x%x", a.num);
            s += b;
            break;
        case Arg::String:
            s += '"' + utf16ToUtf8(a.str) + '"';
            break;
        case Arg::Blob:
            std::snprintf(b, sizeof b, "blob(%zu)", a.blob.size());
            s += b;
            if (!a.blob.empty()) s += ' ' + hex(a.blob.data(), a.blob.size(), 16);
            break;
        }
    }
    return s + ']';
}

// ---------------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> portQueryRequest()
{
    static const char q[] = "\x00\x00\x00\x0f" "RemoteDBServer";
    std::vector<uint8_t> v(q, q + 4 + 14);
    v.push_back(0);
    return v;
}

bool isPortQueryRequest(const uint8_t* data, size_t len)
{
    const std::vector<uint8_t> q = portQueryRequest();
    return len == q.size() && std::memcmp(data, q.data(), len) == 0;
}

std::vector<uint8_t> portQueryReply(uint16_t port)
{
    return {uint8_t(port >> 8), uint8_t(port)};
}

} // namespace db
} // namespace vdj
