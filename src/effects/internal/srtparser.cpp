#include "srtparser.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace {

// Parse "HH:MM:SS,mmm" → milliseconds. Returns -1 on failure.
qint64 parse_timestamp(const QStringView& s) {
  static const QRegularExpression re(QStringLiteral("^(\\d{1,2}):(\\d{2}):(\\d{2})[,\\.](\\d{3})$"));
  QRegularExpressionMatch m = re.matchView(s);
  if (!m.hasMatch()) return -1;

  qint64 h = m.captured(1).toLongLong();
  qint64 min = m.captured(2).toLongLong();
  qint64 sec = m.captured(3).toLongLong();
  qint64 ms = m.captured(4).toLongLong();
  return h * 3600000 + min * 60000 + sec * 1000 + ms;
}

// Strip unsupported HTML tags, keep <i>, <b>, <u> and their closing tags.
QString strip_unsupported_tags(const QString& text) {
  static const QRegularExpression tag_re(QStringLiteral("</?(?!/?(?:i|b|u)>)[^>]*>"),
                                         QRegularExpression::CaseInsensitiveOption);
  return QString(text).remove(tag_re);
}

// Parse timecode line: "00:00:05,000 --> 00:00:08,500"
bool parse_timecode_line(const QString& line, qint64& start_ms, qint64& end_ms) {
  int arrow = line.indexOf(QLatin1String("-->"));
  if (arrow < 0) return false;

  QStringView left = QStringView(line).left(arrow).trimmed();
  QStringView right = QStringView(line).mid(arrow + 3).trimmed();

  // Strip positional metadata after the end timestamp
  int space = right.indexOf(QLatin1Char(' '));
  if (space > 0) right = right.left(space);

  start_ms = parse_timestamp(left);
  end_ms = parse_timestamp(right);
  return start_ms >= 0 && end_ms >= 0 && end_ms > start_ms;
}

}  // namespace

namespace {

enum SrtState { EXPECT_INDEX, EXPECT_TIMECODE, READING_TEXT };

struct SrtParserCtx {
  SrtParseResult& result;
  SrtState state{EXPECT_INDEX};
  qint64 cur_start{0};
  qint64 cur_end{0};
  QStringList cur_text_lines;

  void flush_cue() {
    while (!cur_text_lines.isEmpty() && cur_text_lines.last().trimmed().isEmpty()) {
      cur_text_lines.removeLast();
    }
    if (!cur_text_lines.isEmpty()) {
      QString joined;
      for (int i = 0; i < cur_text_lines.size(); i++) {
        if (i > 0) joined += QLatin1String("<br>");
        joined += strip_unsupported_tags(cur_text_lines[i].trimmed());
      }
      SubtitleCue cue;
      cue.start_ms = cur_start;
      cue.end_ms = cur_end;
      cue.text = joined;
      result.cues.append(cue);
    }
    cur_text_lines.clear();
  }

  // Returns false to signal outer loop should `continue` (skip to next line).
  bool handle_expect_index(const QString& line) {
    QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) return false;
    bool ok = false;
    trimmed.toInt(&ok);
    if (ok) {
      state = EXPECT_TIMECODE;
    } else if (parse_timecode_line(trimmed, cur_start, cur_end)) {
      state = READING_TEXT;
    }
    return true;
  }

  void handle_expect_timecode(const QString& line) {
    QString trimmed = line.trimmed();
    if (parse_timecode_line(trimmed, cur_start, cur_end)) {
      state = READING_TEXT;
    } else {
      result.skipped++;
      state = EXPECT_INDEX;
    }
  }

  // Returns true if the outer loop should `break` out of the switch (implicit in for-loop).
  bool handle_reading_text(const QString& line, int i, const QStringList& lines) {
    QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
      flush_cue();
      state = EXPECT_INDEX;
      return false;
    }
    // Check if this line is actually a new cue index (missing blank line)
    bool ok = false;
    trimmed.toInt(&ok);
    if (ok && i + 1 < lines.size()) {
      qint64 next_start, next_end;
      if (parse_timecode_line(lines[i + 1].trimmed(), next_start, next_end)) {
        flush_cue();
        state = EXPECT_TIMECODE;
        return true;
      }
    }
    cur_text_lines.append(line);
    return false;
  }
};

}  // namespace

SrtParseResult parse_srt(const QString& filepath) {
  SrtParseResult result;
  result.skipped = 0;

  QFile file(filepath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return result;
  }

  QTextStream stream(&file);
  QString content = stream.readAll();

  // Skip BOM if present
  if (content.startsWith(QChar(0xFEFF))) content = content.mid(1);

  // Normalize line endings
  content.replace(QLatin1String("\r\n"), QLatin1String("\n"));
  content.replace(QLatin1Char('\r'), QLatin1Char('\n'));

  QStringList lines = content.split(QLatin1Char('\n'));

  SrtParserCtx ctx{result};

  for (int i = 0; i < lines.size(); i++) {
    const QString& line = lines[i];
    switch (ctx.state) {
      case EXPECT_INDEX:
        if (!ctx.handle_expect_index(line)) continue;
        break;
      case EXPECT_TIMECODE:
        ctx.handle_expect_timecode(line);
        break;
      case READING_TEXT:
        ctx.handle_reading_text(line, i, lines);
        break;
    }
  }

  // Flush last cue if file doesn't end with blank line
  if (ctx.state == READING_TEXT) ctx.flush_cue();

  // Sort by start_ms
  std::sort(result.cues.begin(), result.cues.end(),
            [](const SubtitleCue& a, const SubtitleCue& b) { return a.start_ms < b.start_ms; });

  return result;
}
