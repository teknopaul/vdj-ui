#ifndef TRAKCRATE_MP3_WRITER_H
#define TRAKCRATE_MP3_WRITER_H

#include "edit_output.h"

#include <memory>

namespace trakcrate {

// Streaming libmp3lame writer: frames go in as they are produced, so a two hour mix never has to be in memory.
// Output goes to <path>.part and is renamed on close(); abort() (or the destructor without close) removes it.
class Mp3Writer {
public:
    Mp3Writer();
    ~Mp3Writer();
    Mp3Writer(const Mp3Writer&) = delete;
    Mp3Writer& operator=(const Mp3Writer&) = delete;

    void setBpm(double bpm);                       // TBPM tag when > 0
    // A jpeg or png, written as the cover (ID3 APIC). Must be called before open().
    void setPicture(const fs::path& imageFile);
    // vbr: average bit rate mode (libmp3lame ABR) around `kbps`, falling back to the best VBR quality when the encoder refuses; else constant.
    bool open(const fs::path& path, int kbps, bool vbr, const AudioTags& tags, int sampleRate, std::string* err = nullptr);
    bool write(const float* stereo, size_t frames, std::string* err = nullptr);
    bool close(std::string* err = nullptr);
    void abort();
    bool isOpen() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace trakcrate

#endif
