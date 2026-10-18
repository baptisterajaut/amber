#include "core/audiometer.h"

#include <cmath>

namespace amber {

double meter_db(double linear) {
  if (linear <= 0.0) return kMeterFloorDb;
  return qMax(kMeterFloorDb, 20.0 * std::log10(linear));
}

void AudioMeterState::push(const QVector<double>& linear_peaks, qint64 now_ms) {
  if (linear_peaks.size() != channels_.size()) {
    channels_ = QVector<Channel>(linear_peaks.size());
    last_ms_ = now_ms;
  }
  advance(now_ms);
  for (int i = 0; i < channels_.size(); i++) {
    Channel& c = channels_[i];
    const double db = meter_db(linear_peaks.at(i));
    c.target = db;
    if (db >= c.level) c.level = db;
    if (c.level >= c.hold) {
      c.hold = c.level;
      c.hold_ms = now_ms;
    }
    if (linear_peaks.at(i) >= 1.0) c.clip = true;
  }
}

void AudioMeterState::advance(qint64 now_ms) {
  if (last_ms_ < 0) last_ms_ = now_ms;
  if (now_ms <= last_ms_) return;
  const qint64 from = last_ms_;
  last_ms_ = now_ms;
  for (Channel& c : channels_) {
    c.level = qMax(c.target, c.level - kMeterFallDbPerSec * double(now_ms - from) / 1000.0);
    // The hold only falls for the part of this step past its 1.5 s
    const qint64 fall_from = qMax(from, c.hold_ms + kMeterHoldMs);
    if (now_ms > fall_from) c.hold -= kMeterFallDbPerSec * double(now_ms - fall_from) / 1000.0;
    c.hold = qMax(c.hold, c.level);
  }
}

void AudioMeterState::clear(qint64 now_ms) {
  if (!channels_.isEmpty()) push(QVector<double>(channels_.size(), 0.0), now_ms);
}

void AudioMeterState::reset_clip() {
  for (Channel& c : channels_) c.clip = false;
}

double AudioMeterState::level_db(int ch) const {
  return (ch >= 0 && ch < channels_.size()) ? channels_.at(ch).level : kMeterFloorDb;
}

double AudioMeterState::peak_hold_db(int ch) const {
  return (ch >= 0 && ch < channels_.size()) ? channels_.at(ch).hold : kMeterFloorDb;
}

bool AudioMeterState::clipped(int ch) const { return ch >= 0 && ch < channels_.size() && channels_.at(ch).clip; }

bool AudioMeterState::animating() const {
  for (const Channel& c : channels_) {
    if (c.level > kMeterFloorDb || c.hold > kMeterFloorDb) return true;
  }
  return false;
}

}  // namespace amber
