#ifndef TRAKCRATE_SEARCH_DIALOG_H
#define TRAKCRATE_SEARCH_DIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;

namespace vdj {

// A very small non modal search box: a line of text and a line of result text. Enter searches for the text (when it is the text of the last search,
// Enter goes on to the next result instead), Page Down / Page Up step through the results, Esc closes. It stays open between searches. It does not
// search by itself: the owner answers searchRequested / nextRequested / previousRequested and reports back with setResult.
class TrakcrateSearchDialog : public QDialog {
    Q_OBJECT

public:
    explicit TrakcrateSearchDialog(const QString& title, QWidget* parent = nullptr);

    QString text() const;
    void setText(const QString& text);
    void setResult(const QString& message);               // "3 of 12", "Not found", ...
    QLineEdit* edit() const { return edit_; }
    QLabel* resultLabel() const { return result_; }

signals:
    void searchRequested(const QString& text);
    void nextRequested();
    void previousRequested();

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    QLineEdit* edit_;
    QLabel* result_;
    QString lastSearched_;
};

} // namespace vdj

#endif
