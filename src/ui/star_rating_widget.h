#ifndef STAR_RATING_WIDGET_H
#define STAR_RATING_WIDGET_H

#include <QWidget>

namespace vdj {

// Five stars. Click star N to set N, click the current rating again to clear it, hover previews, keys 0-5 when focused.
class StarRatingWidget : public QWidget {
    Q_OBJECT

public:
    explicit StarRatingWidget(QWidget* parent = nullptr);

    int rating() const { return rating_; }
    void setRating(int stars);                 // 0..5, clamped; does not emit ratingChanged
    void setEditable(bool on);
    QSize sizeHint() const override;

signals:
    void ratingChanged(int stars);             // from the user only

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    int starAt(const QPoint& p) const;         // 1..5, 0 outside
    void userSet(int stars);

    int rating_ = 0;
    int hover_ = 0;
    bool editable_ = true;
};

} // namespace vdj

#endif
