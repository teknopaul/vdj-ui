#ifndef WAV_INFO_H
#define WAV_INFO_H

#include <QString>
#include <QStringList>

namespace vdj {

// Tempo of a sample in BPM, 0 when unknown: the tempo of the RIFF "acid" chunk if the .wav has one, else a number
// followed by "bpm" in the file name ("kick_128bpm.wav", "Loop 93.5 BPM.wav").
double wavBpmFromFile(const QString& path);
double bpmFromFileName(const QString& fileName);

// The .wav files of a folder (full paths), sorted ASCIIbetically (case sensitive), without sub folders.
QStringList wavsInFolder(const QString& folder);
QStringList siblingWavs(const QString& samplePath);

// The next (dir > 0) or previous (dir < 0) .wav in the sample's folder; clamps at the ends (returns the sample
// itself there). A path that is not in its folder (deleted) steps to the first file after / before its sorted position.
// Empty when the folder has no .wav files.
QString stepSample(const QString& samplePath, int dir);

} // namespace vdj

#endif // WAV_INFO_H
