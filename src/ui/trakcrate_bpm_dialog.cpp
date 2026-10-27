#include "trakcrate_bpm_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace vdj {

TrakcrateBpmDialog::TrakcrateBpmDialog(const QString& title, double detected, double confidence, double tagBpm, bool hasGrid, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Detected BPM"));
    QVBoxLayout* top = new QVBoxLayout(this);
    QString text = QStringLiteral("<b>%1</b><br>Detected %2 BPM (confidence %3)").arg(title.toHtmlEscaped())
                       .arg(detected, 0, 'f', 2).arg(confidence, 0, 'f', 2);
    text += tagBpm > 0 ? QStringLiteral("<br>The file's tag says %1 BPM").arg(tagBpm, 0, 'f', 2) : QStringLiteral("<br>The file has no BPM tag");
    QLabel* label = new QLabel(text);
    label->setTextFormat(Qt::RichText);
    top->addWidget(label);

    spin_ = new QDoubleSpinBox;
    spin_->setRange(30, 300);
    spin_->setDecimals(2);
    spin_->setSuffix(QStringLiteral(" BPM"));
    spin_->setValue(detected > 0 ? detected : (tagBpm > 0 ? tagBpm : 120));
    QPushButton* half = new QPushButton(QStringLiteral("Half"));
    QPushButton* twice = new QPushButton(QStringLiteral("Double"));
    QHBoxLayout* row = new QHBoxLayout;
    row->addWidget(spin_, 1);
    row->addWidget(half);
    row->addWidget(twice);
    top->addLayout(row);
    top->addWidget(new QLabel(QStringLiteral("Type a value to enter the BPM yourself.")));

    if (hasGrid) {
        replace_ = new QCheckBox(QStringLiteral("Replace the existing beat grid with this BPM"));
        replace_->setChecked(true);
        top->addWidget(replace_);
    } else {
        top->addWidget(new QLabel(QStringLiteral("A beat grid will be created from this BPM.")));
    }

    QDialogButtonBox* box = new QDialogButtonBox;
    box->addButton(QStringLiteral("Use this BPM"), QDialogButtonBox::AcceptRole);
    box->addButton(QStringLiteral("Reject"), QDialogButtonBox::RejectRole);
    top->addWidget(box);

    connect(half, &QPushButton::clicked, this, &TrakcrateBpmDialog::halve);
    connect(twice, &QPushButton::clicked, this, &TrakcrateBpmDialog::doubleIt);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

double TrakcrateBpmDialog::bpm() const { return spin_->value(); }
bool TrakcrateBpmDialog::replaceGrid() const { return replace_ && replace_->isChecked(); }
void TrakcrateBpmDialog::halve() { spin_->setValue(spin_->value() / 2); }
void TrakcrateBpmDialog::doubleIt() { spin_->setValue(spin_->value() * 2); }
void TrakcrateBpmDialog::setBpm(double bpm) { spin_->setValue(bpm); }

} // namespace vdj
