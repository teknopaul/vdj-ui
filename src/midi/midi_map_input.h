#pragma once

#include <QObject>
#include <QString>
#include <array>
#include <cstdint>

class QThread;

namespace vdj {

// Operation indices (1-based), matching adj_midiin.h
static constexpr int ADJMM_OP_START         = 1;
static constexpr int ADJMM_OP_STOP          = 2;
static constexpr int ADJMM_OP_TOGGLE        = 3;
static constexpr int ADJMM_OP_Q_RESTART     = 4;
static constexpr int ADJMM_OP_NUDGE_FFWD    = 5;
static constexpr int ADJMM_OP_NUDGE_FWD     = 6;
static constexpr int ADJMM_OP_NUDGE_FREW    = 7;
static constexpr int ADJMM_OP_NUDGE_REW     = 8;
// 9 unused
static constexpr int ADJMM_OP_TEMPO_INC_MIN = 10;
static constexpr int ADJMM_OP_TEMPO_INC     = 11;
static constexpr int ADJMM_OP_TEMPO_INC_1   = 12;
static constexpr int ADJMM_OP_TEMPO_DEC_MIN = 13;
static constexpr int ADJMM_OP_TEMPO_DEC     = 14;
static constexpr int ADJMM_OP_TEMPO_DEC_1   = 15;
static constexpr int ADJMM_OP_SLIDER        = 16;
static constexpr int ADJMM_OP_SLIDER_ON     = 17;
static constexpr int ADJMM_OP_SLIDER_OFF    = 18;
static constexpr int ADJMM_OP_COPY_MASTER   = 19;
static constexpr int ADJMM_OP_BECOME_MASTER = 20;
static constexpr int ADJMM_MAX_OP           = 20;

// Special sentinels for channel / controller / value fields
static constexpr uint8_t ADJMM_ANY_CHANNEL        = 0;
static constexpr uint8_t ADJMM_UNSET              = 128;
static constexpr uint8_t ADJMM_ANY_VALUE          = 129;
static constexpr uint8_t ADJMM_NOTEON             = 130;
static constexpr uint8_t ADJMM_NOTEOFF            = 131;
static constexpr uint8_t ADJMM_WHEEL_PLUS_PLUS    = 132;
static constexpr uint8_t ADJMM_WHEEL_MINUS_MINUS  = 133;
static constexpr uint8_t ADJMM_WHEEL_PLUS         = 134;
static constexpr uint8_t ADJMM_WHEEL_MINUS        = 135;

struct AdjmmOp {
    uint8_t channel    = ADJMM_UNSET;
    uint8_t controller = ADJMM_UNSET;
    uint8_t value      = ADJMM_UNSET;
};

struct AdjmmMap {
    std::array<AdjmmOp, ADJMM_MAX_OP> ops;

    // Parse a .adjmm file; returns nullptr on file-open failure (parse
    // errors are logged but do not abort — unknown op names are silently
    // skipped so old files keep working when new ops are added).
    static AdjmmMap* readFromFile(const QString& path);
};

// Reads ALSA MIDI controller / note events in a background thread and routes
// them to signals the main window applies to the VDJ according to a loaded .adjmm map file.
//
// Shares the same file format as the adj CLI tool so /etc/adj/*.adjmm files
// can be used directly for both.
class MidiMapInput : public QObject {
    Q_OBJECT
public:
    explicit MidiMapInput(QObject* parent = nullptr);
    ~MidiMapInput();

    // Load map file and start background reader; returns false on error.
    bool start(const QString& mapPath);
    void stop();
    bool isRunning() const;

    // The ALSA port controllers are subscribed to (-1 until start() succeeded).
    int clientId() const { return clientId_; }
    int portId() const { return portId_; }

signals:
    // Emitted from the reader thread; connect with the default (auto -> queued) connection.
    void startRequested();
    void stopRequested();
    void toggleRequested();
    void restartRequested();
    void nudgeRequested(int ms);
    void tempoAdjustRequested(double deltaBpm);
    void copyMasterRequested();
    void becomeMasterRequested();
    void errorOccurred(const QString& msg);

private:
    int            clientId_ = -1;
    int            portId_   = -1;
    AdjmmMap*      map_    = nullptr;
    class Worker;
    Worker*        worker_ = nullptr;
    QThread*       thread_ = nullptr;
};

} // namespace vdj
