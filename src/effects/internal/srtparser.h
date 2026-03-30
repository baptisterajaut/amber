#ifndef SRTPARSER_H
#define SRTPARSER_H

#include <QString>
#include <QVector>

struct SubtitleCue {
  qint64 start_ms;
  qint64 end_ms;
  QString text;
};

struct SrtParseResult {
  QVector<SubtitleCue> cues;
  int skipped;
};

SrtParseResult parse_srt(const QString& filepath);

#endif  // SRTPARSER_H
