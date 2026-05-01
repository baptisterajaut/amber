#include "recordingtap.h"

#include <QtMath>
#include <cstdint>

QVector<double> recording_tap_compute_peaks(const QByteArray& chunk, int ch, int bits) {
  QVector<double> peaks(ch, 0.0);
  if (bits != 16 || ch <= 0) return peaks;
  const int16_t* s = reinterpret_cast<const int16_t*>(chunk.constData());
  const int sample_count = chunk.size() / sizeof(int16_t);
  for (int i = 0; i < sample_count; ++i) {
    const int c = i % ch;
    const double v = qAbs(double(s[i]) / 32768.0);
    if (v > peaks[c]) peaks[c] = v;
  }
  return peaks;
}

RecordingTap::RecordingTap(QIODevice* underlying, int ch, int bits, QObject* parent)
    : QIODevice(parent), underlying_(underlying), channel_count_(ch), sample_size_bits_(bits) {}

qint64 RecordingTap::writeData(const char* data, qint64 len) {
  qint64 written = underlying_->write(data, len);
  QVector<double> peaks =
      recording_tap_compute_peaks(QByteArray::fromRawData(data, len), channel_count_, sample_size_bits_);
  emit peaksAvailable(peaks);
  return written;
}
