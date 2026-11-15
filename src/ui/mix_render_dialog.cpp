#include "mix_render_dialog.h"

#include "edit_prefs.h"
#include "mix_filters.h"
#include "trakcrate/mix_render.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

namespace vdj {

using namespace trakcrate;

namespace {
QString durationText(double ms)
{
    const long long s = llround(ms / 1000.0);
    return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}
}

MixRenderDialog::MixRenderDialog(const Mix& mix, const fs::path& libraryRoot, ConfigManager* config, StretchCache* cache, QWidget* parent)
    : QDialog(parent), mix_(mix), root_(libraryRoot), config_(config), cache_(cache)
{
    setWindowTitle(QStringLiteral("Render studio mix as MP3"));
    EditPrefs prefs(config);
    name_ = new QLineEdit(QString::fromStdString(mix.name.empty() ? "Studio mix" : mix.name));
    author_ = new QLineEdit(mix.author.empty() ? prefs.author() : QString::fromStdString(mix.author));
    image_ = new QLineEdit(QString::fromStdString(mix.image));
    folder_ = new QLineEdit(prefs.folder());
    thumb_ = new QLabel;
    thumb_->setFixedSize(64, 64);
    thumb_->setAlignment(Qt::AlignCenter);
    planned_ = new QLabel;
    planned_->setWordWrap(true);
    warnings_ = new QLabel;
    warnings_->setWordWrap(true);
    warnings_->setStyleSheet(QStringLiteral("color: #c60;"));
    result_ = new QLabel;
    result_->setWordWrap(true);
    stage_ = new QLabel;
    progress_ = new QProgressBar;
    progress_->setRange(0, 1000);
    progress_->setValue(0);
    render_ = new QPushButton(QStringLiteral("Render"));
    render_->setDefault(true);
    close_ = new QPushButton(QStringLiteral("Cancel"));

    auto* imageBrowse = new QPushButton(QStringLiteral("..."));
    auto* folderBrowse = new QPushButton(QStringLiteral("..."));
    auto* imageRow = new QHBoxLayout;
    imageRow->addWidget(image_, 1);
    imageRow->addWidget(imageBrowse);
    imageRow->addWidget(thumb_);
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(folderBrowse);
    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Name"), name_);
    form->addRow(QStringLiteral("Author"), author_);
    form->addRow(QStringLiteral("Cover image"), imageRow);
    form->addRow(QStringLiteral("Folder"), folderRow);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(render_);
    buttons->addWidget(close_);
    auto* col = new QVBoxLayout(this);
    col->addLayout(form);
    col->addWidget(planned_);
    col->addWidget(warnings_);
    col->addWidget(stage_);
    col->addWidget(progress_);
    col->addWidget(result_);
    col->addLayout(buttons);

    connect(name_, &QLineEdit::textChanged, this, [this] { refreshPlan(); });
    connect(folder_, &QLineEdit::textChanged, this, [this] { refreshPlan(); });
    connect(image_, &QLineEdit::textChanged, this, [this] {
        QPixmap p(image_->text());
        thumb_->setPixmap(p.isNull() ? QPixmap() : p.scaled(thumb_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });
    connect(imageBrowse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Cover image"), QString(), QStringLiteral("Images (*.jpg *.jpeg *.png)"));
        if (!f.isEmpty()) image_->setText(f);
    });
    connect(folderBrowse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getExistingDirectory(this, QStringLiteral("Folder for the mix"), folder_->text());
        if (!f.isEmpty()) folder_->setText(f);
    });
    connect(render_, &QPushButton::clicked, this, [this] { startRender(); });
    connect(close_, &QPushButton::clicked, this, [this] { reject(); });

    std::string err;
    if (!readMixFacts(mix_, &facts_, &err)) factsError_ = QString::fromStdString(err);
    {   // the thumbnail for a name given up front
        QPixmap p(image_->text());
        if (!p.isNull()) thumb_->setPixmap(p.scaled(thumb_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    refreshPlan();
}

MixRenderDialog::~MixRenderDialog()
{
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

Mix MixRenderDialog::resultMix() const
{
    Mix m = mix_;
    m.name = name_->text().trimmed().toStdString();
    m.author = author_->text().toStdString();
    m.image = image_->text().toStdString();
    return m;
}

void MixRenderDialog::refreshPlan()
{
    warningList_.clear();
    if (!factsError_.isEmpty()) {
        warningList_ << factsError_;
    } else {
        for (size_t i = 0; i < mix_.tracks.size(); ++i) {
            if (mix_.tracks[i].missing) warningList_ << QStringLiteral("Track %1 is missing").arg(i + 1);
            else if (const std::string p = analysedProblem(mix_.tracks[i].file.parent_path()); !p.empty())
                warningList_ << QStringLiteral("Track %1: %2").arg(i + 1).arg(QString::fromStdString(p));
        }
        const std::vector<EffectiveTrack> eff = effectiveTracks(mix_, facts_);
        for (int i = 0; i + 1 < int(mix_.tracks.size()); ++i)
            if (eff[size_t(i)].ok && eff[size_t(i + 1)].ok && pairTempoDiffers(eff, i))
                warningList_ << QStringLiteral("Tracks %1 and %2 run at tempos more than 2 % apart").arg(i + 1).arg(i + 2);
        if (warningList_.isEmpty()) {
            const std::string bad = validateMix(mix_, facts_);
            if (!bad.empty()) warningList_ << QString::fromStdString(bad);
        }
    }
    QString plan;
    const QString name = name_->text().trimmed().isEmpty() ? QStringLiteral("Studio mix") : name_->text().trimmed();
    if (factsError_.isEmpty()) {
        const std::vector<EffectiveTrack> eff = effectiveTracks(mix_, facts_);
        const double len = mixLengthMs(trackStartMs(mix_, eff), eff);
        const fs::path out = uniqueMixOutPath(folder_->text().toStdString(), name.toStdString());
        plan = QStringLiteral("%1, %2 long, 256 kbps VBR").arg(QString::fromStdString(out.filename().string()), durationText(len));
    }
    planned_->setText(plan);
    warnings_->setText(warningList_.join(QLatin1Char('\n')));
    render_->setEnabled(!running_ && factsError_.isEmpty());
}

void MixRenderDialog::startRender()
{
    if (running_ || !factsError_.isEmpty()) return;
    if (thread_.joinable()) thread_.join();
    const Mix m = resultMix();
    EditPrefs prefs(config_);
    prefs.setAuthor(author_->text());
    prefs.setFolder(folder_->text());
    result_->clear();
    progress_->setValue(0);
    cancel_ = false;
    running_ = true;
    render_->setEnabled(false);
    close_->setText(QStringLiteral("Cancel render"));
    const fs::path folder = folder_->text().toStdString();
    thread_ = std::thread([this, m, folder] {
        MixRenderOptions opts;
        opts.stretchCache = cache_;
        opts.ladspaFactory = [m] { return buildLadspaProcessors(m.filters); };
        const MixRenderProgress progress = [this](const std::string& stage, double done, double total) {
            const QString s = QString::fromStdString(stage);
            const int v = total > 0 ? int(std::min(1.0, done / total) * 1000) : 0;
            QMetaObject::invokeMethod(this, [this, s, v] { stage_->setText(s); progress_->setValue(v); }, Qt::QueuedConnection);
        };
        std::string err;
        fs::path out;
        const bool ok = renderMixToMp3(m, root_, folder, opts, progress, &cancel_, &err, &out);
        const QString msg = QString::fromStdString(err), file = QString::fromStdString(out.string());
        QMetaObject::invokeMethod(this, [this, ok, msg, file] { finish(ok, msg, file); }, Qt::QueuedConnection);
    });
}

void MixRenderDialog::finish(bool ok, const QString& message, const QString& file)
{
    if (thread_.joinable()) thread_.join();
    running_ = false;
    close_->setText(QStringLiteral("Close"));
    render_->setEnabled(true);
    if (ok) {
        outFile_ = file;
        progress_->setValue(1000);
        stage_->clear();
        const QFileInfo fi(file);
        const double mb = double(fi.size()) / (1024.0 * 1024.0);
        result_->setText(QStringLiteral("Saved %1 (%2 MB)").arg(file).arg(mb, 0, 'f', 1));
        emit rendered(file);
    } else {
        stage_->clear();
        result_->setText(message == QLatin1String("cancelled") ? QStringLiteral("Cancelled, nothing was written") : QStringLiteral("Failed: %1").arg(message));
    }
    refreshPlan();
}

void MixRenderDialog::cancelRender() { cancel_ = true; }

void MixRenderDialog::waitForFinish()
{
    for (int i = 0; i < 6000 && running_; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 20), std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void MixRenderDialog::reject()
{
    if (running_) {
        cancel_ = true;                                   // the dialog stays until the worker has stopped
        result_->setText(QStringLiteral("Cancelling..."));
        return;
    }
    QDialog::reject();
}

} // namespace vdj
