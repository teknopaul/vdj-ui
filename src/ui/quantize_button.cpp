#include "quantize_button.h"
#include <QPushButton>

namespace vdj {

QPushButton* makeQuantizeButton(QWidget* parent)
{
    auto* b = new QPushButton(QStringLiteral("Q"), parent);
    b->setCheckable(true);
    b->setToolTip(QStringLiteral("Quantize"));
    b->setStyleSheet(QStringLiteral(
        "QPushButton { background: #8a1c1c; color: white; font-weight: bold; border: 1px solid #5a1010; border-radius: 3px; }"
        "QPushButton:checked { background: #ff3030; border: 1px solid #ff9a9a; }"
        "QPushButton:disabled { background: #5a3a3a; color: #bbbbbb; }"));
    b->setFocusPolicy(Qt::NoFocus);
    b->setFixedSize(26, 22);
    return b;
}

} // namespace vdj
