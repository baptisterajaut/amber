#include "srtparser.h"

#include <QFile>
#include <QRegularExpression>
#include <QStringDecoder>

#include <optional>

namespace {

// Parse "HH:MM:SS,mmm" → milliseconds. Returns -1 on failure.
qint64 parse_timestamp(const QStringView& s) {
  static const QRegularExpression re(
      QStringLiteral("^(\\d{1,2}):(\\d{2}):(\\d{2})[,\\.](\\d{3})$"));
  QRegularExpressionMatch m = re.matchView(s);
  if (!m.hasMatch()) return -1;

  qint64 h = m.captured(1).toLongLong();
  qint64 min = m.captured(2).toLongLong();
  qint64 sec = m.captured(3).toLongLong();
  qint64 ms = m.captured(4).toLongLong();
  return h * 3600000 + min * 60000 + sec * 1000 + ms;
}

// Strip the SRT markup the renderer doesn't draw: <font ...> and </font> (the effect's Font and Color fields own
// those) and ASS override blocks ({\an8}, {\i1}...). The style tags stay (<i>, <b>, <u>, <s>), and so does any other
// '<' or '>': it is text ("a < b"), cue_text_to_html() escapes it for the renderer.
QString strip_unsupported_tags(const QString& text) {
  static const QRegularExpression markup_re(QStringLiteral("</?font\\b[^>]*>|\\{\\\\[^}]*\\}"),
                                            QRegularExpression::CaseInsensitiveOption);
  return QString(text).remove(markup_re);
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

QString decode_srt_bytes(const QByteArray& bytes) {
  // A BOM names the encoding (UTF-8, UTF-16, UTF-32)
  std::optional<QStringConverter::Encoding> bom = QStringConverter::encodingForData(bytes);
  if (bom) return QStringDecoder(*bom).decode(bytes);

  // Stateless: a multibyte sequence cut by the end of the data (a Windows-1252 file ending in "\xE9") is an error, not
  // state kept for a next chunk that never comes
  QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
  QString text = utf8.decode(bytes);
  if (!utf8.hasError()) return text;

  // Not UTF-8: the legacy Western encoding most such SRTs use, the usual fallback of subtitle players.
  // CBA: no charset detection, a Cyrillic/Greek/CJK legacy file decodes as Windows-1252 mojibake; detect (e.g. ICU's
  // ucsdet) if those turn up. Windows-1252 needs Qt's ICU support, Latin-1 (same letters, no 0x80-0x9F punctuation)
  // without it
  QStringDecoder cp1252("Windows-1252");
  if (cp1252.isValid()) return cp1252.decode(bytes);
  return QStringDecoder(QStringConverter::Latin1).decode(bytes);
}

SrtParseResult parse_srt(const QString& filepath) {
  SrtParseResult result;
  result.skipped = 0;

  QFile file(filepath);
  if (!file.open(QIODevice::ReadOnly)) {
    return result;
  }

  QString content = decode_srt_bytes(file.readAll());

  // Skip BOM if present
  if (content.startsWith(QChar(0xFEFF))) {
    content = content.mid(1);
  }

  // Normalize line endings
  content.replace(QLatin1String("\r\n"), QLatin1String("\n"));
  content.replace(QLatin1Char('\r'), QLatin1Char('\n'));

  QStringList lines = content.split(QLatin1Char('\n'));

  enum State { EXPECT_INDEX, EXPECT_TIMECODE, READING_TEXT };
  State state = EXPECT_INDEX;

  qint64 cur_start = 0, cur_end = 0;
  QStringList cur_text_lines;

  auto flush_cue = [&]() {
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
  };

  for (int i = 0; i < lines.size(); i++) {
    QString line = lines[i];

    switch (state) {
      case EXPECT_INDEX: {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;

        bool ok;
        trimmed.toInt(&ok);
        if (ok) {
          state = EXPECT_TIMECODE;
        } else if (parse_timecode_line(trimmed, cur_start, cur_end)) {
          state = READING_TEXT;
        }
        break;
      }
      case EXPECT_TIMECODE: {
        QString trimmed = line.trimmed();
        if (parse_timecode_line(trimmed, cur_start, cur_end)) {
          state = READING_TEXT;
        } else {
          result.skipped++;
          state = EXPECT_INDEX;
        }
        break;
      }
      case READING_TEXT: {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
          flush_cue();
          state = EXPECT_INDEX;
        } else {
          // Check if this line is actually a new cue index (missing blank line)
          bool ok = false;
          trimmed.toInt(&ok);
          if (ok && i + 1 < lines.size()) {
            qint64 next_start, next_end;
            if (parse_timecode_line(lines[i + 1].trimmed(), next_start, next_end)) {
              flush_cue();
              state = EXPECT_TIMECODE;
              break;
            }
          }
          cur_text_lines.append(line);
        }
        break;
      }
    }
  }

  // Flush last cue if file doesn't end with blank line
  if (state == READING_TEXT) {
    flush_cue();
  }

  // Sort by start_ms
  std::sort(result.cues.begin(), result.cues.end(),
            [](const SubtitleCue& a, const SubtitleCue& b) { return a.start_ms < b.start_ms; });

  return result;
}

QString cue_text_to_html(const QString& text) {
  static const QRegularExpression tag_re(QStringLiteral("</?(?:i|b|u|s)>|<br\\s*/?>"),
                                         QRegularExpression::CaseInsensitiveOption);
  auto escaped = [](QStringView s) {
    return s.toString()
        .replace(QLatin1Char('<'), QLatin1String("&lt;"))
        .replace(QLatin1Char('>'), QLatin1String("&gt;"));
  };
  QString html;
  qsizetype pos = 0;
  for (QRegularExpressionMatchIterator it = tag_re.globalMatch(text); it.hasNext();) {
    QRegularExpressionMatch m = it.next();
    html += escaped(QStringView(text).mid(pos, m.capturedStart() - pos));
    html += m.captured();
    pos = m.capturedEnd();
  }
  html += escaped(QStringView(text).mid(pos));
  return html;
}
