#ifndef DB_CODEC_H
#define DB_CODEC_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Wire codec for the Pioneer "dbserver" (RemoteDB) protocol spoken on TCP port 1051 (and 12523 for the port query).
// Qt-free on purpose so it can be unit tested without a GUI. See references/adj/doc/tcp-protocol-notes.md and
// https://djl-analysis.deepsymmetry.org/djl-analysis/track_metadata.html
//
// Message layout (every field is a tag byte followed by big-endian data):
//   11 872349ae            magic
//   11 TTTTTTTT            transaction id
//   10 tttt                message type
//   0f nn                  argument count (at most 12)
//   14 0000000c <12 bytes> one tag per argument: 02 string, 03 blob, 06 number (unused slots are 00)
//   args...                11 u32 | 26 u32-char-count UTF-16BE (incl. NUL) | 14 u32-length bytes
// A blob argument is preceded by a number argument holding its length; when that length is 0 the blob is omitted
// from the wire entirely (not even a 14 tag).

namespace vdj {
namespace db {

constexpr uint32_t kMagic = 0x872349ae;
constexpr uint32_t kSetupTxid = 0xfffffffe;      // setup and disconnect messages
constexpr size_t   kMaxArgs = 12;
constexpr size_t   kMaxFieldBytes = 16u * 1024 * 1024;   // sanity limit for one string / blob
constexpr uint16_t kRemoteDbPort = 12523;
constexpr uint16_t kDefaultDbPort = 1051;

// Argument tag codes as they appear in the 12 byte tag blob.
constexpr uint8_t kArgNone = 0x00;
constexpr uint8_t kArgString = 0x02;
constexpr uint8_t kArgBlob = 0x03;
constexpr uint8_t kArgNumber = 0x06;

struct Arg {
    enum Kind { Number, String, Blob };
    Kind                  kind = Number;
    uint32_t              num = 0;
    std::u16string        str;     // without the final (terminator) character
    char16_t              term = 0;    // final character on the wire; 0x0000 normally. A real deck sends an "empty"
                                       // string as one character 0x0009 in some menu items, so keep it for exact round trips
    std::vector<uint8_t>  blob;

    static Arg number(uint32_t n);
    static Arg string(const std::u16string& s);
    static Arg stringUtf8(const std::string& s);
    static Arg blobArg(const std::vector<uint8_t>& b);

    bool operator==(const Arg& o) const;
};

struct Message {
    uint32_t         txid = 0;
    uint16_t         type = 0;
    std::vector<Arg> args;

    bool operator==(const Message& o) const;
};

// The 5 byte greeting both sides send right after the TCP connect (a number field holding 1).
std::vector<uint8_t> encodeGreeting();

// Serialise one message. Throws std::invalid_argument when it has more than kMaxArgs arguments, or a blob argument
// that is not preceded by a number argument equal to its length (the wire format cannot express that).
std::vector<uint8_t> encode(const Message& m);
void                 encodeAppend(const Message& m, std::vector<uint8_t>& out);

// Incremental parser. feed() any chunk of the byte stream (partial messages and several messages per chunk are fine),
// then call next() until it returns None. After Error the parser stays failed; the stream cannot be resynchronised.
class StreamParser {
public:
    enum Status { None, Greeting, MessageReady, Error };

    void   feed(const uint8_t* data, size_t len);
    Status next(Message& out);

    bool   failed() const { return failed_; }
    size_t errorOffset() const { return errorOffset_; }     // stream offset of the failing field
    size_t consumed() const { return base_ + pos_; }        // stream offset of the next unparsed byte
    size_t buffered() const { return buf_.size() - pos_; }

private:
    std::vector<uint8_t> buf_;
    size_t               pos_ = 0;        // next unparsed byte in buf_
    size_t               base_ = 0;       // stream offset of buf_[0]
    size_t               waitFor_ = 0;    // do not retry parsing until buffered() >= waitFor_
    bool                 failed_ = false;
    size_t               errorOffset_ = 0;
};

// One line description for logs: "tx=0x00000003 type=0x3000 [#1 0x04010301 #2 \"Title\" #3 blob(12) 00 11 ...]".
std::string describe(const Message& m);

// Space separated lowercase hex of at most maxBytes bytes ("..." appended when truncated).
std::string hex(const uint8_t* data, size_t len, size_t maxBytes = 32);

std::string utf16ToUtf8(const std::u16string& s);
std::u16string utf8ToUtf16(const std::string& s);

// Port 12523: the client asks "where is the dbserver", the server answers with 2 bytes (big-endian port).
std::vector<uint8_t> portQueryRequest();
bool                 isPortQueryRequest(const uint8_t* data, size_t len);   // exactly the 19 byte request
std::vector<uint8_t> portQueryReply(uint16_t port);

} // namespace db
} // namespace vdj

#endif // DB_CODEC_H
