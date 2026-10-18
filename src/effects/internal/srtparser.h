#ifndef SRTPARSER_H
#define SRTPARSER_H

#include <QByteArray>
#include <QString>
#include <QVector>

// A subtitle entry. Times are the SubtitleEffect's clip-local milliseconds (see cue_ms_at_frame()); line breaks in
// text are stored as "<br>", the way parse_srt() joins multi-line SRT text and the renderer's HTML expects them.
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

// Parses SRT text that is already decoded (parse_srt() reads the file and calls this)
SrtParseResult parse_srt_text(QString content);

// Standard SRT: index from 1, "HH:MM:SS,mmm --> HH:MM:SS,mmm", the text lines ("<br>" back to line breaks), a blank
// line, '\n' line endings. offset_ms is added to every time; a cue that ends at or before 0 after the offset is
// dropped and one that starts before 0 starts at 0. The caller encodes it (UTF-8).
QString write_srt(const QVector<SubtitleCue>& cues, qint64 offset_ms = 0);

// Stable sort by start time (cues starting together keep their order)
void sort_cues(QVector<SubtitleCue>& cues);

// Moves the cues at indexes by delta_ms, all by the same amount: when that would put one of them before 0, the delta is
// reduced so the earliest one starts at 0. Returns the delta applied. Does not re-sort.
qint64 shift_cues(QVector<SubtitleCue>& cues, const QVector<int>& indexes, qint64 delta_ms);

// Times a cue can have: start at or after 0, end after start
bool cue_times_valid(qint64 start_ms, qint64 end_ms);

// "<br>" (any case, also "<br/>") <-> '\n', for showing and editing cue text as plain lines. Blank lines are dropped
// going back to cue text (write_srt() drops them too): in SRT a blank line ends the cue.
QString cue_text_to_lines(const QString& text);
QString cue_lines_to_text(const QString& lines);

// Cue text as the HTML the SubtitleEffect renders: the style tags (<i>, <b>, <u>, <s>, their closers, any case) and
// <br> are kept, every other '<' and '>' is escaped so "a < b and c > d" shows as written instead of being read as a
// tag
QString cue_text_to_html(const QString& text);

// The SubtitleEffect's time in ms at a clip-local frame (playhead - timeline_in + clip_in), computed exactly the way
// rendering does: get_timecode() = frame / frame_rate seconds, then qRound64(timecode * 1000). Clip speed plays no
// part, as it plays none in the effect's timecode.
qint64 cue_ms_at_frame(long local_frame, double frame_rate);

// The first clip-local frame whose time is at or after ms: where a cue starting at ms first shows (and, for an end
// time, the first frame it no longer shows)
long first_frame_at_or_after_ms(qint64 ms, double frame_rate);

#endif  // SRTPARSER_H
