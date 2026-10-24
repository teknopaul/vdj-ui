#ifndef ID3_TAGS_H
#define ID3_TAGS_H

#include <QHash>
#include <QString>

namespace vdj {

struct TrackMeta {
    QString title;
    QString artist;
    QString bpm;
    QString key;
    QString album;
};

// Extract standard ID3v2 tags (TIT2/TPE1/TALB/TBPM/TKEY) with ID3v1 fallback.
TrackMeta readId3Tags(const QString& filePath);

} // namespace vdj

#endif // ID3_TAGS_H