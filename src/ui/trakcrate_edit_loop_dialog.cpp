#include "trakcrate_edit_loop_dialog.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

#include "edit_prefs.h"
#include "trakcrate/track.h"

using namespace trakcrate;

namespace vdj {

namespace {

QString importedFrom(const fs::path& trackDir)
{
    Track t;
    return loadTrack(trackDir, &t) ? QString::fromStdString(t.source.get("imported_from")) : QString();
}

QString beatsText(double b) { return b < 1 ? QStringLiteral("1/2 beat") : QStringLiteral("%1 beat%2").arg(b).arg(b > 1 ? "s" : ""); }

} // namespace

EditLoopDialog::EditLoopDialog(const fs::path& trackDir, const BeatGrid& grid, int startBeat, int lenBeats, EditRenderCache* cache, ConfigManager* config, QWidget* parent)
    : QDialog(parent), trackDir_(trackDir), grid_(grid), startBeat_(startBeat), cache_(cache), config_(config)
{
    setWindowTitle(QStringLiteral("Export loop for Acid"));
    const EditPrefs prefs(config);
    name_ = new QLineEdit(QStringLiteral("loop"));
    length_ = new QComboBox;
    for (int bars : kLoopBars) length_->addItem(QStringLiteral("%1 beats").arg(bars * 4), bars * 4);
    length_->setCurrentIndex(std::max(0, length_->findData(lenBeats)));
    algo_ = new QComboBox;
    algo_->addItem(QStringLiteral("cut"), int(EditAlgo::Cut));
    algo_->addItem(QStringLiteral("cross"), int(EditAlgo::Cross));
    algo_->addItem(QStringLiteral("fadeout"), int(EditAlgo::Fadeout));
    param_ = new QComboBox;
    format_ = new QComboBox;
    format_->addItem(QStringLiteral("WAV (16 bit)"), QStringLiteral("wav"));
    format_->addItem(QStringLiteral("MP3 (constant bitrate)"), QStringLiteral("mp3"));
    format_->setCurrentIndex(prefs.format() == QLatin1String("mp3") ? 1 : 0);
    bitrate_ = new QComboBox;
    for (int k : kMp3Kbps) bitrate_->addItem(QStringLiteral("%1 kbps").arg(k), k);
    bitrate_->setCurrentIndex(std::max(0, bitrate_->findData(prefs.mp3Kbps())));
    folder_ = new QLineEdit(prefs.loopFolder(importedFrom(trackDir)));
    choose_ = new QPushButton(QStringLiteral("Choose..."));
    file_ = new QLabel;
    result_ = new QLabel;
    result_->setWordWrap(true);
    preview_ = new QPushButton(QStringLiteral("Preview"));
    export_ = new QPushButton(QStringLiteral("Export"));
    export_->setDefault(true);
    auto* cancel = new QPushButton(QStringLiteral("Close"));

    auto* col = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Name"), name_);
    form->addRow(QStringLiteral("Length"), length_);
    auto* algoRow = new QHBoxLayout;
    algoRow->addWidget(algo_);
    algoRow->addWidget(param_);
    form->addRow(QStringLiteral("Click avoidance"), algoRow);
    form->addRow(QStringLiteral("Format"), format_);
    form->addRow(QStringLiteral("MP3 bitrate"), bitrate_);
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(choose_);
    form->addRow(QStringLiteral("Folder"), folderRow);
    form->addRow(QStringLiteral("File"), file_);
    col->addLayout(form);
    col->addWidget(result_);
    auto* row = new QHBoxLayout;
    row->addWidget(preview_);
    row->addStretch(1);
    row->addWidget(export_);
    row->addWidget(cancel);
    col->addLayout(row);

    connect(algo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { fillParam(); });
    for (QComboBox* b : {length_, format_, param_}) connect(b, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateLabels(); });
    connect(name_, &QLineEdit::textChanged, this, [this] { updateLabels(); });
    connect(folder_, &QLineEdit::textChanged, this, [this] { updateLabels(); });
    connect(choose_, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Folder for loops"), folder_->text());
        if (!d.isEmpty()) folder_->setText(d);
    });
    connect(preview_, &QPushButton::clicked, this, &EditLoopDialog::startPreview);
    connect(export_, &QPushButton::clicked, this, &EditLoopDialog::startExport);
    connect(cancel, &QPushButton::clicked, this, [this] { cancel_ = true; player_.stop(); reject(); });
    fillParam();
}

EditLoopDialog::~EditLoopDialog()
{
    cancel_ = true;
    if (thread_) {
        thread_->wait();
        delete thread_;
    }
    player_.unload();
}

void EditLoopDialog::fillParam()
{
    param_->blockSignals(true);
    param_->clear();
    const EditAlgo a = EditAlgo(algo_->currentData().toInt());
    if (a == EditAlgo::Cut) {
        for (int ms : kCutFadeMs) param_->addItem(QStringLiteral("%1 ms").arg(ms), double(ms));
        param_->setCurrentIndex(param_->findData(10.0));
    } else {
        for (double b : (a == EditAlgo::Cross ? std::vector<double>(std::begin(kCrossBeats), std::end(kCrossBeats)) : std::vector<double>(std::begin(kFadeoutBeats), std::end(kFadeoutBeats))))
            param_->addItem(beatsText(b), b);
        param_->setCurrentIndex(std::max(0, param_->findData(a == EditAlgo::Cross ? 0.5 : 1.0)));
    }
    param_->blockSignals(false);
    updateLabels();
}

LoopExportParams EditLoopDialog::params() const
{
    LoopExportParams p;
    p.name = name_->text().trimmed().toStdString();
    p.author = EditPrefs(config_).author().toStdString();
    p.startBeat = startBeat_;
    p.lenBeats = length_->currentData().toInt();
    p.algo = EditAlgo(algo_->currentData().toInt());
    p.algoParam = param_->currentData().toDouble();
    p.format = format_->currentData().toString() == QLatin1String("mp3") ? OutFormat::Mp3 : OutFormat::Wav;
    p.mp3Kbps = bitrate_->currentData().toInt();
    p.folder = folder_->text().toStdString();
    return p;
}

void EditLoopDialog::updateLabels()
{
    const LoopExportParams p = params();
    bitrate_->setEnabled(p.format == OutFormat::Mp3);
    file_->setText(QString::fromStdString(plannedLoopFileName(trackDir_, p)));
    export_->setEnabled(!thread_ && !p.name.empty());
    preview_->setEnabled(!thread_);
}

void EditLoopDialog::startExport()
{
    if (thread_) return;
    const LoopExportParams p = params();
    if (p.name.empty()) { result_->setText(QStringLiteral("Enter a name for the loop")); return; }
    result_->setText(QStringLiteral("Exporting..."));
    cancel_ = false;
    const BeatGrid g = grid_;
    thread_ = QThread::create([this, p, g] {
        const LoopOutcome o = exportLoop(cache_, trackDir_, g, p, &cancel_);
        QMetaObject::invokeMethod(this, "workDone", Qt::QueuedConnection, Q_ARG(bool, o.ok), Q_ARG(QString, QString::fromStdString(o.error)),
                                  Q_ARG(QString, QString::fromStdString(o.file.string())), Q_ARG(bool, false));
    });
    updateLabels();
    thread_->start();
}

void EditLoopDialog::startPreview()
{
    if (thread_) return;
    const LoopExportParams p = params();
    result_->setText(QStringLiteral("Rendering the loop..."));
    cancel_ = false;
    const BeatGrid g = grid_;
    const QString wav = tmp_.filePath(QStringLiteral("loop-preview.wav"));
    thread_ = QThread::create([this, p, g, wav] {
        std::string err;
        bool ok = ensureSource(cache_, trackDir_, &err, &cancel_);
        if (ok) {
            const DecodedPcm sample = renderLoopSample(*cache_->source, g, p.startBeat, p.lenBeats, p.algo, p.algoParam);
            if (sample.frames() == 0) { ok = false; err = "the loop does not fit the track"; }
            else ok = writeLoopPreviewWav(sample, wav.toStdString(), 8, &err);
        }
        QMetaObject::invokeMethod(this, "workDone", Qt::QueuedConnection, Q_ARG(bool, ok), Q_ARG(QString, QString::fromStdString(err)), Q_ARG(QString, wav),
                                  Q_ARG(bool, true));
    });
    updateLabels();
    thread_->start();
}

void EditLoopDialog::workDone(bool ok, const QString& error, const QString& file, bool preview)
{
    if (thread_) {
        thread_->wait();
        delete thread_;
        thread_ = nullptr;
    }
    updateLabels();
    if (!ok) {
        result_->setText(error);
        emit failed(error);
        return;
    }
    if (preview) {
        QString err;
        if (!player_.load(file, &err)) { result_->setText(err); emit failed(err); return; }
        result_->setText(QStringLiteral("Playing the loop"));
        player_.play();
        emit previewReady();
        return;
    }
    player_.stop();
    EditPrefs prefs(config_);                                           // the folder chosen is the default from now on
    prefs.setLoopFolder(folder_->text());
    prefs.setFormat(format_->currentData().toString());
    prefs.setMp3Kbps(bitrate_->currentData().toInt());
    result_->setText(QStringLiteral("Saved %1").arg(QFileInfo(file).fileName()));
    emit exported(file);
    accept();
}

} // namespace vdj
