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

#ifndef AUDIOMONITOR_H
#define AUDIOMONITOR_H

#include <QElapsedTimer>
#include <QMutex>
#include <QTimer>
#include <QWidget>

#include "core/audiometer.h"

/**
 * @brief The AudioMonitor class
 *
 * Used to show a visual representation of audio currently playing: a dBFS peak meter per channel with a scale,
 * peak-hold markers and clip indicators that stay lit until the monitor is clicked
 */
class AudioMonitor : public QWidget
{
  Q_OBJECT
public:
  /**
   * @brief AudioMonitor Constructor
   * @param parent
   *
   * QWidget parent object.
   */
  explicit AudioMonitor(QWidget *parent = nullptr);

  /**
   * @brief Set the current audio value
   *
   * The main interface for updating the audio monitor. Using this function will redraw the monitor with the values
   * specified.
   *
   * @param values
   *
   * An array of doubles between 0.0 and 1.0 to display the amplitude. 0.0 is no audio, 1.0 is full volume. Each array
   * entry is a channel and the audio monitor will automatically adjust to the channel count in the array.
   */
  void set_value(const QVector<double>& values);

protected:
  /**
   * @brief Internal paint event
   *
   * Paints the
   */
  void paintEvent(QPaintEvent *) override;

  /**
   * @brief Clicking the monitor resets the clip indicators
   */
  void mousePressEvent(QMouseEvent *) override;

signals:

public slots:

private:
  /**
   * @brief Meter ballistics, fed from the audio thread and advanced by the GUI thread, both under meter_mutex
   */
  amber::AudioMeterState meter;
  QMutex meter_mutex;
  QElapsedTimer clock;

  /**
   * @brief ~30 fps timer letting the meters fall, only running while they are above the floor
   */
  QTimer anim_timer;

  void animate();

  /**
   * @brief Internal timer to clear the audio monitor after a certain amount of time
   *
   * If it doesn't receive any values within a certain amount of time, it's assumed audio is no longer playing and the
   * monitor is cleared.
   */
  QTimer clear_timer;

public slots:
  /**
   * @brief Slot to clear the audio monitor
   */
  void clear();
};

#endif // AUDIOMONITOR_H
