#ifndef NETWORK_CONFIG_DIALOG_H
#define NETWORK_CONFIG_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>

namespace vdj {

class NetworkConfigDialog : public QDialog {
    Q_OBJECT

public:
    explicit NetworkConfigDialog(QWidget* parent = nullptr);

    QString interface() const;
    void setInterface(const QString& iface);
    QString playerIdText() const;

    // Ask which network interface to use (physical ones, with state and addresses). Returns an empty string
    // when cancelled or when there is no candidate interface.
    static QString chooseInterface(QWidget* parent, const QString& title, const QString& preselect = QString());

private:
    QComboBox* ifaceCombo_;
    QLineEdit* playerIdEdit_;
    QPushButton* okButton_;
};

} // namespace vdj

#endif // NETWORK_CONFIG_DIALOG_H
