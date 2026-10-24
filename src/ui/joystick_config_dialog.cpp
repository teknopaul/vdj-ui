#include "joystick_config_dialog.h"
#include "config/config_manager.h"
#include <QComboBox>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <fcntl.h>
#include <unistd.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>

namespace vdj {

static const char* kActions[] = { "play_pause", "sync", "master", "copy_bpm", "broadcast_beat" };

QList<JoystickInfo> JoystickConfigDialog::scanJoysticks()
{
    QList<JoystickInfo> out;
    QDir dir(QStringLiteral("/dev/input"));
    const QStringList entries = dir.entryList(QStringList() << "js*", QDir::System);
    for (const QString& e : entries) {
        QString path = dir.filePath(e);
        int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            JoystickInfo bad;
            bad.device = path;
            bad.name = QStringLiteral("<unreadable>");
            out.append(bad);
            continue;
        }
        JoystickInfo info;
        info.device = path;
        char name[128] = { 0 };
        if (ioctl(fd, JSIOCGNAME(sizeof(name) - 1), name) >= 0)
            info.name = QString::fromUtf8(name);
        else
            info.name = QStringLiteral("joystick");
        __u8 axes = 0, buttons = 0;
        if (ioctl(fd, JSIOCGAXES, &axes) >= 0) info.axes = axes;
        if (ioctl(fd, JSIOCGBUTTONS, &buttons) >= 0) info.buttons = buttons;
        ::close(fd);
        out.append(info);
    }
    return out;
}

JoystickConfigDialog::JoystickConfigDialog(ConfigManager* config, QWidget* parent)
    : QDialog(parent), config_(config)
{
    setWindowTitle(QStringLiteral("Joystick Configuration"));
    resize(520, 380);

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(QStringLiteral(
        "Pick a joystick, select an action, then press Learn and move a control. "
        "Mappings are saved as flat config files."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* form = new QFormLayout();
    device_ = new QComboBox(this);
    action_ = new QComboBox(this);
    for (const char* a : kActions)
        action_->addItem(QString::fromUtf8(a));
    form->addRow(QStringLiteral("Device:"), device_);
    form->addRow(QStringLiteral("Action:"), action_);
    layout->addLayout(form);

    mappingList_ = new QListWidget(this);
    layout->addWidget(mappingList_);

    auto* learnRow = new QHBoxLayout();
    learnButton_ = new QPushButton(QStringLiteral("Learn"), this);
    learnStatus_ = new QLabel(QStringLiteral("No joysticks detected yet"), this);
    learnRow->addWidget(learnButton_);
    learnRow->addWidget(learnStatus_, 1);
    layout->addLayout(learnRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);

    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(20);
    connect(pollTimer_, &QTimer::timeout, this, &JoystickConfigDialog::startLearning);

    populate();

    connect(device_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refreshMappings(); });
    connect(learnButton_, &QPushButton::clicked, this, &JoystickConfigDialog::startLearning);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

JoystickConfigDialog::~JoystickConfigDialog()
{
    stopLearning();
}

void JoystickConfigDialog::populate()
{
    device_->clear();
    const QList<JoystickInfo> sticks = scanJoysticks();
    for (const JoystickInfo& j : sticks) {
        device_->addItem(QStringLiteral("%1  -  %2 (axes:%3 buttons:%4)")
            .arg(j.device, j.name).arg(j.axes).arg(j.buttons));
    }
    if (sticks.isEmpty()) {
        learnStatus_->setText(QStringLiteral("No joysticks found in /dev/input"));
        learnButton_->setEnabled(false);
    } else {
        learnStatus_->setText(QStringLiteral("Select a device and action"));
        learnButton_->setEnabled(true);
    }
    refreshMappings();
}

void JoystickConfigDialog::refreshMappings()
{
    mappingList_->clear();
    QString device = device_->currentText();
    QString dev = device.split(' ').first();
    if (dev.isEmpty()) return;
    const QHash<QString, QString> map = config_->joystickMappings(dev);
    QStringList actions = map.keys();
    actions.sort();
    for (const QString& a : actions) {
        mappingList_->addItem(QStringLiteral("%1 -> %2").arg(a, map.value(a)));
    }
}

void JoystickConfigDialog::startLearning()
{
    stopLearning();

    const QList<JoystickInfo> sticks = scanJoysticks();
    int idx = device_->currentIndex();
    if (idx < 0 || idx >= sticks.size())
        return;
    if (sticks[idx].device != device_->currentText().split(' ').first())
        return;

    const QString path = sticks[idx].device;
    fd_ = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        learnStatus_->setText(QStringLiteral("Cannot open %1").arg(path));
        return;
    }

    learnStatus_->setText(QStringLiteral("Learning... move a control"));
    learnButton_->setText(QStringLiteral("Stop"));

    // poll timer reads button events and learns the first button press
    pollTimer_->disconnect(this);
    connect(pollTimer_, &QTimer::timeout, this, [this]() {
        struct js_event ev;
        while (::read(fd_, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
            if ((ev.type & ~JS_EVENT_INIT) == JS_EVENT_BUTTON && ev.value == 1) {
                learnedControl_ = QStringLiteral("button_%1").arg(ev.number);
                QString dev = device_->currentText().split(' ').first();
                QString action = action_->currentText();
                config_->saveJoystickMapping(dev, action, learnedControl_);
                learnStatus_->setText(QStringLiteral("Learned %1 -> %2")
                                      .arg(action, learnedControl_));
                refreshMappings();
                stopLearning();
                return;
            }
        }
    });
    pollTimer_->start();
}

void JoystickConfigDialog::stopLearning()
{
    if (pollTimer_) pollTimer_->stop();
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    learnButton_->setText(QStringLiteral("Learn"));
}

} // namespace vdj