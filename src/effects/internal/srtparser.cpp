#include "srtparser.h"

#include <QFile>
#include <QRegularExpression>
#include <QStringDecoder>

#include <algorithm>
#include <cmath>
#include <optional>

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
  QFile file(filepath);
  if (!file.open(QIODevice::ReadOnly)) {
    SrtParseResult result;
    result.skipped = 0;
    return result;
  }

  return parse_srt_text(decode_srt_bytes(file.readAll()));
}

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

SrtParseResult parse_srt_text(QString content) {
  SrtParseResult result;
  result.skipped = 0;

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

  sort_cues(result.cues);

  return result;
}

namespace {

// A blank line ends an SRT block: cue text keeps only its non-blank lines
QStringList non_blank_lines(const QString& lines) {
  QStringList kept;
  for (const QString& line : lines.split(QLatin1Char('\n'))) {
    if (!line.trimmed().isEmpty()) kept.append(line);
  }
  return kept;
}

QString format_timestamp(qint64 ms) {
  qint64 h = ms / 3600000;
  ms -= h * 3600000;
  qint64 m = ms / 60000;
  ms -= m * 60000;
  qint64 s = ms / 1000;
  ms -= s * 1000;
  return QStringLiteral("%1:%2:%3,%4")
      .arg(h, 2, 10, QLatin1Char('0'))
      .arg(m, 2, 10, QLatin1Char('0'))
      .arg(s, 2, 10, QLatin1Char('0'))
      .arg(ms, 3, 10, QLatin1Char('0'));
}

}  // namespace

QString write_srt(const QVector<SubtitleCue>& cues, qint64 offset_ms) {
  QString out;
  int index = 1;
  for (const SubtitleCue& cue : cues) {
    qint64 start = cue.start_ms + offset_ms;
    qint64 end = cue.end_ms + offset_ms;
    if (end <= 0) continue;
    if (start < 0) start = 0;

    out += QString::number(index++) + QLatin1Char('\n');
    out += format_timestamp(start) + QLatin1String(" --> ") + format_timestamp(end) + QLatin1Char('\n');
    out += non_blank_lines(cue_text_to_lines(cue.text)).join(QLatin1Char('\n')) + QLatin1String("\n\n");
  }
  return out;
}

void sort_cues(QVector<SubtitleCue>& cues) {
  std::stable_sort(cues.begin(), cues.end(),
                   [](const SubtitleCue& a, const SubtitleCue& b) { return a.start_ms < b.start_ms; });
}

qint64 shift_cues(QVector<SubtitleCue>& cues, const QVector<int>& indexes, qint64 delta_ms) {
  for (int i : indexes) {
    if (i >= 0 && i < cues.size() && cues[i].start_ms + delta_ms < 0) delta_ms = -cues[i].start_ms;
  }
  for (int i : indexes) {
    if (i < 0 || i >= cues.size()) continue;
    cues[i].start_ms += delta_ms;
    cues[i].end_ms += delta_ms;
  }
  return delta_ms;
}

bool cue_times_valid(qint64 start_ms, qint64 end_ms) { return start_ms >= 0 && end_ms > start_ms; }

QString cue_text_to_lines(const QString& text) {
  static const QRegularExpression br_re(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption);
  return QString(text).replace(br_re, QStringLiteral("\n"));
}

QString cue_lines_to_text(const QString& lines) {
  QString text = lines;
  text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
  text.replace(QChar(QChar::LineSeparator), QLatin1Char('\n'));
  text.replace(QChar(QChar::ParagraphSeparator), QLatin1Char('\n'));
  return non_blank_lines(text).join(QLatin1String("<br>"));
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

qint64 cue_ms_at_frame(long local_frame, double frame_rate) {
  // Same operations as get_timecode() (rendering/renderfunctions.cpp) followed by SubtitleEffect's qRound64(* 1000)
  double timecode = double(local_frame) / frame_rate;
  return qRound64(timecode * 1000.0);
}

long first_frame_at_or_after_ms(qint64 ms, double frame_rate) {
  // A rounded estimate, then step: qRound64 can land either side of the exact frame
  long f = long(std::floor(double(ms) * frame_rate / 1000.0)) - 1;
  while (cue_ms_at_frame(f, frame_rate) < ms) f++;
  while (cue_ms_at_frame(f - 1, frame_rate) >= ms) f--;
  return f;
}
