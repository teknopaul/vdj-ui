#include "ext_pdb.h"

#include "pdb_writer.h"

#include <algorithm>
#include <map>

namespace trakcrate {

namespace {

constexpr uint32_t kTagTable = 3, kTrackTagTable = 4;
constexpr uint32_t kEmptyEntry = 0x03ffffff;               // what an index page holds where the first data page of an empty table would be

void put32(std::string& s, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) s[at + size_t(i)] = char((v >> (8 * i)) & 0xFF);
}

PdbTable* findTable(PdbFile& f, uint32_t type)
{
    for (PdbTable& t : f.tables)
        if (t.type == type) return &t;
    return nullptr;
}

// The pages of a table after its index page, in chain order, up to its last page.
std::vector<uint32_t> dataPages(const PdbFile& f, const PdbTable& t)
{
    std::vector<uint32_t> v;
    uint32_t n = t.firstPage;
    for (size_t guard = 0; n != t.lastPage && guard < f.pages.size(); ++guard) {
        if (n == 0 || n >= f.pages.size()) break;
        n = f.pages[n].next;
        if (n == 0 || n >= f.pages.size()) break;
        v.push_back(n);
    }
    return v;
}

} // namespace

bool extPdbStats(const std::string& bytes, ExtPdbStats* out, std::string* err)
{
    PdbFile f;
    if (!parsePdb(bytes, &f, err)) return false;
    const PdbTable* tags = nullptr;
    for (const PdbTable& t : f.tables) if (t.type == kTagTable) tags = &t;
    if (!tags) {
        if (err) *err = "no tag table: this is not an exportExt.pdb";
        return false;
    }
    ExtPdbStats s;
    for (uint32_t n : dataPages(f, *tags))
        for (const PdbRow& r : f.pages[n].rows) (r.present ? s.tags : s.deletedTags)++;
    for (const PdbRow* r : tableRows(f, kTrackTagTable)) { (void)r; ++s.trackLinks; }
    *out = s;
    return true;
}

namespace {

uint32_t rd32(const std::string& s, size_t at) { return uint32_t(uint8_t(s[at])) | uint32_t(uint8_t(s[at + 1])) << 8 | uint32_t(uint8_t(s[at + 2])) << 16 | uint32_t(uint8_t(s[at + 3])) << 24; }

void appendUtf8(std::string& o, uint32_t c)
{
    if (c < 0x80) o += char(c);
    else if (c < 0x800) { o += char(0xC0 | (c >> 6)); o += char(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { o += char(0xE0 | (c >> 12)); o += char(0x80 | ((c >> 6) & 0x3F)); o += char(0x80 | (c & 0x3F)); }
    else { o += char(0xF0 | (c >> 18)); o += char(0x80 | ((c >> 12) & 0x3F)); o += char(0x80 | ((c >> 6) & 0x3F)); o += char(0x80 | (c & 0x3F)); }
}

// A DeviceSQL string at `at` inside the row: a short ASCII string (flag byte with the low bit set), or a long one (0x40 ASCII, 0x90 UTF-16LE:
// a u16 length of the whole string at +1, the text at +4). False when it runs outside the row.
bool deviceSqlString(const std::string& row, size_t at, std::string* out)
{
    if (at >= row.size()) return false;
    const uint8_t flag = uint8_t(row[at]);
    if (flag & 1) {
        const size_t n = (flag >> 1) - 1;
        if ((flag >> 1) == 0 || at + 1 + n > row.size()) return false;
        *out = row.substr(at + 1, n);
        return true;
    }
    if (at + 4 > row.size()) return false;
    const size_t total = uint8_t(row[at + 1]) | size_t(uint8_t(row[at + 2])) << 8;
    if (total < 4 || at + total > row.size()) return false;
    if (flag == 0x40) { *out = row.substr(at + 4, total - 4); return true; }
    if (flag != 0x90) return false;
    std::string o;
    for (size_t i = at + 4; i + 1 < at + total; i += 2) {
        uint32_t c = uint8_t(row[i]) | uint32_t(uint8_t(row[i + 1])) << 8;
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < at + total) {
            const uint32_t lo = uint8_t(row[i + 2]) | uint32_t(uint8_t(row[i + 3])) << 8;
            if (lo >= 0xDC00 && lo < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); i += 2; }
        }
        appendUtf8(o, c);
    }
    *out = o;
    return true;
}

} // namespace

bool readExtTags(const std::string& bytes, std::vector<ExtTag>* out, std::string* err)
{
    PdbFile f;
    if (!parsePdb(bytes, &f, err)) return false;
    bool haveTable = false;
    for (const PdbTable& t : f.tables) haveTable |= t.type == kTagTable;
    if (!haveTable) {
        if (err) *err = "no tag table: this is not an exportExt.pdb";
        return false;
    }
    std::vector<ExtTag> all;
    for (const PdbRow* r : tableRows(f, kTagTable)) {
        const std::string& b = r->bytes;
        if (b.size() < 0x1f) continue;
        ExtTag t;
        t.parent = rd32(b, 0x0c);
        t.position = rd32(b, 0x10);
        t.id = rd32(b, 0x14);
        t.category = rd32(b, 0x18) != 0;
        const uint16_t subtype = uint16_t(uint8_t(b[0]) | uint8_t(b[1]) << 8);
        // the name is the first of two strings; the 8 bit offsets (subtype 0x0680) are the bytes at 0x1d and 0x1e (doc/my-tags.md); the 16 bit
        // variant has not been seen, its names are not read
        if (subtype != 0x0680 || !deviceSqlString(b, uint8_t(b[0x1d]), &t.name)) t.name = "?";
        all.push_back(std::move(t));
    }
    auto byPosition = [](const ExtTag& a, const ExtTag& b) { return a.position != b.position ? a.position < b.position : a.id < b.id; };
    std::vector<ExtTag> cats, tags;
    for (ExtTag& t : all) (t.category ? cats : tags).push_back(std::move(t));
    std::sort(cats.begin(), cats.end(), byPosition);
    std::sort(tags.begin(), tags.end(), byPosition);
    std::vector<ExtTag> res;
    std::vector<bool> used(tags.size(), false);
    for (const ExtTag& c : cats) {
        res.push_back(c);
        for (size_t i = 0; i < tags.size(); ++i)
            if (!used[i] && tags[i].parent == c.id) { res.push_back(tags[i]); used[i] = true; }
    }
    for (size_t i = 0; i < tags.size(); ++i)
        if (!used[i]) res.push_back(tags[i]);
    *out = std::move(res);
    return true;
}

bool withoutTrackLinks(const std::string& bytes, std::string* out, std::string* err)
{
    PdbFile f;
    if (!parsePdb(bytes, &f, err)) return false;
    PdbTable* tags = findTable(f, kTagTable);
    if (!tags) {
        if (err) *err = "no tag table: this is not an exportExt.pdb";
        return false;
    }

    // the tag table: drop the deleted rows. A row stores its slot number (0x20 per slot) in bytes 2 and 3, so the rows that move up are rewritten.
    for (uint32_t n : dataPages(f, *tags)) {
        PdbPage& p = f.pages[n];
        if (!p.structured || p.presentRows() == 0 || p.presentRows() == int(p.rows.size())) continue;
        const bool countedRows = p.u20 == uint16_t(p.presentRows());     // the page header repeats the number of live rows
        std::vector<PdbRow> kept;
        for (const PdbRow& r : p.rows)
            if (r.present) kept.push_back(r);
        for (size_t i = 0; i < kept.size(); ++i) {
            if (kept[i].bytes.size() < 4) continue;
            const uint16_t shift = uint16_t(0x20 * i);
            kept[i].bytes[2] = char(shift & 0xFF);
            kept[i].bytes[3] = char(shift >> 8);
        }
        p.rows = std::move(kept);
        p.groupFlags.clear();
        for (size_t at = 0; at < p.rows.size(); at += kPdbRowsPerGroup) {
            const size_t k = std::min<size_t>(kPdbRowsPerGroup, p.rows.size() - at);
            p.groupFlags.push_back(uint16_t(k == 16 ? 0xFFFFu : ((1u << k) - 1)));
        }
        if (countedRows) p.u20 = uint16_t(p.rows.size());
    }

    // the track to tag table: no data pages. The index page points at the blank page that comes next, as in a table rekordbox wrote empty.
    PdbTable* links = findTable(f, kTrackTagTable);
    if (links && links->lastPage != links->firstPage) {
        const std::vector<uint32_t> data = dataPages(f, *links);
        if (data.empty() || links->firstPage >= f.pages.size()) {
            if (err) *err = "the track tag table has no usable page chain";
            return false;
        }
        for (uint32_t n : data) {
            PdbPage blank;
            blank.raw.assign(kPdbPageSize, '\0');
            f.pages[n] = blank;
        }
        PdbPage& idx = f.pages[links->firstPage];
        if (idx.structured || idx.raw.size() != kPdbPageSize) {
            if (err) *err = "the track tag table's first page is not an index page";
            return false;
        }
        links->emptyCandidate = data.front();
        links->lastPage = links->firstPage;
        idx.next = links->emptyCandidate;
        put32(idx.raw, 0x0c, idx.next);
        put32(idx.raw, 0x2c, kEmptyEntry);
    }
    return serialisePdb(f, out, err);
}

} // namespace trakcrate
