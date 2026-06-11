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

#include "engine/sequence.h"
#include "rendering/audio.h"
#include "panels/panels.h"
#include "panels/timeline.h"

#include <QPainter>
#include <QLinearGradient>
#include <QtMath>

#include <QDebug>

#define AUDIO_MONITOR_PEAK_HEIGHT 15
#define AUDIO_MONITOR_GAP 3

extern "C" {
#include "libavformat/avformat.h"
}

AudioMonitor::AudioMonitor(QWidget *parent) :
  QWidget(parent)
{
  clear_timer.setInterval(500);
  connect(&clear_timer, &QTimer::timeout, this, &AudioMonitor::clear);
}

void AudioMonitor::set_value(const QVector<double> &ivalues) {
  values_mutex.lock();
  values = ivalues;
  values_mutex.unlock();

  // Called from audio thread — dispatch repaint to GUI thread.
  // Plain update() paints to the backing store but the RHI compositor
  // may not flush this widget's region when a QRhiWidget is present.
  QMetaObject::invokeMethod(this, "repaint", Qt::QueuedConnection);
  QMetaObject::invokeMethod(&clear_timer, "start", Qt::QueuedConnection);
}

void AudioMonitor::clear() {
  clear_timer.stop();

  values_mutex.lock();
  values.fill(0);  // values are linear peaks: 0 = silence
  values_mutex.unlock();
  repaint();
}

void AudioMonitor::resizeEvent(QResizeEvent *e) {
  gradient = QLinearGradient(QPoint(0, rect().top()), QPoint(0, rect().bottom()));
  gradient.setColorAt(0, Qt::red);
  gradient.setColorAt(0.25, Qt::yellow);
  gradient.setColorAt(1, Qt::green);
  QWidget::resizeEvent(e);
}

void AudioMonitor::paintEvent(QPaintEvent *) {
  if (amber::ActiveSequence != nullptr) {
    QMutexLocker locker(&values_mutex);
    if (values.size() > 0) {
      QPainter p(this);
      int channel_x = AUDIO_MONITOR_GAP;
      int channel_count = values.size();
      int channel_width = (width()/channel_count) - AUDIO_MONITOR_GAP;
      // Bar must not extend past the widget bottom: it used to be created with the
      // full widget height but offset below the peak header, so the bottom 18px of
      // revealed bar rendered off-widget and quiet signals showed nothing at all.
      int bar_height = height() - AUDIO_MONITOR_PEAK_HEIGHT - AUDIO_MONITOR_GAP;
      int i;
      for (i=0;i<channel_count;i++) {
        QRect r(channel_x, AUDIO_MONITOR_PEAK_HEIGHT + AUDIO_MONITOR_GAP, channel_width, bar_height);
        p.fillRect(r, gradient);

        // values are linear peaks (0..1); display on a dB scale with a -60dB floor
        // so attenuated signals stay visible (-30dB = half bar). The old pseudo-linear
        // mapping crushed anything below ~-12dB into the bottom few pixels.
        double peak_linear = values.at(i);
        double filled = 0.0;
        if (peak_linear > 0.0) {
          double db = 20.0 * std::log10(peak_linear);
          filled = qBound(0.0, (db + 60.0) / 60.0, 1.0);
        }
        bool clipping = (filled >= 1.0);

        r.setHeight(qRound(bar_height * (1.0 - filled)));

        QRect peak_rect(channel_x, 0, channel_width, AUDIO_MONITOR_PEAK_HEIGHT);
        if (clipping) {
          p.fillRect(peak_rect, QColor(255, 0, 0));
        } else {
          p.fillRect(peak_rect, QColor(64, 0, 0));
        }

        p.fillRect(r, QColor(0, 0, 0, 160));

        channel_x += channel_width + AUDIO_MONITOR_GAP;
      }
    }
  }
}
