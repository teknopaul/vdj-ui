#ifndef VDJ_DIR_USAGE_H
#define VDJ_DIR_USAGE_H

#include <QString>
#include <atomic>

namespace vdj {

// What a location holds of a rekordbox export: the bytes under PIONEER/ and Contents/ (like `du -s`), and the audio files under Contents/.
struct DirUsage {
    qint64 bytes = 0;
    int audioFiles = 0;
    bool ok = false;            // false: neither folder exists (a fresh stick)
};

// Walks both folders without following symlinks. Slow on a big stick: not for the GUI thread. A set cancel flag ends the walk early.
DirUsage measureRekordbox(const QString& base, const std::atomic<bool>* cancel = nullptr);

} // namespace vdj

#endif
