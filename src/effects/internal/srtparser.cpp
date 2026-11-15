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

QString format_timestamp(qint64 ms, QChar ms_separator = QLatin1Char(',')) {
  qint64 h = ms / 3600000;
  ms -= h * 3600000;
  qint64 m = ms / 60000;
  ms -= m * 60000;
  qint64 s = ms / 1000;
  ms -= s * 1000;
  return QStringLiteral("%1:%2:%3%4%5")
      .arg(h, 2, 10, QLatin1Char('0'))
      .arg(m, 2, 10, QLatin1Char('0'))
      .arg(s, 2, 10, QLatin1Char('0'))
      .arg(ms_separator)
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

namespace {

// "[HH:]MM:SS.mmm" -> milliseconds, -1 on failure (WebVTT's hours are optional and may have more than two digits)
qint64 parse_vtt_timestamp(QStringView s) {
  static const QRegularExpression re(QStringLiteral("^(?:(\\d+):)?(\\d{2}):(\\d{2})\\.(\\d{3})$"));
  QRegularExpressionMatch m = re.matchView(s);
  if (!m.hasMatch()) return -1;
  const qint64 h = m.captured(1).isEmpty() ? 0 : m.captured(1).toLongLong();
  const qint64 min = m.captured(2).toLongLong();
  const qint64 sec = m.captured(3).toLongLong();
  if (min > 59 || sec > 59) return -1;
  return h * 3600000 + min * 60000 + sec * 1000 + m.captured(4).toLongLong();
}

// "00:01.000 --> 00:04.000 line:90% align:start": the cue settings after the end time are ignored
bool parse_vtt_timing(QStringView line, qint64& start_ms, qint64& end_ms) {
  const qsizetype arrow = line.indexOf(QLatin1String("-->"));
  if (arrow < 0) return false;
  QStringView left = line.left(arrow).trimmed();
  QStringView right = line.mid(arrow + 3).trimmed();
  for (qsizetype i = 0; i < right.size(); i++) {
    if (right[i].isSpace()) {
      right = right.left(i);
      break;
    }
  }
  start_ms = parse_vtt_timestamp(left);
  end_ms = parse_vtt_timestamp(right);
  return start_ms >= 0 && end_ms > start_ms;
}

// One WebVTT text line as cue text: <i>/<b>/<u> kept without their classes, the other tags dropped, references decoded
QString vtt_line_to_cue_text(const QString& line) {
  static const QRegularExpression drop_re(QStringLiteral("</?(?:c|v|lang|ruby|rt)(?:[.\\s][^>]*)?>|<\\d[^>]*>"),
                                          QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression class_re(QStringLiteral("<(i|b|u)\\.[^>]*>"),
                                           QRegularExpression::CaseInsensitiveOption);
  QString text = QString(line).remove(drop_re);
  text.replace(class_re, QStringLiteral("<\\1>"));
  // CBA: an escaped tag ("&lt;i&gt;") decodes to a real style tag; nobody writes those in subtitles
  text.replace(QLatin1String("&lt;"), QLatin1String("<"))
      .replace(QLatin1String("&gt;"), QLatin1String(">"))
      .replace(QLatin1String("&nbsp;"), QString(QChar(0x00A0)))
      .replace(QLatin1String("&lrm;"), QString(QChar(0x200E)))
      .replace(QLatin1String("&rlm;"), QString(QChar(0x200F)))
      .replace(QLatin1String("&amp;"), QLatin1String("&"));  // last: "&amp;lt;" stays "&lt;"
  return text.trimmed();
}

// Cue text line as WebVTT text: <i>/<b>/<u> kept, <s> dropped, every other '<', '>' and '&' escaped
QString cue_line_to_vtt(const QString& line) {
  static const QRegularExpression tag_re(QStringLiteral("</?(?:i|b|u|s)>"), QRegularExpression::CaseInsensitiveOption);
  auto escaped = [](QStringView s) {
    return s.toString()
        .replace(QLatin1Char('&'), QLatin1String("&amp;"))
        .replace(QLatin1Char('<'), QLatin1String("&lt;"))
        .replace(QLatin1Char('>'), QLatin1String("&gt;"));
  };
  QString out;
  qsizetype pos = 0;
  for (QRegularExpressionMatchIterator it = tag_re.globalMatch(line); it.hasNext();) {
    QRegularExpressionMatch m = it.next();
    out += escaped(QStringView(line).mid(pos, m.capturedStart() - pos));
    const QString tag = m.captured().toLower();
    if (tag != QLatin1String("<s>") && tag != QLatin1String("</s>")) out += tag;
    pos = m.capturedEnd();
  }
  out += escaped(QStringView(line).mid(pos));
  return out;
}

}  // namespace

SrtParseResult parse_vtt_text(QString content) {
  SrtParseResult result;
  result.skipped = 0;
  if (content.startsWith(QChar(0xFEFF))) content = content.mid(1);
  content.replace(QLatin1String("\r\n"), QLatin1String("\n"));
  content.replace(QLatin1Char('\r'), QLatin1Char('\n'));

  // Blocks of lines; the first is the WEBVTT header. Only an empty line ends a block: a line of spaces is cue text
  // (YouTube's automatic captions start their cues with one)
  QVector<QStringList> blocks(1);
  for (const QString& line : content.split(QLatin1Char('\n'))) {
    if (line.isEmpty()) {
      if (!blocks.last().isEmpty()) blocks.append(QStringList());
    } else {
      blocks.last().append(line);
    }
  }
  for (int b = 0; b < blocks.size(); b++) {
    const QStringList& block = blocks[b];
    if (block.isEmpty()) continue;
    const QString first = block.first().trimmed();
    if (b == 0 && first.startsWith(QLatin1String("WEBVTT"))) continue;
    static const QRegularExpression meta_re(QStringLiteral("^(?:NOTE|STYLE|REGION)(?:\\s|$)"));
    if (meta_re.match(first).hasMatch()) continue;

    int timing = -1;
    if (block.first().contains(QLatin1String("-->"))) {
      timing = 0;
    } else if (block.size() > 1 && block[1].contains(QLatin1String("-->"))) {
      timing = 1;  // after the cue identifier
    }
    SubtitleCue cue;
    if (timing < 0 || !parse_vtt_timing(QStringView(block[timing]).trimmed(), cue.start_ms, cue.end_ms)) {
      result.skipped++;
      continue;
    }
    QStringList text;
    for (int i = timing + 1; i < block.size(); i++) {
      const QString t = vtt_line_to_cue_text(block[i]);
      if (!t.isEmpty()) text.append(t);
    }
    if (text.isEmpty()) continue;  // an empty cue shows nothing
    cue.text = text.join(QLatin1String("<br>"));
    result.cues.append(cue);
  }
  sort_cues(result.cues);
  return result;
}

QString write_vtt(const QVector<SubtitleCue>& cues, qint64 offset_ms) {
  QString out = QStringLiteral("WEBVTT\n\n");
  for (const SubtitleCue& cue : cues) {
    qint64 start = cue.start_ms + offset_ms;
    const qint64 end = cue.end_ms + offset_ms;
    if (end <= 0) continue;
    if (start < 0) start = 0;
    out += format_timestamp(start, QLatin1Char('.')) + QLatin1String(" --> ") +
           format_timestamp(end, QLatin1Char('.')) + QLatin1Char('\n');
    QStringList lines;
    for (const QString& line : non_blank_lines(cue_text_to_lines(cue.text))) lines.append(cue_line_to_vtt(line));
    out += lines.join(QLatin1Char('\n')) + QLatin1String("\n\n");
  }
  return out;
}

SrtParseResult parse_subtitle_file(const QString& filepath) {
  QFile file(filepath);
  if (!file.open(QIODevice::ReadOnly)) {
    SrtParseResult result;
    result.skipped = 0;
    return result;
  }
  const QString text = decode_srt_bytes(file.readAll());
  const QStringView body = text.startsWith(QChar(0xFEFF)) ? QStringView(text).mid(1) : QStringView(text);
  return body.startsWith(QLatin1String("WEBVTT")) ? parse_vtt_text(text) : parse_srt_text(text);
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
