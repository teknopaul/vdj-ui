#include <QtTest/QtTest>

#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QUndoStack>

#include "trakcrate_wav.h"
#include "ui/player_panel.h"
#include "ui/star_rating_widget.h"
#include "ui/trakcrate_analysis.h"
#include "ui/trakcrate_track_editor.h"
#include "ui/waveform_widget.h"
#include <QListWidget>
#include "audio/audio_player.h"
#include "trakcrate/importer.h"
#include "trakcrate/track.h"

#include <fstream>

namespace tc = trakcrate;
using namespace vdj;

class TestTrakcrateEditor : public QObject
{
    Q_OBJECT

    tc::fs::path tmp_;
    tc::Library lib_;
    QString dir_;

    static std::string slurp(const tc::fs::path& p) { std::string s; tc::readFile(p, &s); return s; }

private slots:
    void init()
    {
        tmp_ = tc::makeTempDir("trakcrate-editor");
        QVERIFY(!tmp_.empty());
        QVERIFY(tc::Library::create(tmp_ / "lib") && tc::Library::open(tmp_ / "lib", &lib_));
        tc::DecodedPcm p = tcwav::clicks(120, 0.25, 12);
        tcwav::writeWavMono(tmp_ / "src" / "a.wav", tcwav::mono(p), p.sampleRate);
        tc::ImportOptions io;
        io.analyse = true;
        QVERIFY(tc::importFolder(lib_, tmp_ / "src", io).ok);
        dir_ = QString::fromStdString(lib_.trackDir("src", "a").string());
    }
    void cleanup() { std::error_code ec; tc::fs::remove_all(tmp_, ec); }

    void starsClickKeysAndClear();
    void ratingAndCommentAreWrittenAndUndone();
    void loadingWritesNothing();
    void noTrackMeansDisabled();
    void analysisFromTheMaster();
    void playerLoadsAMasterTrack();
    void cuemarksAreEditedWrittenAndUndone();
    void waveformEditModeReportsWhatTheUserDid();
    void scrollBarPansTheZoomedWaveform();
};

void TestTrakcrateEditor::starsClickKeysAndClear()
{
    StarRatingWidget w;
    w.resize(w.sizeHint());
    QSignalSpy spy(&w, &StarRatingWidget::ratingChanged);
    QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, QPoint(2 * 20 + 5, 8));      // third star
    QCOMPARE(w.rating(), 3);
    QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, QPoint(2 * 20 + 5, 8));      // the same star again clears
    QCOMPARE(w.rating(), 0);
    QTest::keyClick(&w, Qt::Key_4);
    QCOMPARE(w.rating(), 4);
    QTest::keyClick(&w, Qt::Key_0);
    QCOMPARE(w.rating(), 0);
    QCOMPARE(spy.count(), 4);
    w.setRating(9);                                                                    // clamped, and not a user change
    QCOMPARE(w.rating(), 5);
    QCOMPARE(spy.count(), 4);
    w.setEditable(false);
    QTest::keyClick(&w, Qt::Key_1);
    QCOMPARE(w.rating(), 5);
}

void TestTrakcrateEditor::ratingAndCommentAreWrittenAndUndone()
{
    TrakcrateTrackEditor ed;
    QSignalSpy changed(&ed, &TrakcrateTrackEditor::trackChanged);
    ed.setTrack(dir_);
    QVERIFY(ed.setRating(4));
    QCOMPARE(ed.rating(), 4);
    tc::Track t;
    QVERIFY(tc::loadTrack(dir_.toStdString(), &t));
    QCOMPARE(t.rating(), 4);
    QCOMPARE(tc::origin(t.source, "meta"), std::string("edited"));
    QCOMPARE(tc::origin(t.source, "beatgrid"), std::string("generated"));              // only the edited group changes

    QVERIFY(ed.setComment(QStringLiteral("great intro\\nwith a newline")));
    QVERIFY(tc::loadTrack(dir_.toStdString(), &t));
    QCOMPARE(t.comment(), std::string("great intro\\nwith a newline"));

    ed.undoStack()->undo();                                                            // the comment
    QVERIFY(tc::loadTrack(dir_.toStdString(), &t));
    QCOMPARE(t.comment(), std::string(""));
    QCOMPARE(t.rating(), 4);
    ed.undoStack()->undo();                                                            // the rating
    QVERIFY(tc::loadTrack(dir_.toStdString(), &t));
    QCOMPARE(t.rating(), 0);
    QCOMPARE(ed.rating(), 0);
    ed.undoStack()->redo();
    QVERIFY(tc::loadTrack(dir_.toStdString(), &t));
    QCOMPARE(t.rating(), 4);
    QVERIFY(changed.count() >= 5);

    // typing in the field and leaving it commits once; unchanged text writes nothing
    const std::string before = slurp(tc::fs::path(dir_.toStdString()) / "meta.txt");
    ed.commentEdit()->setFocus();
    ed.commentEdit()->setText(QStringLiteral("typed"));
    emit ed.commentEdit()->editingFinished();
    QVERIFY(tc::loadTrack(dir_.toStdString(), &t));
    QCOMPARE(t.comment(), std::string("typed"));
    const int depth = ed.undoStack()->count();
    emit ed.commentEdit()->editingFinished();
    QCOMPARE(ed.undoStack()->count(), depth);
    QVERIFY(slurp(tc::fs::path(dir_.toStdString()) / "meta.txt") != before);

    // the stars of the widget write too
    QTest::keyClick(ed.stars(), Qt::Key_2);
    QVERIFY(tc::loadTrack(dir_.toStdString(), &t));
    QCOMPARE(t.rating(), 2);
}

void TestTrakcrateEditor::loadingWritesNothing()
{
    const tc::fs::path d = dir_.toStdString();
    const std::string meta = slurp(d / "meta.txt"), source = slurp(d / "source.txt"), grid = slurp(d / "beatgrid.txt");
    {
        TrakcrateTrackEditor ed;
        ed.setTrack(dir_);
        ed.setTrack(QString());
        ed.setTrack(dir_);
        PlayerPanel panel;
        QVERIFY(panel.loadTrakcrateTrack(dir_));
        trakcrateTrackAnalysis(dir_);
    }
    QCOMPARE(slurp(d / "meta.txt"), meta);
    QCOMPARE(slurp(d / "source.txt"), source);
    QCOMPARE(slurp(d / "beatgrid.txt"), grid);
}

void TestTrakcrateEditor::noTrackMeansDisabled()
{
    TrakcrateTrackEditor ed;
    QVERIFY(!ed.stars()->isEnabled());
    QVERIFY(!ed.commentEdit()->isEnabled());
    QVERIFY(!ed.setRating(3));
    QVERIFY(!ed.setComment(QStringLiteral("x")));
    ed.setTrack(dir_);
    QVERIFY(ed.stars()->isEnabled());
    ed.setRating(3);
    QVERIFY(ed.undoStack()->canUndo());
    ed.setTrack(QString());                                                            // history belongs to one track
    QVERIFY(!ed.undoStack()->canUndo());
    QVERIFY(!ed.stars()->isEnabled());
    ed.setTrack(QStringLiteral("/nonexistent/track"));                                 // unreadable: shown empty, nothing thrown
    QCOMPARE(ed.rating(), 0);
}

void TestTrakcrateEditor::analysisFromTheMaster()
{
    tc::Track t;
    QVERIFY(tc::loadTrack(dir_.toStdString(), &t));
    t.hasCuemarks = true;
    for (int i = 0; i < 3; ++i) {
        tc::Cuemark m;
        m.ms = 1000.0 * (i + 1);
        m.beat = std::nan("");
        if (i == 1) m.hot = 'B';
        t.cuemarks.marks.push_back(m);
    }
    QVERIFY(tc::saveCuemarks(t));
    const TrackAnalysis a = trakcrateTrackAnalysis(dir_);
    QVERIFY(a.valid);
    QCOMPARE(a.preview.size(), 400);
    QCOMPARE(a.mini.size(), 100);
    QVERIFY(a.color.size() > 1000);                                                    // 12 s at 150 per second
    QVERIFY(std::fabs(a.avgBpm - 120.0) < 1.0);
    QVERIFY(a.beats.size() > 20);
    QCOMPARE(int(a.beats[0].number), 1);
    QCOMPARE(int(a.beats[4].number), 1);
    QVERIFY(std::fabs(a.beats[1].ms - a.beats[0].ms - 500.0) < 5.0);
    QCOMPARE(a.cues.size(), 3);
    QCOMPARE(a.cues[0].hotCue, 0);                                                     // memory points first
    QCOMPARE(a.cues[2].hotCue, 2);                                                     // B = 2
    QCOMPARE(a.cues[2].timeMs, quint32(2000));
    QVERIFY(!trakcrateTrackAnalysis(QStringLiteral("/nonexistent")).valid);
}

void TestTrakcrateEditor::playerLoadsAMasterTrack()
{
    PlayerPanel panel;
    QSignalSpy loaded(&panel, &PlayerPanel::trakcrateTrackLoaded);
    QVERIFY(panel.trakcrateDir().isEmpty());
    QVERIFY(panel.loadTrakcrateTrack(dir_));
    QCOMPARE(panel.trakcrateDir(), dir_);
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(loaded.at(0).at(0).toString(), dir_);
    QVERIFY(panel.hasBeatGrid());
    QVERIFY(std::fabs(panel.originalBpm() - 120.0) < 1.0);
    QVERIFY(panel.playButton()->isEnabled());

    TrackInfo other;                                                                   // a track from elsewhere replaces it
    other.title = "x";
    other.audioPath = QString::fromStdString((tmp_ / "src" / "a.wav").string());
    panel.loadTrack(other);
    QVERIFY(panel.trakcrateDir().isEmpty());
    QCOMPARE(loaded.count(), 2);
    QVERIFY(loaded.at(1).at(0).toString().isEmpty());

    QVERIFY(!panel.loadTrakcrateTrack(QString::fromStdString((tmp_ / "lib").string())));   // no audio there
    QVERIFY(panel.trakcrateDir().isEmpty());
}

void TestTrakcrateEditor::cuemarksAreEditedWrittenAndUndone()
{
    TrakcrateTrackEditor ed;
    QSignalSpy changed(&ed, &TrakcrateTrackEditor::trackChanged);
    QVERIFY(!ed.addCuemark(1000));                                       // no track
    ed.setTrack(dir_);
    const std::string before = slurp(tc::fs::path(dir_.toStdString()) / "cuemarks.txt");
    QVERIFY(ed.addCuemark(2000, "drop"));
    QVERIFY(ed.addCuemark(500));
    QCOMPARE(ed.cuemarkCount(), 2);
    QCOMPARE(ed.cuemarkList()->count(), 2);
    QVERIFY(ed.cuemarkList()->item(1)->text().contains("drop"));
    QVERIFY(!ed.addCuemark(2000.3));                                     // too close to a mark
    QVERIFY(ed.setCuemarkHot(1, 'B'));
    QVERIFY(ed.setCuemarkLabel(0, "intro"));
    QVERIFY(ed.moveCuemark(0, 3000));
    QVERIFY(changed.count() >= 5);

    tc::Track t;
    QVERIFY(tc::loadTrack(dir_.toStdString(), &t));
    QCOMPARE(t.cuemarks.marks.size(), size_t(2));
    QCOMPARE(t.cuemarks.marks[0].label, std::string("drop"));            // sorted: 2000 is now first
    QCOMPARE(int(t.cuemarks.marks[0].hot), int('B'));
    QCOMPARE(t.cuemarks.marks[1].ms, 3000.0);
    QCOMPARE(t.cuemarks.marks[1].label, std::string("intro"));
    QCOMPARE(tc::origin(t.source, "cues"), std::string("edited"));
    QVERIFY(!std::isnan(t.cuemarks.marks[0].beat));                       // the click track has a grid

    QVERIFY(ed.removeCuemark(1));
    QCOMPARE(ed.cuemarkCount(), 1);
    while (ed.undoStack()->canUndo()) ed.undoStack()->undo();            // everything undone: back to no marks
    QCOMPARE(ed.cuemarkCount(), 0);
    QVERIFY(tc::loadTrack(dir_.toStdString(), &t));
    QVERIFY(t.cuemarks.marks.empty());
    ed.undoStack()->redo();
    QCOMPARE(ed.cuemarkCount(), 1);
    Q_UNUSED(before);
}

void TestTrakcrateEditor::scrollBarPansTheZoomedWaveform()
{
    TrakcrateTrackEditor ed;
    ed.setTrack(dir_);
    ed.resize(600, 500);
    ed.show();
    WaveformWidget* w = ed.waveform();
    QScrollBar* sb = ed.waveScrollBar();
    QVERIFY(w->hasWaveform());
    QVERIFY(!sb->isEnabled());                                   // the whole track is showing: nothing to scroll
    const double len = w->analysis().lengthMs();
    w->zoomAt(w->width() / 2, 4.0);                              // zoom in on the middle
    QVERIFY(sb->isEnabled());
    QVERIFY(w->viewStart() > 0);
    QCOMPARE(sb->value(), int(std::lround(w->viewStart())));
    QCOMPARE(double(sb->pageStep()), std::floor(w->viewSpan()));
    sb->setValue(0);                                             // dragging the handle to the left reaches the very start
    QCOMPARE(w->viewStart(), 0.0);
    QVERIFY(w->viewSpan() < len);
    sb->setValue(sb->maximum());
    QVERIFY(qAbs(w->viewStart() + w->viewSpan() - len) < 1.5);   // and to the right, the very end
    w->fit();
    QVERIFY(!sb->isEnabled());
}

void TestTrakcrateEditor::waveformEditModeReportsWhatTheUserDid()
{
    TrakcrateTrackEditor ed;
    ed.setTrack(dir_);
    ed.resize(600, 500);
    ed.show();
    WaveformWidget* w = ed.waveform();
    QVERIFY(w->hasWaveform());
    QVERIFY(!w->cuemarkEditing());
    QSignalSpy added(w, &WaveformWidget::cuemarkAdded);
    QTest::mouseDClick(w, Qt::LeftButton, Qt::NoModifier, QPoint(w->width() / 2, 20));
    QCOMPARE(added.count(), 0);                                            // not in edit mode: a double click fits the view
    ed.editMarksButton()->setChecked(true);
    QVERIFY(w->cuemarkEditing());
    QTest::mouseDClick(w, Qt::LeftButton, Qt::NoModifier, QPoint(w->width() / 2, 20));
    QCOMPARE(added.count(), 1);
    const double ms = added.at(0).at(0).toDouble();
    QVERIFY(ms > 0);
    // snapped to a beat of the 120 bpm grid (500 ms apart, from the first beat)
    const auto& beats = w->analysis().beats;
    bool onBeat = false;
    for (const auto& b : beats) if (std::fabs(b.ms - ms) < 0.01) onBeat = true;
    QVERIFY(onBeat);
    QCOMPARE(w->cuemarks().size(), 1);

    // dragging the mark moves it on release
    QSignalSpy moved(w, &WaveformWidget::cuemarkMoved);
    const int x = w->timeX(w->cuemarks()[0].ms);
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, QPoint(x, 20));
    QTest::mouseMove(w, QPoint(x + 60, 20));
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, QPoint(x + 60, 20));
    QCOMPARE(moved.count(), 1);
    QVERIFY(moved.at(0).at(1).toDouble() > ms);
    QCOMPARE(w->cuemarks().size(), 1);

    // Delete removes the selected mark
    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, QPoint(w->timeX(w->cuemarks()[0].ms), 20));
    QCOMPARE(w->selectedCuemark(), 0);
    QTest::keyClick(w, Qt::Key_Delete);
    QCOMPARE(w->cuemarks().size(), 0);

    // M adds at the play position
    w->setPlayPosition(1750);
    QTest::keyClick(w, Qt::Key_M);
    QCOMPARE(w->cuemarks().size(), 1);
    ed.setTrack(QString());
    QVERIFY(!w->cuemarkEditing());
}

QTEST_MAIN(TestTrakcrateEditor)
#include "test_trakcrate_editor.moc"
