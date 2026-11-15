#include "mix_stretch_dock.h"

#include "audio/audio_player.h"
#include "trakcrate/stretch_engine.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QStandardItemModel>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <random>

using namespace trakcrate;

namespace vdj {

// ---- StretchJobQueue ----------------------------------------------------------------------------------------------------------------

StretchJobQueue::StretchJobQueue(StretchCache* cache, QObject* parent) : QObject(parent), cache_(cache) {}

StretchJobQueue::~StretchJobQueue() { cancel(); }

void StretchJobQueue::cancel()
{
    if (!worker_) return;
    cancel_ = true;
    QThread* w = worker_;
    worker_ = nullptr;
    w->wait();
    delete w;
    cancel_ = false;
}

void StretchJobQueue::waitIdle()
{
    if (!worker_) return;
    worker_->wait();
    for (int i = 0; i < 20 && worker_; ++i) QCoreApplication::processEvents();
}

void StretchJobQueue::request(const Mix& mix, const std::vector<TrackFacts>& facts)
{
    cancel();
    struct Job { int lane; fs::path dir; StretchChoice choice; double ratio; };
    std::vector<Job> jobs;
    const std::vector<EffectiveTrack> eff = effectiveTracks(mix, facts);
    for (size_t i = 0; i < eff.size(); ++i) {
        const MixTrack& t = mix.tracks[i];
        if (t.stretchOff || t.missing || std::fabs(eff[i].ratio - 1) < 1e-9 || stretchImpossible(eff[i].ratio)) continue;
        const StretchChoice c = StretchRegistry::instance().resolveChoice(t.stretch, mix.stretch);
        if (cache_->has(t.file.parent_path(), c, eff[i].ratio)) continue;
        jobs.push_back({int(i), t.file.parent_path(), c, eff[i].ratio});
    }
    if (jobs.empty()) {
        emit allDone();
        return;
    }
    for (const Job& j : jobs) emit laneStretching(j.lane, true);
    const int gen = ++generation_;
    worker_ = QThread::create([this, jobs, gen] {
        for (const Job& j : jobs) {
            if (cancel_) return;
            std::string err;
            cache_->get(j.dir, nullptr, j.choice, j.ratio, [this, j](double f) { QMetaObject::invokeMethod(this, [this, j, f] { emit laneProgress(j.lane, f); }, Qt::QueuedConnection); }, &cancel_, &err);
            if (!cancel_) QMetaObject::invokeMethod(this, [this, j, gen] { if (gen == generation_) emit laneStretching(j.lane, false); }, Qt::QueuedConnection);
        }
        if (!cancel_) QMetaObject::invokeMethod(this, [this, gen] { if (gen == generation_) emit allDone(); }, Qt::QueuedConnection);
    });
    worker_->start();
}

// ---- StretchOptionsForm ----------------------------------------------------------------------------------------------------------

StretchOptionsForm::StretchOptionsForm(QWidget* parent) : QWidget(parent)
{
    layout_ = new QFormLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
}

void StretchOptionsForm::setEngine(const QString& engineId, const StretchOptions& values)
{
    if (engine_ == engineId && !fields_.empty()) {
        StretchEngine* cur = StretchRegistry::instance().find(engineId.toStdString());
        bool same = cur != nullptr;
        const StretchOptions now = this->values();
        if (cur)
            for (const StretchOption& o : cur->options())
                if (optionValue(now, o.name, o.def) != optionValue(values, o.name, o.def)) same = false;
        if (same) return;                                   // nothing to rebuild: a widget that is still handling its own signal must not be deleted
    }
    engine_ = engineId;
    fields_.clear();
    // rows are taken out and deleted later: this may run from a signal of one of the widgets being removed
    while (layout_->rowCount() > 0) {
        QFormLayout::TakeRowResult r = layout_->takeRow(0);
        for (QLayoutItem* it : {r.labelItem, r.fieldItem}) {
            if (!it) continue;
            if (QWidget* w = it->widget()) { w->hide(); w->deleteLater(); }
            delete it;
        }
    }
    StretchEngine* e = StretchRegistry::instance().find(engineId.toStdString());
    if (!e) return;
    for (const StretchOption& o : e->options()) {
        const std::string cur = optionValue(values, o.name, o.def);
        QWidget* w = nullptr;
        if (o.kind == StretchOption::Enum) {
            auto* c = new QComboBox;
            for (const std::string& ch : o.choices) c->addItem(QString::fromStdString(ch));
            c->setCurrentText(QString::fromStdString(cur));
            connect(c, QOverload<int>::of(&QComboBox::activated), this, &StretchOptionsForm::changed);
            w = c;
        } else if (o.kind == StretchOption::Bool) {
            auto* c = new QCheckBox;
            c->setChecked(cur == "true" || cur == "1");
            connect(c, &QCheckBox::toggled, this, &StretchOptionsForm::changed);
            w = c;
        } else {
            auto* d = new QDoubleSpinBox;
            d->setRange(o.lo, o.hi);
            d->setValue(std::atof(cur.c_str()));
            connect(d, &QDoubleSpinBox::editingFinished, this, &StretchOptionsForm::changed);
            w = d;
        }
        w->setToolTip(QString::fromStdString(o.help));
        layout_->addRow(QString::fromStdString(o.label.empty() ? o.name : o.label), w);
        fields_.push_back({o.name, w});
    }
}

StretchOptions StretchOptionsForm::values() const
{
    StretchOptions out;
    for (const auto& f : fields_) {
        if (auto* c = qobject_cast<QComboBox*>(f.second)) out[f.first] = c->currentText().toStdString();
        else if (auto* b = qobject_cast<QCheckBox*>(f.second)) out[f.first] = b->isChecked() ? "true" : "false";
        else if (auto* d = qobject_cast<QDoubleSpinBox*>(f.second)) out[f.first] = QString::number(d->value(), 'g', 9).toStdString();
    }
    return out;
}

// ---- MixStretchDock ---------------------------------------------------------------------------------------------------------------

MixStretchDock::MixStretchDock(QWidget* parent) : QWidget(parent)
{
    auto* top = new QVBoxLayout(this);
    top->addWidget(new QLabel(QStringLiteral("<b>Mix default engine</b>")));
    default_ = new QComboBox;
    top->addWidget(default_);
    defaultForm_ = new StretchOptionsForm;
    top->addWidget(defaultForm_);
    top->addWidget(new QLabel(QStringLiteral("<b>Selected track</b>")));
    track_ = new QComboBox;
    top->addWidget(track_);
    trackForm_ = new StretchOptionsForm;
    top->addWidget(trackForm_);
    ratio_ = new QLabel;
    ratio_->setWordWrap(true);
    top->addWidget(ratio_);
    restretch_ = new QPushButton(QStringLiteral("Re-stretch"));
    restretch_->setToolTip(QStringLiteral("Stretch the tracks again with the current engine (cached stretches are reused)"));
    audition_ = new QPushButton(QStringLiteral("Audition..."));
    audition_->setToolTip(QStringLiteral("Compare the engines on the excerpt around the selected track's mix period"));
    top->addWidget(restretch_);
    top->addWidget(audition_);
    top->addStretch(1);

    // engines of the registry, unavailable ones greyed out with the reason as tooltip
    auto fill = [](QComboBox* c, bool withDefault) {
        c->clear();
        if (withDefault) c->addItem(QStringLiteral("Mix default"), QString());
        for (const StretchRegistry::Entry& e : StretchRegistry::instance().list()) {
            c->addItem(QString::fromStdString(e.engine->name()), QString::fromStdString(e.engine->id()));
            const int row = c->count() - 1;
            c->setItemData(row, QString::fromStdString(e.available ? e.engine->description() : "Not available: " + e.why), Qt::ToolTipRole);
            if (!e.available) {
                auto* m = qobject_cast<QStandardItemModel*>(c->model());
                if (m) m->item(row)->setEnabled(false);
            }
        }
        if (withDefault) c->addItem(QStringLiteral("Do not stretch"), QStringLiteral("!off"));
    };
    fill(default_, false);
    fill(track_, true);
    connect(default_, QOverload<int>::of(&QComboBox::activated), this, [this] { defaultForm_->setEngine(default_->currentData().toString(), {}); emitDefault(); });
    connect(defaultForm_, &StretchOptionsForm::changed, this, &MixStretchDock::emitDefault);
    connect(track_, QOverload<int>::of(&QComboBox::activated), this, [this] {
        const QString id = track_->currentData().toString();
        trackForm_->setEngine(id == QLatin1String("!off") ? QString() : id, {});
        emitTrack();
    });
    connect(trackForm_, &StretchOptionsForm::changed, this, &MixStretchDock::emitTrack);
    connect(restretch_, &QPushButton::clicked, this, &MixStretchDock::restretchRequested);
    connect(audition_, &QPushButton::clicked, this, &MixStretchDock::auditionRequested);
}

void MixStretchDock::emitDefault()
{
    if (filling_) return;
    emit defaultChosen(StretchChoice{default_->currentData().toString().toStdString(), defaultForm_->values()});
}

void MixStretchDock::emitTrack()
{
    if (filling_ || lane_ < 0) return;
    const QString id = track_->currentData().toString();
    if (id == QLatin1String("!off")) emit trackChosen(lane_, true, StretchChoice());
    else emit trackChosen(lane_, false, StretchChoice{id.toStdString(), trackForm_->values()});
}

void MixStretchDock::setMix(const Mix& mix, const std::vector<EffectiveTrack>& eff, int lane)
{
    filling_ = true;
    lane_ = lane;
    const std::string defId = mix.stretch.engine.empty() ? StretchRegistry::instance().defaultEngineId() : mix.stretch.engine;
    default_->setCurrentIndex(std::max(0, default_->findData(QString::fromStdString(defId))));
    defaultForm_->setEngine(QString::fromStdString(defId), mix.stretch.options);
    track_->setEnabled(lane >= 0);
    if (lane >= 0 && size_t(lane) < mix.tracks.size()) {
        const MixTrack& t = mix.tracks[size_t(lane)];
        const QString id = t.stretchOff ? QStringLiteral("!off") : QString::fromStdString(t.stretch.engine);
        track_->setCurrentIndex(std::max(0, track_->findData(id)));
        trackForm_->setEngine(t.stretchOff ? QString() : QString::fromStdString(t.stretch.engine), t.stretch.options);
        if (size_t(lane) < eff.size()) {
            const EffectiveTrack& e = eff[size_t(lane)];
            QString text = QStringLiteral("Track %1: ratio %2 (%3%)").arg(lane + 1).arg(e.ratio, 0, 'f', 4).arg((e.ratio - 1) * 100, 0, 'f', 1);
            const std::string w = stretchWarning(e.ratio);
            if (!w.empty()) text += QStringLiteral("\n") + QString::fromStdString(w);
            ratio_->setText(text);
        }
    } else {
        trackForm_->setEngine(QString(), {});
        ratio_->setText(QStringLiteral("Click a track to see its stretch."));
    }
    audition_->setEnabled(lane >= 0);
    filling_ = false;
}

// ---- MixAuditionDialog ------------------------------------------------------------------------------------------------------------

MixAuditionDialog::MixAuditionDialog(const DecodedPcm& excerpt, double ratio, QWidget* parent) : QDialog(parent), excerpt_(excerpt), ratio_(ratio)
{
    setWindowTitle(QStringLiteral("Audition stretch engines"));
    resize(420, 420);
    auto* top = new QVBoxLayout(this);
    top->addWidget(new QLabel(QStringLiteral("Tick the engines to compare (ratio %1). Each is stretched on the excerpt, then you play them one after the other or switch between them.").arg(ratio, 0, 'f', 4)));
    engines_ = new QListWidget;
    for (const StretchRegistry::Entry& e : StretchRegistry::instance().list()) {
        auto* it = new QListWidgetItem(QString::fromStdString(e.engine->name()));
        it->setData(Qt::UserRole, QString::fromStdString(e.engine->id()));
        it->setFlags(e.available ? (it->flags() | Qt::ItemIsUserCheckable) : (it->flags() & ~Qt::ItemIsEnabled));
        it->setCheckState(Qt::Unchecked);
        it->setToolTip(QString::fromStdString(e.available ? e.engine->description() : "Not available: " + e.why));
        engines_->addItem(it);
    }
    top->addWidget(engines_);
    blind_ = new QCheckBox(QStringLiteral("Blind (hide the names until Reveal)"));
    top->addWidget(blind_);
    auto* go = new QPushButton(QStringLiteral("Stretch ticked engines"));
    top->addWidget(go);
    status_ = new QLabel;
    top->addWidget(status_);
    buttonsHost_ = new QWidget;
    buttons_ = new QHBoxLayout(buttonsHost_);
    top->addWidget(buttonsHost_);
    auto* row = new QHBoxLayout;
    auto* seq = new QPushButton(QStringLiteral("Play all in turn"));
    auto* stopB = new QPushButton(QStringLiteral("Stop"));
    reveal_ = new QPushButton(QStringLiteral("Reveal"));
    row->addWidget(seq);
    row->addWidget(stopB);
    row->addWidget(reveal_);
    top->addLayout(row);
    player_ = new AudioPlayer(this);
    dir_ = QDir::tempPath() + QStringLiteral("/vdj-audition-%1").arg(QCoreApplication::applicationPid());
    QDir().mkpath(dir_);
    connect(go, &QPushButton::clicked, this, &MixAuditionDialog::renderTicked);
    connect(stopB, &QPushButton::clicked, this, &MixAuditionDialog::stop);
    poll_ = new QTimer(this);
    poll_->setInterval(250);
    connect(poll_, &QTimer::timeout, this, [this] {
        if (seq_ < 0 || player_->state() == AudioPlayer::Playing) return;
        if (++seq_ >= int(items_.size())) { seq_ = -1; poll_->stop(); return; }
        playIndex(seq_);
    });
    connect(seq, &QPushButton::clicked, this, [this] { if (items_.empty()) return; seq_ = 0; playIndex(0); poll_->start(); });
    connect(reveal_, &QPushButton::clicked, this, [this] {
        QString s;
        for (const Item& it : items_) s += it.label + QStringLiteral(" = ") + QString::fromStdString(it.name) + QStringLiteral("\n");
        status_->setText(s);
    });
}

MixAuditionDialog::~MixAuditionDialog()
{
    player_->stop();
    QDir(dir_).removeRecursively();
}

void MixAuditionDialog::stop()
{
    seq_ = -1;
    if (poll_) poll_->stop();
    player_->stop();
}

void MixAuditionDialog::renderTicked()
{
    stop();
    items_.clear();
    while (QLayoutItem* it = buttons_->takeAt(0)) {
        if (QWidget* w = it->widget()) { w->hide(); w->deleteLater(); }
        delete it;
    }
    std::vector<std::string> ids;
    for (int i = 0; i < engines_->count(); ++i)
        if (engines_->item(i)->checkState() == Qt::Checked) ids.push_back(engines_->item(i)->data(Qt::UserRole).toString().toStdString());
    if (ids.empty()) {
        status_->setText(QStringLiteral("Tick at least one engine."));
        return;
    }
    if (blind_->isChecked()) std::shuffle(ids.begin(), ids.end(), std::mt19937(std::random_device()()));
    int n = 0;
    for (const std::string& id : ids) {
        StretchEngine* e = StretchRegistry::instance().find(id);
        DecodedPcm out;
        std::string err;
        status_->setText(QStringLiteral("Stretching with %1...").arg(QString::fromStdString(e->name())));
        QCoreApplication::processEvents();
        StretchChoice c = StretchRegistry::instance().normalize(StretchChoice{id, {}});
        if (!e->stretch(excerpt_, ratio_, c.options, &out, nullptr, nullptr, &err)) {
            status_->setText(QStringLiteral("%1 failed: %2").arg(QString::fromStdString(e->name()), QString::fromStdString(err)));
            continue;
        }
        Item item;
        item.id = id;
        item.name = e->name();
        item.path = (dir_ + QStringLiteral("/%1.wav").arg(n)).toStdString();
        item.label = QString(QChar('A' + n));
        if (!writeWavFloat(item.path, out, &err)) continue;
        items_.push_back(item);
        ++n;
    }
    for (size_t i = 0; i < items_.size(); ++i) {
        auto* b = new QPushButton(blind_->isChecked() ? items_[i].label : QString::fromStdString(items_[i].name));
        buttons_->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, i] { playIndex(int(i)); });
    }
    status_->setText(QStringLiteral("%1 excerpt(s) ready.").arg(items_.size()));
    reveal_->setEnabled(blind_->isChecked());
}

void MixAuditionDialog::playIndex(int i)
{
    if (i < 0 || size_t(i) >= items_.size()) return;
    QString err;
    if (!player_->load(QString::fromStdString(items_[size_t(i)].path), &err)) {
        seq_ = -1;
        status_->setText(err);
        return;
    }
    player_->play();
}

} // namespace vdj
