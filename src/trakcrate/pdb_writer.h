#ifndef TRAKCRATE_PDB_WRITER_H
#define TRAKCRATE_PDB_WRITER_H

#include <cstdint>
#include <string>
#include <vector>

// Structure of a rekordbox export.pdb (see doc/trakcrate/pdb-write-notes.md for what is known and what is guessed). The model keeps every
// byte of a file: pages that are not understood are kept verbatim, so parse followed by serialise reproduces any file exactly; pages that
// are understood (data pages) are held as rows and rebuilt from them, so a writer can add rows and get a consistent page.
namespace trakcrate {

constexpr uint32_t kPdbPageSize = 4096;
constexpr uint32_t kPdbPageHeader = 40;          // the row heap starts at 0x28
constexpr uint32_t kPdbGroupSize = 0x24;         // 16 row offsets + present bits + row flags, growing down from the page end
constexpr uint32_t kPdbRowsPerGroup = 16;

struct PdbTable {
    uint32_t type = 0;               // 0 tracks, 1 genres, 2 artists, 3 albums, 4 labels, 5 keys, 6 colors, 7 playlist tree, 8 playlist entries, ...
    uint32_t emptyCandidate = 0;     // page number of the next page to use for this table
    uint32_t firstPage = 0;          // the table's index page
    uint32_t lastPage = 0;           // the table's last page (the last data page, or the index page for an empty table)
};

struct PdbRow {
    std::string bytes;               // the row exactly as stored in the heap (type specific, undecoded here)
    bool present = true;             // false: deleted, the bytes remain in the heap
};

struct PdbPage {
    // page header words, set for every page that starts with a valid header (also for raw pages, for chain walking)
    uint32_t zero0 = 0;              // 0x00, always 0
    uint32_t index = 0;              // 0x04, the page's own number (0 in a blank page)
    uint32_t type = 0;               // 0x08, the table type
    uint32_t next = 0;               // 0x0c, next page of the table
    uint32_t u10 = 0;                // 0x10, unknown: looks like a change counter
    uint32_t u14 = 0;                // 0x14, unknown, 0 in all samples
    uint8_t flags = 0;               // 0x1b: 0x40 set = not a data page; seen 0x00 (blank), 0x24, 0x34, 0x64 (index page)
    uint16_t u20 = 0, numLarge = 0, u24 = 0, u26 = 0;    // 0x20, 0x22, 0x24, 0x26: unknown

    // Raw page: the whole 4096 bytes verbatim (blank pages, index pages, and anything a data page check refused).
    bool structured = false;
    std::string raw;

    // Structured data page.
    std::vector<PdbRow> rows;                  // in slot order; the heap holds them in this order, back to back
    std::vector<uint16_t> groupFlags;          // one word per group of 16 slots, the "row flags" (unknown meaning), 0xffff-style like present bits

    int presentRows() const;
    // Free bytes in the page as rekordbox records them: the page minus header, heap and the used part of the row index.
    int freeBytes() const;
    int heapBytes() const;
};

struct PdbFile {
    uint32_t pageSize = kPdbPageSize;
    uint32_t nextUnusedPage = 0;     // 0x0c of the header page
    uint32_t unknown10 = 0;          // 0x10, 5 in the samples
    uint32_t sequence = 0;           // 0x14, a modification counter
    uint32_t gap = 0;                // 0x18, 0 in the samples
    std::vector<PdbTable> tables;
    std::string headerTail;          // the rest of page 0 after the table entries (zeros in the samples)
    std::vector<PdbPage> pages;      // pages[0] is unused (the header page is the fields above); pages.size() * 4096 == file size
};

// False (with err) when the bytes are not a pdb this module can hold (size not a multiple of 4096, page size not 4096, damaged header).
bool parsePdb(const std::string& bytes, PdbFile* out, std::string* err = nullptr);

// False when a structured page does not fit (too many rows for 4096 bytes) or the file is inconsistent.
bool serialisePdb(const PdbFile& file, std::string* out, std::string* err = nullptr);

// Rows of every page of the table `type` (walking the chain from its first page), present rows only, in page then slot order.
std::vector<const PdbRow*> tableRows(const PdbFile& file, uint32_t type);

} // namespace trakcrate

#endif
