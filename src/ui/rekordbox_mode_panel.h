#ifndef REKORDBOX_MODE_PANEL_H
#define REKORDBOX_MODE_PANEL_H

#include <QWidget>
#include <functional>

#include "dbserver/rb_mode.h"
#include "dbserver/rb_mode_options.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace vdj {

class ConfigManager;

// Dock panel for the "pretend to be a rekordbox PC" experiment (see doc/rekordbox-mode.md). It only drives RbMode; all
// behaviour lives in src/dbserver. While it runs the normal link connection must stay off (same UDP ports): the host
// window tells the panel how to check that with setLinkConnectedProbe().
class RekordboxModePanel : public QWidget {
    Q_OBJECT

public:
    explicit RekordboxModePanel(ConfigManager* config, QWidget* parent = nullptr);
    ~RekordboxModePanel() override;

    // fills the widgets from command line options (--rekordbox-mode ...)
    void applyOptions(const RbModeOptions& o);
    // the options as currently shown; ports and other test overrides come from baseOptions()
    RbModeOptions options() const;
    RbModeOptions& baseOptions() { return base_; }

    bool startMode(QString* error = nullptr);
    void stopMode();
    bool isRunning() const { return mode_.isRunning(); }
    RbMode& mode() { return mode_; }

    void setLinkConnectedProbe(std::function<bool()> probe) { linkConnected_ = std::move(probe); }

    // for tests
    QString statusText() const;
    int unknownCount() const;
    int deckRows() const;

signals:
    void runningChanged(bool running);

private slots:
    void onStartStop();
    void refresh();
    void copyUnknown();

private:
    void browseExport();
    void browseLogDir();
    void addLog(const QString& line);
    void addUnknown(const QString& ip, quint16 type, const QString& description, const QByteArray& raw);
    void showUnknown(int row);
    void saveSettings() const;
    void loadSettings();
    void setRunningUi(bool running);

    ConfigManager* config_;
    RbMode         mode_;
    RbModeOptions  base_;
    std::function<bool()> linkConnected_;

    QLineEdit*      exportEdit_;
    QComboBox*      ifaceCombo_;
    QSpinBox*       deviceSpin_;
    QLineEdit*      nameEdit_;
    QComboBox*      policyCombo_;
    QLineEdit*      logEdit_;
    QPushButton*    startBtn_;
    QLabel*         statusLabel_;
    QLabel*         countersLabel_;
    QTableWidget*   decksTable_;
    QListWidget*    unknownList_;
    QPlainTextEdit* logView_;
    QTimer*         refreshTimer_;
    int             logLines_ = 0;
};

} // namespace vdj

#endif // REKORDBOX_MODE_PANEL_H
