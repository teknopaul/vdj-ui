#include "trakcrate_track_model.h"

#include <QColor>
#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cstdlib>

#include "trakcrate/analysis.h"
#include "waveform_widget.h"

namespace vdj {

TrakcrateTrackModel::TrakcrateTrackModel(QObject* parent) : QAbstractTableModel(parent) {}

void TrakcrateTrackModel::setRows(const std::vector<trakcrate::fs::path>& dirs)
{
    beginResetModel();
    rows_.clear();
    rows_.reserve(dirs.size());
    for (const trakcrate::fs::path& d : dirs) {
        Row r;
        r.dir = d;
        rows_.push_back(std::move(r));
    }
    endResetModel();
}

void TrakcrateTrackModel::clear() { setRows({}); }

void TrakcrateTrackModel::showFolder(const trakcrate::Library& lib, const QString& folder)
{
    std::vector<trakcrate::fs::path> dirs;
    for (const std::string& t : lib.listTracks(folder.toStdString())) dirs.push_back(lib.trackDir(folder.toStdString(), t));
    setRows(dirs);
}

void TrakcrateTrackModel::showPlaylist(const trakcrate::Library& lib, const QString& name)
{
    setRows(lib.playlistTracks(name.toStdString()));
}

QString TrakcrateTrackModel::trackDir(int row) const
{
    if (row < 0 || size_t(row) >= rows_.size()) return QString();
    return QString::fromStdString(rows_[size_t(row)].dir.string());
}

bool TrakcrateTrackModel::track(int row, trakcrate::Track* out) const
{
    if (row < 0 || size_t(row) >= rows_.size()) return false;
    return trakcrate::loadTrack(rows_[size_t(row)].dir, out);
}

static QString formatTime(double seconds)
{
    if (seconds <= 0) return QString();
    int s = int(seconds + 0.5);
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

// The PWAV preview drawn at a fixed scale (height 31 = 0 dBFS, never normalised), tinted by its whiteness bits like the decks' preview.
static QPixmap miniWave(const trakcrate::Waveform& w)
{
    constexpr int W = 120, H = 20;
    if (w.pwav.empty()) return QPixmap();
    QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    const size_t n = w.pwav.size();
    for (int x = 0; x < W; ++x) {
        int h = 0, white = 0;
        for (size_t i = size_t(x) * n / W; i < size_t(x + 1) * n / W && i < n; ++i) {
            h = std::max(h, int(w.pwav[i] & 0x1f));
            white = std::max(white, int(w.pwav[i] >> 5));
        }
        const int px = h * H / 31;
        const int c = 90 + white * 20;
        p.setPen(QColor(c / 2, c, 255));
        p.drawLine(x, H / 2 - px / 2, x, H / 2 + (px + 1) / 2 - 1);
    }
    p.end();
    blurImage(img);
    return QPixmap::fromImage(img);
}

void TrakcrateTrackModel::load(const Row& r) const
{
    if (r.loaded) return;
    r.loaded = true;
    r.track = trakcrate::Track();
    if (!trakcrate::loadTrack(r.dir, &r.track)) {
        r.flags = QStringLiteral("unreadable");
        return;
    }
    const trakcrate::Track& t = r.track;
    r.hasAudio = !trakcrate::audioFileOf(r.dir).empty();
    trakcrate::BeatGrid g = trakcrate::beatGridFrom(t.grid);
    r.bpmValue = (t.hasGrid && !g.empty()) ? g.bpm : t.meta.getDouble("bpm_tag", 0);
    r.bpm = r.bpmValue > 0 ? QString::number(r.bpmValue, 'f', 2) : QString();
    r.seconds = double(t.meta.getInt("duration_ms", 0)) / 1000.0;
    r.time = formatTime(r.seconds);
    r.comment = QString::fromStdString(t.meta.get("comment"));
    r.rating = int(std::max(0LL, std::min(5LL, t.meta.getInt("rating", 0))));
    trakcrate::Waveform wf;
    if (trakcrate::loadWaveform(r.dir, &wf)) r.wave = miniWave(wf);
    r.origin = QStringLiteral("generated");
    for (const char* group : {"meta", "beatgrid", "cues", "waveform", "artwork"}) {
        std::string o = trakcrate::origin(t.source, group);
        if (o == "rekordbox") { r.origin = QStringLiteral("rekordbox"); break; }
        if (o == "edited") r.origin = QStringLiteral("edited");
    }
    QStringList flags;
    if (!r.hasAudio) flags << QStringLiteral("no audio");
    if (!t.hasGrid || g.empty()) flags << QStringLiteral("no beat grid");
    r.flags = flags.join(QStringLiteral(", "));
}

QString TrakcrateTrackModel::starsText(int stars)
{
    stars = std::max(0, std::min(5, stars));
    return QString(stars, QChar(0x2605)) + QString(5 - stars, QChar(0x2606));
}

bool TrakcrateTrackModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role == Qt::EditRole && index.isValid() && index.column() == Comment) return setComment(index, value.toString());
    if (role != RatingRole || !index.isValid() || index.row() < 0 || size_t(index.row()) >= rows_.size()) return false;
    const Row& r = rows_[size_t(index.row())];
    const int stars = std::max(0, std::min(5, value.toInt()));
    trakcrate::Track t;
    std::string err;
    if (!trakcrate::loadTrack(r.dir, &t, &err)) { emit ratingFailed(QStringLiteral("Could not read the track: %1").arg(QString::fromStdString(err))); return false; }
    if (int(t.meta.getInt("rating", 0)) == stars) { load(r); r.rating = stars; return true; }
    t.meta.setInt("rating", stars);
    trakcrate::setOrigin(t.source, "meta", "edited");
    if (!trakcrate::saveMeta(t, &err) || !trakcrate::saveSource(t, &err)) {
        emit ratingFailed(QStringLiteral("Could not save the rating: %1").arg(QString::fromStdString(err)));
        return false;
    }
    load(r);
    r.rating = stars;
    r.track.meta.setInt("rating", stars);
    const QModelIndex first = this->index(index.row(), 0), last = this->index(index.row(), ColumnCount - 1);
    emit dataChanged(first, last);
    emit ratingWritten(QString::fromStdString(r.dir.string()), stars);
    return true;
}

bool TrakcrateTrackModel::setComment(const QModelIndex& index, const QString& text)
{
    if (index.row() < 0 || size_t(index.row()) >= rows_.size()) return false;
    const Row& r = rows_[size_t(index.row())];
    trakcrate::Track t;
    std::string err;
    if (!trakcrate::loadTrack(r.dir, &t, &err)) { emit ratingFailed(QStringLiteral("Could not read the track: %1").arg(QString::fromStdString(err))); return false; }
    const std::string v = text.trimmed().toStdString();
    load(r);
    if (t.meta.get("comment") == v) return true;
    t.meta.set("comment", v);
    trakcrate::setOrigin(t.source, "meta", "edited");
    if (!trakcrate::saveMeta(t, &err) || !trakcrate::saveSource(t, &err)) {
        emit ratingFailed(QStringLiteral("Could not save the comment: %1").arg(QString::fromStdString(err)));
        return false;
    }
    r.comment = text.trimmed();
    r.track.meta.set("comment", v);
    emit dataChanged(this->index(index.row(), 0), this->index(index.row(), ColumnCount - 1));
    emit commentWritten(QString::fromStdString(r.dir.string()), r.comment);
    return true;
}

Qt::ItemFlags TrakcrateTrackModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index) & ~Qt::ItemIsDropEnabled;
    if (index.isValid() && draggable_) f |= Qt::ItemIsDragEnabled;
    if (index.isValid() && index.column() == Comment) f |= Qt::ItemIsEditable;
    return f;
}

int TrakcrateTrackModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(rows_.size()); }
int TrakcrateTrackModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : ColumnCount; }

QVariant TrakcrateTrackModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || size_t(index.row()) >= rows_.size()) return QVariant();
    const Row& r = rows_[size_t(index.row())];
    if (role != Qt::DisplayRole && role != Qt::EditRole && role != SortRole && role != RatingRole && role != Qt::ToolTipRole && role != Qt::DecorationRole) return QVariant();
    load(r);
    if (role == Qt::DecorationRole) return index.column() == Wave && !r.wave.isNull() ? QVariant(r.wave) : QVariant();
    const trakcrate::KvDoc& m = r.track.meta;
    if (role == RatingRole) return r.rating;
    if (role == Qt::ToolTipRole && index.column() == Rating) return QStringLiteral("Click a star to rate; click the same star again to clear");
    if (role == Qt::ToolTipRole) return QString::fromStdString(r.dir.string());
    if (role == SortRole) {
        if (index.column() == Bpm) return r.bpmValue;
        if (index.column() == Time) return r.seconds;
        if (index.column() == Rating) return r.rating;
    }
    QString title = QString::fromStdString(m.get("title"));
    if (title.isEmpty()) title = QString::fromStdString(r.dir.filename().string());
    switch (index.column()) {
    case Title: return title;
    case Artist: return QString::fromStdString(m.get("artist"));
    case Album: return QString::fromStdString(m.get("album"));
    case Bpm: return r.bpm;
    case Time: return r.time;
    case Comment: return r.comment;
    case Rating: return starsText(r.rating);
    case Wave: return QVariant();
    case Origin: return r.origin;
    case Flags: return r.flags;
    }
    return QVariant();
}

QVariant TrakcrateTrackModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    static const char* names[ColumnCount] = {"Title", "Artist", "Album", "Comment", "BPM", "Time", "Rating", "Waveform", "Origin", "Flags"};
    return section >= 0 && section < ColumnCount ? QVariant(QString::fromLatin1(names[section])) : QVariant();
}

} // namespace vdj
