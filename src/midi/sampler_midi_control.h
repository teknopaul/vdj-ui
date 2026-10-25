#pragma once

#include "sampler_ctl_map.h"

#include <QObject>
#include <QString>

namespace vdj {

// Turns raw controller events (MidiMapInput::controlEvent / noteEvent) into typed sampler-control signals, using the
// sampler part of an .adjmm file. All slots run on the thread that owns this object (the GUI thread).
class SamplerMidiControl : public QObject {
    Q_OBJECT
public:
    explicit SamplerMidiControl(QObject* parent = nullptr) : QObject(parent) {}

    void setMap(const SamplerCtlMap& map) { map_ = map; }
    const SamplerCtlMap& map() const { return map_; }
    // Reads the .adjmm file; false when it cannot be opened (the old map stays).
    bool loadFromFile(const QString& path);
    QString path() const { return path_; }
    // The example mapping shipped inside the program (a guess at the nanoKONTROL layout, see the file's header).
    static QString examplePath() { return QStringLiteral(":/doc/nanokontrol-studio-sampler.adjmm"); }
    // Copies the example to `path` when nothing is there yet; never overwrites. True when the file exists afterwards.
    static bool installExample(const QString& path);

    void setEnabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }

    // While learning, events are reported through learned() and nothing else happens.
    void setLearning(bool on) { learning_ = on; }
    bool learning() const { return learning_; }

public slots:
    void onControlEvent(int channel, int controller, int value, const QString& source);
    void onNoteEvent(int channel, int note, int velocity, bool on, const QString& source);

signals:
    void buttonPressed(vdj::Control control, int strip);
    void knobMoved(int strip, int value);
    void faderMoved(int strip, int value);
    void trackBack();
    void trackForward();
    void modeToggleRequested();
    // Learn mode: channel 1-16, controller number (or note), isNote, value (CC value / velocity)
    void learned(int channel, int controller, bool isNote, int value);
    // Every event that passed the source filter (for the controller dialog's live line)
    void eventSeen(const QString& description);

private:
    bool sourceAccepted(const QString& source) const;
    void act(const ControlAction& a);

    SamplerCtlMap map_;
    QString path_;
    bool enabled_ = true;
    bool learning_ = false;
};

} // namespace vdj

Q_DECLARE_METATYPE(vdj::Control)
