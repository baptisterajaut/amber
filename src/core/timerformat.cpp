#include "core/timerformat.h"

#include <QtMath>

#include <cmath>

namespace amber {

namespace {
constexpr double kEpsilon = 1e-6;  // seconds

QString pad2(qint64 v) { return QString::number(v).rightJustified(2, QLatin1Char('0')); }
}  // namespace

long clip_elapsed_frames(double timecode, double fps, long clip_in, long last_frame) {
  long elapsed = long(qRound64(timecode * fps)) - clip_in;
  return qBound(0L, elapsed, qMax(0L, last_frame));
}

double timer_seconds(bool countdown, long elapsed_frames, long last_frame, double fps, double offset) {
  if (fps <= 0) return offset;
  long frames = countdown ? last_frame - elapsed_frames : elapsed_frames;
  return offset + double(frames) / fps;
}

QString format_timer(double seconds, TimerFormat f, bool round_up) {
  const int per_second = (f == TimerFormat::MMSSHundredths) ? 100 : 1;
  const double v =
      round_up ? std::ceil((seconds - kEpsilon) * per_second) : std::floor((seconds + kEpsilon) * per_second);
  const qint64 units = qint64(v);
  const QString sign = units < 0 ? QStringLiteral("-") : QString();
  const qint64 a = qAbs(units);

  switch (f) {
    case TimerFormat::HHMMSS:
      return sign + pad2(a / 3600) + ':' + pad2(a / 60 % 60) + ':' + pad2(a % 60);
    case TimerFormat::Seconds:
      return sign + QString::number(a);
    case TimerFormat::MMSSHundredths: {
      const qint64 s = a / 100;
      return sign + pad2(s / 60) + ':' + pad2(s % 60) + '.' + pad2(a % 100);
    }
    case TimerFormat::MMSS:
    default:
      return sign + pad2(a / 60) + ':' + pad2(a % 60);
  }
}

double progress_fraction(long elapsed_frames, long last_frame) {
  if (last_frame <= 0) return 1.0;
  return qBound(0.0, double(elapsed_frames) / double(last_frame), 1.0);
}

}  // namespace amber
