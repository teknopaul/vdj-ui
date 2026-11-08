#ifndef EDIT_PREFS_H
#define EDIT_PREFS_H

#include <QString>

namespace vdj {

class ConfigManager;

// What the track edit dialogs remember (config section "edit"): output format and MP3 bitrate, the author name used in titles and file names,
// and the folders for edits saved as files and for acid loops.
class EditPrefs {
public:
    explicit EditPrefs(ConfigManager* config) : config_(config) {}

    QString format() const;                      // "wav" or "mp3", default wav
    void setFormat(const QString& f);
    int mp3Kbps() const;                         // 128, 192, 256 or 320, default 256
    void setMp3Kbps(int kbps);
    QString author() const;                      // default $USER
    void setAuthor(const QString& a);
    // Where a file-only edit goes: the remembered folder, else the folder the original was imported from (when it still exists), else Music, else home.
    QString folder(const QString& importedFrom = QString()) const;
    void setFolder(const QString& f);
    // Where acid loops go: the remembered loop folder, else the same rule as folder().
    QString loopFolder(const QString& importedFrom = QString()) const;
    void setLoopFolder(const QString& f);

    static QString defaultFolder(const QString& importedFrom);

private:
    QString get(const char* key) const;
    void set(const char* key, const QString& v);
    ConfigManager* config_;
};

} // namespace vdj

#endif
