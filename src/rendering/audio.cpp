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

#include "audio.h"

#include "core/appcontext.h"
#include "global/global.h"

#include "engine/sequence.h"

#include "global/config.h"
#include "global/debug.h"
#include "rendering/recordingtap.h"
#include "rendering/renderfunctions.h"

#include <QApplication>
#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QDir>
#include <QFile>
#include <QMediaDevices>
#include <QTimer>
#include <QtMath>

extern "C" {
#include <libavcodec/avcodec.h>
}

QAudioSink* audio_output;
QIODevice* audio_io_device;
bool audio_device_set = false;
QAudioSource* audio_input = nullptr;
RecordingTap* recording_tap = nullptr;
QTimer* audio_notify_timer = nullptr;
QFile output_recording;
bool recording = false;

AudioSenderThread* audio_thread = nullptr;

bool is_audio_device_set() { return audio_device_set; }

QAudioDevice get_audio_device(bool output) {
  QList<QAudioDevice> devs = output ? QMediaDevices::audioOutputs() : QMediaDevices::audioInputs();

  // try to retrieve preferred device from config
  QString preferred_device =
      output ? amber::CurrentConfig.preferred_audio_output : amber::CurrentConfig.preferred_audio_input;
  if (!preferred_device.isEmpty()) {
    for (const auto& dev : devs) {
      // try to match available devices with preferred device
      if (dev.description() == preferred_device) {
        return dev;
      }
    }
  }

  // if no preferred output is set, try to get the default device
  QAudioDevice default_device = output ? QMediaDevices::defaultAudioOutput() : QMediaDevices::defaultAudioInput();
  if (!default_device.isNull()) {
    return default_device;
  }

  // if no default output could be retrieved, just use the first in the list
  if (devs.size() > 0) {
    return devs.at(0);
  }

  // couldn't find any audio devices, return null device
  return QAudioDevice();
}

void init_audio() {
  stop_audio();

  QAudioFormat audio_format;
  audio_format.setSampleRate(amber::CurrentConfig.audio_rate);
  audio_format.setChannelCount(2);
  audio_format.setSampleFormat(QAudioFormat::Int16);

  QAudioDevice info = get_audio_device(true);

  audio_output = new QAudioSink(info, audio_format);
  audio_output->moveToThread(QApplication::instance()->thread());

  // connect
  audio_io_device = audio_output->start();
  if (audio_io_device == nullptr) {
    qWarning() << "Received nullptr audio device. No compatible audio output was found.";
  } else {
    audio_device_set = true;

    // start sender thread
    audio_thread = new AudioSenderThread();

    // QAudioSink has no notify() signal, use a QTimer instead.
    // PreciseTimer is required on Windows — the default CoarseTimer rounds to the
    // OS scheduler tick (~15.6ms), so a 5ms interval would actually fire every
    // 15ms and starve the audio sink (underruns / stutter). On Linux/macOS this
    // is a no-op since timers are already high-resolution.
    audio_notify_timer = new QTimer();
    audio_notify_timer->setInterval(5);
    audio_notify_timer->setTimerType(Qt::PreciseTimer);
    QObject::connect(audio_notify_timer, &QTimer::timeout, audio_thread, &AudioSenderThread::notifyReceiver);
    audio_notify_timer->start();

    audio_thread->start(QThread::TimeCriticalPriority);

    clear_audio_ibuffer();
  }
}

void stop_audio() {
  if (audio_device_set) {
    if (audio_notify_timer != nullptr) {
      audio_notify_timer->stop();
      delete audio_notify_timer;
      audio_notify_timer = nullptr;
    }

    audio_thread->stop();
    audio_thread = nullptr;

    audio_output->stop();
    delete audio_output;
    audio_output = nullptr;
    audio_io_device = nullptr;
    audio_device_set = false;
  }
}

void clear_audio_ibuffer(long new_frame) {
  if (audio_thread != nullptr) audio_thread->lock.lock();
  audio_write_lock.lock();
  // Frame must be stored before read-cursor: a cacher racing past audio_write_lock
  // will compute a conservative offset and stall rather than overwrite stale data.
  audio_ibuffer_frame.store(new_frame);
  memset(audio_ibuffer, 0, audio_ibuffer_size);
  audio_ibuffer_read.store(0);
  audio_write_lock.unlock();
  if (audio_thread != nullptr) audio_thread->lock.unlock();
}

int current_audio_freq() {
  if (audio_rendering) return audio_rendering_rate.load();
  if (audio_output != nullptr) return audio_output->format().sampleRate();
  return 48000;
}

qint64 get_buffer_offset_from_frame(double framerate, long frame) {
  long ibuf_frame = audio_ibuffer_frame.load();
  if (frame >= ibuf_frame) {
    int multiplier = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * 2;  // 2 channels for stereo
    return static_cast<qint64>(qFloor((double(frame - ibuf_frame) / framerate) * current_audio_freq())) * multiplier;
  } else {
    qWarning() << "Invalid values passed to get_buffer_offset_from_frame" << frame << "<" << ibuf_frame;
    return -1;
  }
}

AudioSenderThread::AudioSenderThread() { connect(this, &QThread::finished, this, &QObject::deleteLater); }

void AudioSenderThread::stop() {
  close = true;
  cond.wakeAll();
  wait();
}

void AudioSenderThread::notifyReceiver() { cond.wakeAll(); }

bool AudioSenderThread::scrub_grain_active() {
  if (!audio_scrub_data_ready.load()) return false;
  unsigned id = audio_scrub_id.load();
  if (id != current_scrub_id_) {
    current_scrub_id_ = id;
    scrub_grain_played_ = 0;
    scrub_grain_total_ = scrub_grain_samples(audio_output->format().sampleRate());
    return true;
  }
  if (scrub_grain_played_ < scrub_grain_total_) {
    return true;
  }
  // Grain finished playing — clear the flag so the next seek triggers a new grain
  audio_scrub_data_ready.store(false);
  return false;
}

void AudioSenderThread::run() {
  // start data loop
  send_audio_to_output(0, audio_ibuffer_size);

  lock.lock();
  while (true) {
    cond.wait(&lock);
    if (close) {
      break;
    } else if (amber::app_ctx->isPlaying() || scrub_grain_active()) {
      int written_bytes = 0;

      int adjusted_read_index = audio_ibuffer_read.load() % audio_ibuffer_size;
      int max_write = audio_ibuffer_size - adjusted_read_index;
      int actual_write = send_audio_to_output(adjusted_read_index, max_write);
      written_bytes += actual_write;
      if (actual_write == max_write) {
        // got all the bytes, write again
        written_bytes += send_audio_to_output(0, audio_ibuffer_size);
      }
    }
  }
  lock.unlock();
}

int AudioSenderThread::send_audio_to_output(qint64 offset, int max) {
  audio_write_lock.lock();

  bool scrub_active = (scrub_grain_played_ < scrub_grain_total_);

  qint64 actual_write;
  qint64 consumed;

  if (scrub_active) {
    // Staging buffer: apply Hann window to a copy, write windowed audio to device
    int pairs_in_chunk = max / 4;
    int pairs_to_write = 0;
    int staging_size = pairs_in_chunk * 4;
    staging_buffer_.resize(staging_size);
    memset(staging_buffer_.data(), 0, staging_size);

    for (int p = 0; p < pairs_in_chunk; p++) {
      if (scrub_grain_played_ >= scrub_grain_total_) break;

      double t = (scrub_grain_total_ > 1) ? (double(scrub_grain_played_) / double(scrub_grain_total_ - 1)) : 0.0;
      double w = 0.5 * (1.0 - cos(2.0 * M_PI * t));

      qint64 src = offset + p * 4;
      for (int ch = 0; ch < 2; ch++) {
        qint64 bi = (src + ch * 2) % audio_ibuffer_size;
        qint64 bi1 = (bi + 1) % audio_ibuffer_size;
        qint16 raw = qint16(((audio_ibuffer[bi1] & 0xFF) << 8) | (audio_ibuffer[bi] & 0xFF));
        qint16 windowed = qint16(raw * w);
        staging_buffer_[p * 4 + ch * 2] = char(windowed & 0xFF);
        staging_buffer_[p * 4 + ch * 2 + 1] = char((windowed >> 8) & 0xFF);
      }
      scrub_grain_played_++;
      pairs_to_write = p + 1;
    }

    // Append silence tail when the grain just finished to prevent the audio
    // device from underrunning between grains (causes audible pop/click).
    // The staging buffer is already zero-filled beyond pairs_to_write.
    int tail_pairs = 0;
    if (scrub_grain_played_ >= scrub_grain_total_ && pairs_to_write > 0) {
      int rate = audio_output->format().sampleRate();
      tail_pairs = qMin(rate / 100, pairs_in_chunk - pairs_to_write);  // ~10 ms
    }
    int total_bytes = (pairs_to_write + tail_pairs) * 4;
    actual_write = (total_bytes > 0) ? audio_io_device->write(staging_buffer_.constData(), total_bytes) : 0;
    consumed = pairs_to_write * 4;  // only advance ibuffer read by grain data
  } else {
    // Normal playback: write raw ibuffer directly to device
    actual_write = audio_io_device->write(reinterpret_cast<const char*>(audio_ibuffer) + offset, max);
    consumed = (actual_write > 0) ? actual_write : 0;
  }

  // Compute VU from raw ibuffer over only the consumed range
  if (consumed > 0) {
    int channels = audio_output->format().channelCount();
    qint64 lim = offset + consumed;
    QVector<double> averages;
    averages.resize(channels);
    averages.fill(0);

    int counter = 0;
    qint16 sample;
    for (qint64 i = offset; i < lim; i += 2) {
      sample = qint16(((audio_ibuffer[i + 1] & 0xFF) << 8) | (audio_ibuffer[i] & 0xFF));
      averages[counter] = qMax((double(qAbs(sample)) / 32768.0), averages[counter]);
      counter = (counter + 1) % channels;
    }
    for (int i = 0; i < channels; i++) {
      averages[i] = log_volume(1.0 - (averages[i]));
    }
    if (!recording) amber::app_ctx->setAudioMonitorValues(averages);
  }

  memset(audio_ibuffer + offset, 0, consumed);
  audio_ibuffer_read.fetch_add(consumed);

  audio_write_lock.unlock();

  // Return 0 during scrub to prevent a second wrap-around drain
  return scrub_active ? 0 : actual_write;
}

void int32_to_char_array(qint32 i, char* array) { memcpy(array, &i, 4); }

void write_wave_header(QFile& f, const QAudioFormat& format) {
  qint32 int32bit;
  char arr[4];

  // 4 byte riff header
  f.write("RIFF");

  // 4 byte file size, filled in later
  for (int i = 0; i < 4; i++) f.putChar(0);

  // 4 byte file type header + 4 byte format chunk marker
  f.write("WAVEfmt");
  f.putChar(0x20);

  // 4 byte length of the above format data (always 16 bytes)
  f.putChar(16);
  for (int i = 0; i < 3; i++) f.putChar(0);

  // 2 byte type format (1 is PCM)
  f.putChar(1);
  f.putChar(0);

  // 2 byte channel count
  int32bit = format.channelCount();
  int32_to_char_array(int32bit, arr);
  f.write(arr, 2);

  // 4 byte integer for sample rate
  int32bit = format.sampleRate();
  int32_to_char_array(int32bit, arr);
  f.write(arr, 4);

  // 4 byte integer for bytes per second
  int32bit = (format.sampleRate() * (format.bytesPerSample() * 8) * format.channelCount()) / 8;
  int32_to_char_array(int32bit, arr);
  f.write(arr, 4);

  // 2 byte integer for bytes per sample per channel
  int32bit = ((format.bytesPerSample() * 8) * format.channelCount()) / 8;
  int32_to_char_array(int32bit, arr);
  f.write(arr, 2);

  // 2 byte integer for bits per sample (16)
  int32bit = (format.bytesPerSample() * 8);
  int32_to_char_array(int32bit, arr);
  f.write(arr, 2);

  // data chunk header
  f.write("data");

  // 4 byte integer for data chunk size (filled in later)?
  for (int i = 0; i < 4; i++) f.putChar(0);
}

void write_wave_trailer(QFile& f) {
  char arr[4];

  f.seek(4);

  // 4 bytes for total file size - 8 bytes
  qint32 file_size = qint32(f.size()) - 8;
  int32_to_char_array(file_size, arr);
  f.write(arr, 4);

  f.seek(40);

  // 4 bytes for data chunk size (file size - header)
  file_size = qint32(f.size()) - 44;
  int32_to_char_array(file_size, arr);
  f.write(arr, 4);
}

bool start_recording() {
  if (recording) {
    qWarning() << "start_recording() called while recording is already active";
    return false;
  }
  if (amber::ActiveSequence == nullptr) {
    qCritical() << "No active sequence to record into";
    return false;
  }

  QString audio_path = QCoreApplication::translate("Audio", "%1 Audio").arg(amber::ActiveProjectFilename);
  QDir audio_dir(audio_path);
  if (!audio_dir.exists() && !audio_dir.mkpath(".")) {
    qCritical() << "Failed to create audio directory";
    return false;
  }

  QString audio_file_path;
  int file_number = 0;
  do {
    file_number++;

    QString audio_filename =
        QString("%1.wav").arg(QCoreApplication::translate("Audio", "Recording %1").arg(QString::number(file_number)));

    audio_file_path = audio_dir.filePath(audio_filename);
  } while (QFile(audio_file_path).exists());

  output_recording.setFileName(audio_file_path);
  if (!output_recording.open(QFile::WriteOnly)) {
    qCritical() << "Failed to open output file. Does Olive have permission to write to this directory?";
    return false;
  }

  QAudioFormat audio_format = audio_output->format();
  audio_format.setSampleFormat(QAudioFormat::Int16);
  if (amber::CurrentConfig.recording_mode != audio_format.channelCount()) {
    audio_format.setChannelCount(amber::CurrentConfig.recording_mode);
  }

  QAudioDevice info = get_audio_device(false);

  write_wave_header(output_recording, audio_format);
  audio_input = new QAudioSource(info, audio_format);

  if (recording_tap) {
    recording_tap->close();
    delete recording_tap;
    recording_tap = nullptr;
  }
  recording_tap = new RecordingTap(&output_recording, audio_format.channelCount(),
                                   audio_format.bytesPerSample() * 8);
  recording_tap->open(QIODevice::WriteOnly);
  QObject::connect(recording_tap, &RecordingTap::peaksAvailable, qApp,
                   [](const QVector<double>& peaks) {
                     if (amber::app_ctx) amber::app_ctx->setAudioMonitorValues(peaks);
                   },
                   Qt::QueuedConnection);
  audio_input->start(recording_tap);
  recording = true;

  return true;
}

void stop_recording() {
  if (recording) {
    audio_input->stop();

    if (recording_tap) {
      recording_tap->close();
      delete recording_tap;
      recording_tap = nullptr;
    }

    write_wave_trailer(output_recording);

    output_recording.close();

    delete audio_input;
    audio_input = nullptr;
    recording = false;
  }
}

QString get_recorded_audio_filename() { return output_recording.fileName(); }

QObject* audio_wake_object = nullptr;
QMutex audio_wake_mutex;

QObject* GetAudioWakeObject() {
  audio_wake_mutex.lock();

  QObject* wake_object = audio_wake_object;
  audio_wake_object = nullptr;

  audio_wake_mutex.unlock();

  return wake_object;
}

void SetAudioWakeObject(QObject* o) {
  audio_wake_mutex.lock();
  audio_wake_object = o;
  audio_wake_mutex.unlock();
}

void WakeAudioWakeObject() {
  QObject* audio_wake_object = GetAudioWakeObject();

  if (audio_wake_object != nullptr) {
    QMetaObject::invokeMethod(audio_wake_object, "play_wake", Qt::QueuedConnection);
  }
}
