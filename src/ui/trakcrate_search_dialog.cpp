#include "trakcrate_search_dialog.h"

#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace vdj {

TrakcrateSearchDialog::TrakcrateSearchDialog(const QString& title, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(title);
    setModal(false);
    edit_ = new QLineEdit;
    edit_->setClearButtonEnabled(true);
    edit_->setMinimumWidth(320);
    result_ = new QLabel(QStringLiteral("Enter to search, Page Down / Page Up for the next / previous result, Esc to close"));
    result_->setEnabled(false);
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(edit_);
    lay->addWidget(result_);
    connect(edit_, &QLineEdit::returnPressed, this, [this] {
        const QString t = edit_->text().trimmed();
        if (t.isEmpty()) return;
        if (t == lastSearched_) { emit nextRequested(); return; }
        lastSearched_ = t;
        emit searchRequested(t);
    });
    connect(edit_, &QLineEdit::textEdited, this, [this] { lastSearched_.clear(); });
}

QString TrakcrateSearchDialog::text() const { return edit_->text(); }

void TrakcrateSearchDialog::setText(const QString& text)
{
    edit_->setText(text);
    edit_->selectAll();
}

void TrakcrateSearchDialog::setResult(const QString& message) { result_->setText(message); result_->setEnabled(true); }

void TrakcrateSearchDialog::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_PageDown) { emit nextRequested(); e->accept(); return; }
    if (e->key() == Qt::Key_PageUp) { emit previousRequested(); e->accept(); return; }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { e->accept(); return; }      // never the dialog's default button
    QDialog::keyPressEvent(e);
}

} // namespace vdj
