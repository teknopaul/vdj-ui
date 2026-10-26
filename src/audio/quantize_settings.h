#ifndef QUANTIZE_SETTINGS_H
#define QUANTIZE_SETTINGS_H

#include <QObject>

namespace vdj {

class ConfigManager;

// The DJ's global quantize resolution (grid step as a fraction of a beat). Every quantized operation in the app reads this
// one setting; persisted as dj/quantize.
class QuantizeSettings : public QObject {
    Q_OBJECT

public:
    enum Resolution { Quarter = 4, Half = 2, Beat = 1 };    // value = subdivisions per beat

    explicit QuantizeSettings(ConfigManager* config = nullptr, QObject* parent = nullptr);

    Resolution resolution() const { return res_; }
    int subdivisions() const { return int(res_); }
    void setResolution(Resolution r);

    static QString label(Resolution r);          // "1/4 beat", "1/2 beat", "1 beat"

signals:
    void changed(int subdivisions);

private:
    ConfigManager* config_;
    Resolution res_ = Beat;
};

} // namespace vdj

#endif // QUANTIZE_SETTINGS_H
