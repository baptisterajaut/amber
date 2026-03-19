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

#ifndef AUDIO_H
#define AUDIO_H

#include <QVector>
#include <QThread>
#include <QWaitCondition>
#include <QMutex>
#include <QIODevice>
#include <QAudioSink>

#include "core/audio.h"
#include "engine/sequence.h"

class AudioSenderThread : public QThread {
  Q_OBJECT
 public:
  AudioSenderThread();
  void run() override;
  void stop();
  QWaitCondition cond;
  bool close{false};
  QMutex lock;
 public slots:
  void notifyReceiver();
 private:
  QVector<qint16> samples;
  int send_audio_to_output(qint64 offset, int max);
};

extern QAudioSink* audio_output;
extern QIODevice* audio_io_device;
extern AudioSenderThread* audio_thread;
extern bool recording;

void clear_audio_ibuffer();

QObject* GetAudioWakeObject();
void SetAudioWakeObject(QObject* o);
void WakeAudioWakeObject();

int current_audio_freq();

bool is_audio_device_set();

void init_audio();
void stop_audio();
qint64 get_buffer_offset_from_frame(double framerate, long frame);

bool start_recording();
void stop_recording();
QString get_recorded_audio_filename();

#endif  // AUDIO_H
