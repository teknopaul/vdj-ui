#include "pdb_tables.h"
#include "pdb_rows.h"

#include <map>

namespace trakcrate {

namespace {

uint16_t u16(const std::string& s, size_t at) { return uint16_t(uint8_t(s[at]) | uint8_t(s[at + 1]) << 8); }
uint32_t u32(const std::string& s, size_t at) { return uint32_t(u16(s, at)) | uint32_t(u16(s, at + 2)) << 16; }

// All rows of a table, deleted ones included, following the chain from the index page to the table's last page.
std::vector<const PdbRow*> allRows(const PdbFile& f, uint32_t type)
{
    std::vector<const PdbRow*> out;
    for (const PdbTable& t : f.tables) {
        if (t.type != type) continue;
        uint32_t n = t.firstPage;
        for (size_t guard = 0; n && n < f.pages.size() && guard < f.pages.size(); ++guard) {
            const PdbPage& p = f.pages[n];
            if (p.type != type) break;
            if (p.structured) for (const PdbRow& r : p.rows) out.push_back(&r);
            if (n == t.lastPage || p.next == n) break;
            n = p.next;
        }
    }
    return out;
}

void stripAnnotations(std::string* s)
{
    for (const char* mark : {"\xEF\xBF\xBA", "\xEF\xBF\xBB"})            // U+FFFA, U+FFFB
        for (size_t at; (at = s->find(mark)) != std::string::npos;) s->erase(at, 3);
}

} // namespace

std::vector<PdbColumn> readColumns(const PdbFile& f)
{
    std::vector<PdbColumn> out;
    for (const PdbRow* r : tableRows(f, kPdbTableColumns)) {
        if (r->bytes.size() < 5) continue;
        PdbColumn c;
        c.id = u16(r->bytes, 0);
        c.unknown = u16(r->bytes, 2);
        if (!decodeDeviceSqlString(r->bytes, 4, &c.name)) continue;
        stripAnnotations(&c.name);
        out.push_back(c);
    }
    return out;
}

std::vector<PdbMenuItem> readMenu(const PdbFile& f)
{
    std::vector<PdbMenuItem> out;
    for (const PdbRow* r : tableRows(f, kPdbTableMenu)) {
        if (r->bytes.size() < 8) continue;
        PdbMenuItem m;
        m.categoryId = u16(r->bytes, 0);
        m.contentPointer = u16(r->bytes, 2);
        m.unknown = uint8_t(r->bytes[4]);
        m.hidden = r->bytes[5] != 0;
        m.sortOrder = u16(r->bytes, 6);
        out.push_back(m);
    }
    return out;
}

std::vector<PdbHistory> readHistory(const PdbFile& f)
{
    std::vector<PdbHistory> out;
    for (const PdbRow* r : allRows(f, kPdbTableHistory)) {
        const std::string& b = r->bytes;
        if (b.size() < 13) continue;
        PdbHistory h;
        h.numTracks = u32(b, 4);                                   // 0 u16 subtype (0x0280), 2 u16 index shift, 8 u32 zero
        size_t at = 12, end = 0;
        if (!decodeDeviceSqlString(b, at, &h.date, &end)) continue;
        at = end;
        if (at + 2 > b.size() || u16(b, at) != 0x1E19) continue;      // a constant word between the date and the version
        at += 2;
        if (!decodeDeviceSqlString(b, at, &h.version, &end)) continue;
        if (!decodeDeviceSqlString(b, end, &h.label)) continue;
        h.deleted = !r->present;
        out.push_back(h);
    }
    return out;
}

std::vector<PdbHistoryPlaylist> readHistoryPlaylists(const PdbFile& f)
{
    std::vector<PdbHistoryPlaylist> out;
    for (const PdbRow* r : tableRows(f, kPdbTableHistoryPlaylists)) {
        if (r->bytes.size() < 5) continue;
        PdbHistoryPlaylist p;
        p.id = u32(r->bytes, 0);
        if (!decodeDeviceSqlString(r->bytes, 4, &p.name)) continue;
        out.push_back(p);
    }
    return out;
}

std::vector<PdbHistoryEntry> readHistoryEntries(const PdbFile& f)
{
    std::vector<PdbHistoryEntry> out;
    for (const PdbRow* r : tableRows(f, kPdbTableHistoryEntries)) {
        if (r->bytes.size() < 12) continue;
        out.push_back({ u32(r->bytes, 0), u32(r->bytes, 4), u32(r->bytes, 8) });
    }
    return out;
}

int applyMenuSettings(std::vector<PdbPage>* menuPages, const std::vector<MenuSetting>& settings)
{
    std::map<uint16_t, MenuSetting> by;
    for (const MenuSetting& s : settings) by[s.categoryId] = s;
    int changed = 0;
    for (PdbPage& p : *menuPages) {
        for (PdbRow& r : p.rows) {
            if (!r.present || r.bytes.size() < 8) continue;
            auto it = by.find(u16(r.bytes, 0));
            if (it == by.end()) continue;
            const char hidden = it->second.visible ? 0 : 1;
            const char lo = char(it->second.sortOrder & 0xff), hi = char(it->second.sortOrder >> 8);
            if (r.bytes[5] != hidden || r.bytes[6] != lo || r.bytes[7] != hi) ++changed;
            r.bytes[5] = hidden;
            r.bytes[6] = lo;
            r.bytes[7] = hi;
        }
    }
    return changed;
}

} // namespace trakcrate
