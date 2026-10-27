#ifndef TRAKCRATE_SHA256_H
#define TRAKCRATE_SHA256_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace trakcrate {

// SHA-256 of a buffer as 64 lowercase hex characters.
std::string sha256Hex(const void* data, size_t len);
inline std::string sha256Hex(const std::string& s) { return sha256Hex(s.data(), s.size()); }

// Incremental form for large files.
class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t len);
    std::string finishHex();      // the object must not be used afterwards

private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    size_t used_ = 0;
    uint64_t total_ = 0;
};

} // namespace trakcrate

#endif
