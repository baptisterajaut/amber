/***

    Olive - Non-Linear Video Editor
    Copyright (C) 2019  Olive Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "audiomonitor.h"

#include "engine/sequence_fwd.h"

#include <QMouseEvent>
#include <QPainter>

#define AUDIO_MONITOR_PEAK_HEIGHT 15
#define AUDIO_MONITOR_GAP 3

namespace {

constexpr int kFrameMs = 33;          // ~30 fps while the meters fall
constexpr int kScaleStepDb = 6;       // scale ticks every 6 dB, 0 down to -54
constexpr int kScaleTickLength = 3;
constexpr int kMinBarWidth = 4;       // narrower than this per channel: the scale gives way to the bars
constexpr double kYellowDb = -18.0;   // Premiere's colour zones: green below, yellow up to -6 dB, red above
constexpr double kRedDb = -6.0;

QColor zone_colour(double db) {
  if (db >= kRedDb) return Qt::red;
  if (db >= kYellowDb) return Qt::yellow;
  return Qt::green;
}

}  // namespace

AudioMonitor::AudioMonitor(QWidget *parent) :
  QWidget(parent)
{
  clock.start();

  clear_timer.setInterval(500);
  connect(&clear_timer, &QTimer::timeout, this, &AudioMonitor::clear);

  anim_timer.setInterval(kFrameMs);
  connect(&anim_timer, &QTimer::timeout, this, &AudioMonitor::animate);
}

void AudioMonitor::set_value(const QVector<double> &ivalues) {
  meter_mutex.lock();
  meter.push(ivalues, clock.elapsed());
  bool moving = meter.animating();
  meter_mutex.unlock();

  // Called from audio thread — dispatch repaint to GUI thread.
  // Plain update() paints to the backing store but the RHI compositor
  // may not flush this widget's region when a QRhiWidget is present.
  QMetaObject::invokeMethod(this, "repaint", Qt::QueuedConnection);
  QMetaObject::invokeMethod(&clear_timer, "start", Qt::QueuedConnection);
  // Not a plain queued start(): pushes come faster than a frame and restarting the timer would keep it from firing.
  // Silence on the floor has nothing to animate: the queued repaint above is enough.
  if (moving) {
    QMetaObject::invokeMethod(this, [this] {
      if (!anim_timer.isActive()) anim_timer.start();
    }, Qt::QueuedConnection);
  }
}

void AudioMonitor::clear() {
  clear_timer.stop();

  // Silence, not a reset: the meters fall to the floor and the clip indicators stay lit
  meter_mutex.lock();
  meter.clear(clock.elapsed());
  meter_mutex.unlock();
  if (!anim_timer.isActive()) anim_timer.start();
  repaint();
}

void AudioMonitor::animate() {
  meter_mutex.lock();
  meter.advance(clock.elapsed());
  bool still_moving = meter.animating();
  meter_mutex.unlock();
  if (!still_moving) anim_timer.stop();
  repaint();
}

void AudioMonitor::mousePressEvent(QMouseEvent *e) {
  meter_mutex.lock();
  meter.reset_clip();
  meter_mutex.unlock();
  repaint();
  e->accept();
}

void AudioMonitor::paintEvent(QPaintEvent *) {
  if (amber::ActiveSequence == nullptr) return;

  // Paint from a copy: set_value() takes the same lock on the audio thread, which must not wait for a paint
  meter_mutex.lock();
  const amber::AudioMeterState state = meter;
  meter_mutex.unlock();

  // CBA: before the first push the channel count is unknown; playback output is always stereo (rendering/audio.cpp)
  int channel_count = state.channel_count() > 0 ? state.channel_count() : 2;

  QPainter p(this);

  QFont scale_font = font();
  if (scale_font.pointSizeF() > 0) {
    scale_font.setPointSizeF(qMax(6.0, scale_font.pointSizeF() * 0.75));
  } else {
    scale_font.setPixelSize(qMax(8, scale_font.pixelSize() * 3 / 4));
  }
  QFontMetrics fm(scale_font);
  int scale_width = fm.horizontalAdvance(QStringLiteral("-54")) + kScaleTickLength + 2;
  if (width() - scale_width - AUDIO_MONITOR_GAP < channel_count * (kMinBarWidth + AUDIO_MONITOR_GAP)) scale_width = 0;

  // Bar must not extend past the widget bottom: it used to be created with the
  // full widget height but offset below the peak header, so the bottom 18px of
  // revealed bar rendered off-widget and quiet signals showed nothing at all.
  int bar_top = AUDIO_MONITOR_PEAK_HEIGHT + AUDIO_MONITOR_GAP;
  int bar_height = height() - bar_top;
  if (bar_height <= 0) return;

  // dB scale with a -60dB floor so attenuated signals stay visible (-30dB = half bar)
  auto db_y = [&](double db) {
    double fraction = qBound(0.0, db / amber::kMeterFloorDb, 1.0);
    return bar_top + qRound(bar_height * fraction);
  };

  if (scale_width > 0) {
    p.setFont(scale_font);
    p.setPen(palette().color(QPalette::WindowText));
    double step_px = bar_height * kScaleStepDb / -amber::kMeterFloorDb;
    int label_every = 1;  // drop labels that would overlap: keep 0 and every other (third...) step
    while (label_every * step_px < fm.height() && label_every < 10) label_every++;
    for (int i = 0; i * kScaleStepDb < -amber::kMeterFloorDb; i++) {
      int db = -i * kScaleStepDb;
      int y = db_y(db);
      if (step_px < 2 && i % label_every != 0) continue;
      p.drawLine(scale_width - kScaleTickLength, y, scale_width - 1, y);
      if (i % label_every == 0) {
        QRect label(0, y - fm.height() / 2, scale_width - kScaleTickLength - 1, fm.height());
        p.drawText(label, Qt::AlignRight | Qt::AlignVCenter, QString::number(db));
      }
    }
  }

  int channel_x = scale_width + AUDIO_MONITOR_GAP;
  int channel_width = (width() - scale_width - AUDIO_MONITOR_GAP) / channel_count - AUDIO_MONITOR_GAP;
  int bar_bottom = bar_top + bar_height;
  int yellow_y = db_y(kYellowDb);
  int red_y = db_y(kRedDb);

  for (int i = 0; i < channel_count; i++) {
    QRect peak_rect(channel_x, 0, channel_width, AUDIO_MONITOR_PEAK_HEIGHT);
    p.fillRect(peak_rect, state.clipped(i) ? QColor(255, 0, 0) : QColor(64, 0, 0));

    // Zones keyed to dB, not to the bar height, so a colour means the same level at any size
    p.fillRect(QRect(channel_x, bar_top, channel_width, red_y - bar_top), Qt::red);
    p.fillRect(QRect(channel_x, red_y, channel_width, yellow_y - red_y), Qt::yellow);
    p.fillRect(QRect(channel_x, yellow_y, channel_width, bar_bottom - yellow_y), Qt::green);

    // Unlit part above the level
    p.fillRect(QRect(channel_x, bar_top, channel_width, db_y(state.level_db(i)) - bar_top), QColor(0, 0, 0, 160));

    double hold = state.peak_hold_db(i);
    if (hold > amber::kMeterFloorDb) {
      int y = qBound(bar_top + 1, db_y(hold), bar_bottom - 1);
      p.fillRect(QRect(channel_x, y - 1, channel_width, 2), zone_colour(hold));
    }

    channel_x += channel_width + AUDIO_MONITOR_GAP;
  }
}
