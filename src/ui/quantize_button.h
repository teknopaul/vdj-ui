#ifndef QUANTIZE_BUTTON_H
#define QUANTIZE_BUTTON_H

class QPushButton;
class QWidget;

namespace vdj {

// The "Q" quantize toggle: small red button, white bold "Q", tooltip "Quantize". One definition for the local player and the VDJ deck row.
QPushButton* makeQuantizeButton(QWidget* parent);

} // namespace vdj

#endif // QUANTIZE_BUTTON_H
