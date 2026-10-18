#ifndef SRTPARSER_H
#define SRTPARSER_H

#include <QByteArray>
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

// Reads the file and decodes it with decode_srt_bytes()
SrtParseResult parse_srt(const QString& filepath);

// SRT file bytes to text: the encoding its BOM names, else UTF-8 when the bytes are valid UTF-8, else Windows-1252
// (Latin-1 if this Qt has no Windows-1252 decoder)
QString decode_srt_bytes(const QByteArray& bytes);

#endif  // SRTPARSER_H
