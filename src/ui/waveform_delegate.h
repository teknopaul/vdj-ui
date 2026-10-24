#ifndef WAVEFORM_DELEGATE_H
#define WAVEFORM_DELEGATE_H

#include <QHash>
#include <QStyledItemDelegate>
#include <QVector>

namespace vdj {

// Paints the mini waveform (PWV2) of a track in a tree column. The item carries the track's analyze_path in
// kAnalyzePathRole; the small .DAT is read the first time the row is painted and kept in a cache.
class MiniWaveformDelegate : public QStyledItemDelegate {
public:
    static constexpr int kAnalyzePathRole = Qt::UserRole + 4;

    explicit MiniWaveformDelegate(QObject* parent = nullptr);

    void setRoot(const QString& root);
    void forget(const QString& analyzePath) { cache_.remove(analyzePath); }   // re-read after the file arrived      // folder holding PIONEER; clears the cache
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // The cached columns of a track (loads them if needed); empty when the track has no analysis.
    const QVector<quint8>& columnsFor(const QString& analyzePath) const;
    int cachedCount() const { return cache_.size(); }

private:
    QString root_;
    mutable QHash<QString, QVector<quint8>> cache_;
};

} // namespace vdj

#endif // WAVEFORM_DELEGATE_H
