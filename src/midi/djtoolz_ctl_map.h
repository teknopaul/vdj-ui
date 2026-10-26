#pragma once

#include "midi_map_input.h"

#include <QHash>
#include <QString>
#include <cmath>
#include <map>
#include <vector>

namespace vdj {

enum class Control { Mute, Solo, Rec, Select, Knob, Fader };

struct ControlAction {
    enum Kind { None, Strip, TrackBack, TrackForward, ModeToggle };
    Kind    kind    = None;
    Control control = Control::Mute;
    int     strip   = 0;                 // 1..8 for Kind Strip
    int     value   = 0;                 // CC value (knob / fader) or the pressed value
};

// The DJ Tools part of an .adjmm file, ready to decode controller events.
class DjtoolzCtlMap {
public:
    static DjtoolzCtlMap fromAdjmm(const AdjmmMap& map);

    // Matching follows adj: channel 0 = any, button ops need their exact value (so a release never matches 127);
    // knob / fader ops match any value. Notes match ops written "N <note>"; only a note on with velocity > 0 presses.
    ControlAction decodeControl(int channel, int controller, int value) const;
    ControlAction decodeNote(int channel, int note, int velocity, bool on) const;

    QString source() const { return directive(QStringLiteral("source")); }
    bool pickup() const { return flag(QStringLiteral("pickup"), true); }
    bool recIsForward() const { return flag(QStringLiteral("rec_is_forward"), true); }
    int  opCount() const { return int(entries_.size()); }

    // Name used in the file: "strip3_fader", "track_back" ...
    static QString opName(ControlAction::Kind kind, Control control, int strip);

private:
    struct Entry {
        ControlAction::Kind kind;
        Control control;
        int strip;
        AdjmmOp op;
    };
    QString directive(const QString& key) const { return directives_.value(key); }
    bool flag(const QString& key, bool def) const;

    std::vector<Entry> entries_;
    QHash<QString, QString> directives_;
};

// An op whose controller is ADJMM_UNSET removes the line of that name.
// Writes the given DJ Tools ops (and "#! key value" directives) into an .adjmm file: the lines of those names are replaced,
// every other line (transport ops, comments) is kept untouched, missing ones are appended; a missing file is created.
bool writeDjtoolzOps(const QString& path, const std::map<QString, AdjmmOp>& ops,
                     const QHash<QString, QString>& directives, QString* error = nullptr);

// "name  ch  ctrl  val" the way adj writes it (channel 0 = any, "N <note>" for notes, "*" for any value).
QString formatAdjmmOp(const QString& name, const AdjmmOp& op);

// 0..127 CC value -> volume 0..127, offset -40..+40 ms (63 and 64 are 0), and back.
inline int volumeFromValue(int v) { return v < 0 ? 0 : v > 127 ? 127 : v; }
inline double offsetMsFromValue(int v) { return std::round((volumeFromValue(v) - 63.5) * 80.0 / 127.0); }
inline int valueFromOffsetMs(double ms) { return volumeFromValue(int(std::lround(ms * 127.0 / 80.0 + 63.5))); }

} // namespace vdj
