#include "pdb_writer.h"

#include <algorithm>

namespace trakcrate {

namespace {

uint32_t rd32(const std::string& s, size_t at) { return uint32_t(uint8_t(s[at])) | uint32_t(uint8_t(s[at + 1])) << 8 | uint32_t(uint8_t(s[at + 2])) << 16 | uint32_t(uint8_t(s[at + 3])) << 24; }
uint16_t rd16(const std::string& s, size_t at) { return uint16_t(uint8_t(s[at]) | uint8_t(s[at + 1]) << 8); }
void wr32(std::string& s, size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) s[at + size_t(i)] = char((v >> (8 * i)) & 0xFF); }
void wr16(std::string& s, size_t at, uint16_t v) { s[at] = char(v & 0xFF); s[at + 1] = char(v >> 8); }

int groupsFor(size_t slots) { return slots == 0 ? 0 : int((slots - 1) / kPdbRowsPerGroup + 1); }

// Bytes the row index takes at the end of the page. Full groups take 36 bytes; the last group only as much as its used offsets plus the two
// words (present bits, row flags): the unused offset slots below it are part of the free gap, or already covered by the heap.
size_t indexSpan(size_t slots)
{
    if (slots == 0) return 0;
    const size_t groups = size_t(groupsFor(slots));
    return (groups - 1) * kPdbGroupSize + 4 + 2 * (slots - (groups - 1) * kPdbRowsPerGroup);
}

// Decodes a data page into rows. False (page stays raw) when anything is not exactly what the writer would produce itself, so a
// structured page always serialises back to the same bytes.
bool decodeDataPage(const std::string& pg, PdbPage* p)
{
    if (p->flags == 0 || (p->flags & 0x40)) return false;
    const uint32_t packed = uint32_t(uint8_t(pg[0x18])) | uint32_t(uint8_t(pg[0x19])) << 8 | uint32_t(uint8_t(pg[0x1a])) << 16;
    const size_t slots = packed & 0x1FFF;
    const uint32_t presentCount = packed >> 13;
    const uint16_t free = rd16(pg, 0x1c), used = rd16(pg, 0x1e);
    const int groups = groupsFor(slots);
    if (slots == 0) return false;
    if (kPdbPageHeader + size_t(used) + indexSpan(slots) > kPdbPageSize) return false;

    std::vector<uint16_t> offs(slots), pres(size_t(groups), 0), flg(size_t(groups), 0);
    for (int g = 0; g < groups; ++g) {
        const size_t base = kPdbPageSize - size_t(g) * kPdbGroupSize;
        pres[size_t(g)] = rd16(pg, base - 4);
        flg[size_t(g)] = rd16(pg, base - 2);
        const size_t n = std::min<size_t>(kPdbRowsPerGroup, slots - size_t(g) * kPdbRowsPerGroup);
        for (size_t i = 0; i < n; ++i) offs[size_t(g) * kPdbRowsPerGroup + i] = rd16(pg, base - 6 - 2 * i);
        if (n < kPdbRowsPerGroup) {
            const uint16_t unusedMask = uint16_t(0xFFFFu << n);
            if ((pres[size_t(g)] & unusedMask) || (flg[size_t(g)] & unusedMask)) return false;
        }
    }
    // rows lie back to back in slot order
    for (size_t i = 0; i < slots; ++i) {
        const uint32_t start = offs[i], end = i + 1 < slots ? offs[i + 1] : used;
        if (i == 0 && start != 0) return false;                                  // the heap starts with the first row
        if (end <= start || end > used) return false;
    }
    // the heap is followed by zeros up to the row index
    const size_t heapEnd = kPdbPageHeader + used, indexStart = kPdbPageSize - indexSpan(slots);
    for (size_t i = heapEnd; i < indexStart; ++i) if (pg[i] != 0) return false;

    PdbPage tmp = *p;
    tmp.rows.clear();
    tmp.groupFlags = flg;
    uint32_t present = 0;
    for (size_t i = 0; i < slots; ++i) {
        const uint32_t start = offs[i], end = i + 1 < slots ? offs[i + 1] : used;
        PdbRow r;
        r.bytes = pg.substr(kPdbPageHeader + start, end - start);
        r.present = (pres[i / kPdbRowsPerGroup] >> (i % kPdbRowsPerGroup)) & 1;
        present += r.present ? 1 : 0;
        tmp.rows.push_back(r);
    }
    if (present != presentCount) return false;
    if (free != tmp.freeBytes()) return false;
    tmp.structured = true;
    tmp.raw.clear();
    *p = tmp;
    return true;
}

bool encodeDataPage(const PdbPage& p, std::string* out, std::string* err)
{
    const size_t slots = p.rows.size();
    const int groups = groupsFor(slots);
    if (slots == 0 || slots > 0x1FFF) { if (err) *err = "a data page needs 1 to 8191 rows"; return false; }
    if (p.groupFlags.size() != size_t(groups)) { if (err) *err = "row flag words do not match the number of row groups"; return false; }
    const int heap = p.heapBytes();
    if (heap > 0xFFFF || kPdbPageHeader + size_t(heap) + indexSpan(slots) > kPdbPageSize) {
        if (err) *err = "rows do not fit in a page";
        return false;
    }
    std::string pg(kPdbPageSize, '\0');
    wr32(pg, 0x00, p.zero0);
    wr32(pg, 0x04, p.index);
    wr32(pg, 0x08, p.type);
    wr32(pg, 0x0c, p.next);
    wr32(pg, 0x10, p.u10);
    wr32(pg, 0x14, p.u14);
    const uint32_t packed = uint32_t(slots) | (uint32_t(p.presentRows()) << 13);
    pg[0x18] = char(packed & 0xFF);
    pg[0x19] = char((packed >> 8) & 0xFF);
    pg[0x1a] = char((packed >> 16) & 0xFF);
    pg[0x1b] = char(p.flags);
    wr16(pg, 0x1c, uint16_t(p.freeBytes()));
    wr16(pg, 0x1e, uint16_t(heap));
    wr16(pg, 0x20, p.u20);
    wr16(pg, 0x22, p.numLarge);
    wr16(pg, 0x24, p.u24);
    wr16(pg, 0x26, p.u26);
    size_t at = kPdbPageHeader;
    for (size_t i = 0; i < slots; ++i) {
        const size_t g = i / kPdbRowsPerGroup, k = i % kPdbRowsPerGroup;
        const size_t base = kPdbPageSize - g * kPdbGroupSize;
        wr16(pg, base - 6 - 2 * k, uint16_t(at - kPdbPageHeader));
        if (p.rows[i].present) wr16(pg, base - 4, uint16_t(rd16(pg, base - 4) | (1u << k)));
        pg.replace(at, p.rows[i].bytes.size(), p.rows[i].bytes);
        at += p.rows[i].bytes.size();
    }
    for (int g = 0; g < groups; ++g) wr16(pg, kPdbPageSize - size_t(g) * kPdbGroupSize - 2, p.groupFlags[size_t(g)]);
    *out = pg;
    return true;
}

} // namespace

int PdbPage::presentRows() const
{
    int n = 0;
    for (const PdbRow& r : rows) n += r.present ? 1 : 0;
    return n;
}

int PdbPage::heapBytes() const
{
    size_t n = 0;
    for (const PdbRow& r : rows) n += r.bytes.size();
    return int(n);
}

int PdbPage::freeBytes() const
{
    const int groups = groupsFor(rows.size());
    return int(kPdbPageSize) - int(kPdbPageHeader) - heapBytes() - (2 * int(rows.size()) + 4 * groups);
}

bool parsePdb(const std::string& bytes, PdbFile* out, std::string* err)
{
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (bytes.size() < kPdbPageSize || bytes.size() % kPdbPageSize) return fail("size is not a multiple of the 4096 byte page");
    if (rd32(bytes, 0) != 0) return fail("bad header");
    PdbFile f;
    f.pageSize = rd32(bytes, 4);
    if (f.pageSize != kPdbPageSize) return fail("page size is not 4096");
    const uint32_t numTables = rd32(bytes, 8);
    if (numTables > (kPdbPageSize - 28) / 16) return fail("too many tables");
    f.nextUnusedPage = rd32(bytes, 12);
    f.unknown10 = rd32(bytes, 16);
    f.sequence = rd32(bytes, 20);
    f.gap = rd32(bytes, 24);
    for (uint32_t i = 0; i < numTables; ++i) {
        PdbTable t;
        t.type = rd32(bytes, 28 + 16 * i);
        t.emptyCandidate = rd32(bytes, 32 + 16 * i);
        t.firstPage = rd32(bytes, 36 + 16 * i);
        t.lastPage = rd32(bytes, 40 + 16 * i);
        f.tables.push_back(t);
    }
    f.headerTail = bytes.substr(28 + 16 * numTables, kPdbPageSize - 28 - 16 * numTables);
    const size_t count = bytes.size() / kPdbPageSize;
    f.pages.resize(count);
    for (size_t n = 1; n < count; ++n) {
        const std::string pg = bytes.substr(n * kPdbPageSize, kPdbPageSize);
        PdbPage& p = f.pages[n];
        p.zero0 = rd32(pg, 0x00);
        p.index = rd32(pg, 0x04);
        p.type = rd32(pg, 0x08);
        p.next = rd32(pg, 0x0c);
        p.u10 = rd32(pg, 0x10);
        p.u14 = rd32(pg, 0x14);
        p.flags = uint8_t(pg[0x1b]);
        p.u20 = rd16(pg, 0x20);
        p.numLarge = rd16(pg, 0x22);
        p.u24 = rd16(pg, 0x24);
        p.u26 = rd16(pg, 0x26);
        p.raw = pg;
        decodeDataPage(pg, &p);                    // stays raw when it is not a plain data page
    }
    *out = f;
    return true;
}

bool serialisePdb(const PdbFile& f, std::string* out, std::string* err)
{
    if (f.pageSize != kPdbPageSize) { if (err) *err = "page size is not 4096"; return false; }
    if (f.pages.empty()) { if (err) *err = "no header page"; return false; }
    std::string res(kPdbPageSize, '\0');
    wr32(res, 4, f.pageSize);
    wr32(res, 8, uint32_t(f.tables.size()));
    wr32(res, 12, f.nextUnusedPage);
    wr32(res, 16, f.unknown10);
    wr32(res, 20, f.sequence);
    wr32(res, 24, f.gap);
    for (size_t i = 0; i < f.tables.size(); ++i) {
        wr32(res, 28 + 16 * i, f.tables[i].type);
        wr32(res, 32 + 16 * i, f.tables[i].emptyCandidate);
        wr32(res, 36 + 16 * i, f.tables[i].firstPage);
        wr32(res, 40 + 16 * i, f.tables[i].lastPage);
    }
    const size_t tail = 28 + 16 * f.tables.size();
    if (tail > kPdbPageSize || f.headerTail.size() != kPdbPageSize - tail) { if (err) *err = "header tail has the wrong size"; return false; }
    res.replace(tail, f.headerTail.size(), f.headerTail);
    for (size_t n = 1; n < f.pages.size(); ++n) {
        const PdbPage& p = f.pages[n];
        if (!p.structured) {
            if (p.raw.size() != kPdbPageSize) { if (err) *err = "raw page " + std::to_string(n) + " has the wrong size"; return false; }
            res += p.raw;
            continue;
        }
        std::string pg;
        if (!encodeDataPage(p, &pg, err)) { if (err) *err = "page " + std::to_string(n) + ": " + *err; return false; }
        res += pg;
    }
    *out = res;
    return true;
}

std::vector<const PdbRow*> tableRows(const PdbFile& f, uint32_t type)
{
    std::vector<const PdbRow*> rows;
    for (const PdbTable& t : f.tables) {
        if (t.type != type) continue;
        uint32_t n = t.firstPage;
        for (size_t guard = 0; n > 0 && n < f.pages.size() && guard < f.pages.size(); ++guard) {
            const PdbPage& p = f.pages[n];
            if (p.type != type) break;
            if (p.structured)
                for (const PdbRow& r : p.rows)
                    if (r.present) rows.push_back(&r);
            if (n == t.lastPage || p.next == n) break;
            n = p.next;
        }
        break;
    }
    return rows;
}

} // namespace trakcrate
