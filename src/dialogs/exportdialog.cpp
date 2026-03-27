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

#include "exportdialog.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtMath>

#include "dialogs/advancedvideodialog.h"
#include "dialogs/exportsavepresetdialog.h"
#include "global/config.h"
#include "global/global.h"
#include "panels/panels.h"
#include "rendering/audio.h"
#include "rendering/exportthread.h"
#include "rendering/renderfunctions.h"
#include "ui/mainwindow.h"
#include "ui/viewerwidget.h"

enum ExportFormats {
  FORMAT_3GPP,
  FORMAT_AIFF,
  FORMAT_APNG,
  FORMAT_AVI,
  FORMAT_DNXHD,
  FORMAT_AC3,
  FORMAT_FLV,
  FORMAT_GIF,
  FORMAT_IMG,
  FORMAT_MP2,
  FORMAT_MP3,
  FORMAT_MPEG1,
  FORMAT_MPEG2,
  FORMAT_MPEG4,
  FORMAT_MPEGTS,
  FORMAT_MKV,
  FORMAT_OGG,
  FORMAT_MOV,
  FORMAT_WAV,
  FORMAT_WEBM,
  FORMAT_WMV,
  FORMAT_SIZE
};

ExportDialog::ExportDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Export \"%1\"").arg(amber::ActiveSequence->name));
  setup_ui();

  rangeCombobox->setCurrentIndex(0);
  if (amber::ActiveSequence->using_workarea) {
    rangeCombobox->setEnabled(true);
    rangeCombobox->setCurrentIndex(1);
  }

  format_strings.resize(FORMAT_SIZE);
  format_strings[FORMAT_3GPP] = "3GPP";
  format_strings[FORMAT_AIFF] = "AIFF";
  format_strings[FORMAT_APNG] = "Animated PNG";
  format_strings[FORMAT_AVI] = "AVI";
  format_strings[FORMAT_DNXHD] = "DNxHD";
  format_strings[FORMAT_AC3] = "Dolby Digital (AC3)";
  format_strings[FORMAT_FLV] = "FLV";
  format_strings[FORMAT_GIF] = "GIF";
  format_strings[FORMAT_IMG] = "Image Sequence";
  format_strings[FORMAT_MP2] = "MP2 Audio";
  format_strings[FORMAT_MP3] = "MP3 Audio";
  format_strings[FORMAT_MPEG1] = "MPEG-1 Video";
  format_strings[FORMAT_MPEG2] = "MPEG-2 Video";
  format_strings[FORMAT_MPEG4] = "MPEG-4 Video";
  format_strings[FORMAT_MPEGTS] = "MPEG-TS";
  format_strings[FORMAT_MKV] = "Matroska MKV";
  format_strings[FORMAT_OGG] = "Ogg";
  format_strings[FORMAT_MOV] = "QuickTime MOV";
  format_strings[FORMAT_WAV] = "WAVE Audio";
  format_strings[FORMAT_WEBM] = "WebM";
  format_strings[FORMAT_WMV] = "Windows Media";

  for (int i = 0; i < FORMAT_SIZE; i++) {
    formatCombobox->addItem(format_strings[i]);
  }
  formatCombobox->setCurrentIndex(FORMAT_MPEG4);

  // default to sequence's native dimensions
  widthSpinbox->setValue(amber::ActiveSequence->width);
  heightSpinbox->setValue(amber::ActiveSequence->height);
  samplingRateSpinbox->setValue(amber::ActiveSequence->audio_frequency);
  framerateSpinbox->setValue(amber::ActiveSequence->frame_rate);

  // set some advanced defaults
  vcodec_params.threads = 0;
}

void ExportDialog::add_codec_to_combobox(QComboBox* box, enum AVCodecID codec) {
  QString codec_name;

  const AVCodec* codec_info = avcodec_find_encoder(codec);

  if (codec_info == nullptr) {
    codec_name = tr("Unknown codec name %1").arg(static_cast<int>(codec));
  } else {
    codec_name = codec_info->long_name;
  }

  box->addItem(codec_name, codec);
}

void ExportDialog::format_changed(int index) {
  vcodecCombobox->clear();
  acodecCombobox->clear();

  int default_vcodec = 0;
  int default_acodec = 0;

  switch (index) {
    case FORMAT_3GPP:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_MPEG4);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_H264);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_H265);

      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_AAC);

      default_vcodec = 1;
      break;
    case FORMAT_AIFF:
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_PCM_S16LE);
      break;
    case FORMAT_APNG:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_APNG);
      break;
    case FORMAT_AVI:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_H264);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_MPEG4);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_MJPEG);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_MSVIDEO1);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_RAWVIDEO);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_HUFFYUV);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_DVVIDEO);

      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_AAC);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_AC3);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_FLAC);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP2);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP3);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_PCM_S16LE);

      default_vcodec = 3;
      default_acodec = 5;
      break;
    case FORMAT_DNXHD:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_DNXHD);

      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_PCM_S16LE);
      break;
    case FORMAT_AC3:
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_AC3);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_EAC3);
      break;
    case FORMAT_FLV:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_FLV1);

      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP3);
      break;
    case FORMAT_GIF:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_GIF);
      break;
    case FORMAT_IMG:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_BMP);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_MJPEG);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_JPEG2000);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_PSD);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_PNG);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_TIFF);

      default_vcodec = 4;
      break;
    case FORMAT_MP2:
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP2);
      break;
    case FORMAT_MP3:
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP3);
      break;
    case FORMAT_MPEG1:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_MPEG1VIDEO);

      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_AC3);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP2);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP3);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_PCM_S16LE);

      default_acodec = 1;
      break;
    case FORMAT_MPEG2:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_MPEG2VIDEO);

      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_AC3);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP2);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP3);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_PCM_S16LE);

      default_acodec = 1;
      break;
    case FORMAT_MPEG4:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_MPEG4);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_H264);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_H265);

      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_AAC);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_AC3);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP2);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP3);

      default_vcodec = 1;
      break;
    case FORMAT_MPEGTS:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_MPEG2VIDEO);

      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_AAC);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_AC3);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP2);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP3);

      default_acodec = 2;
      break;
    case FORMAT_MKV:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_MPEG4);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_H264);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_H265);

      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_AAC);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_AC3);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_EAC3);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_FLAC);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP2);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP3);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_OPUS);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_PCM_S16LE);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_VORBIS);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_WAVPACK);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_WMAV1);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_WMAV2);

      default_vcodec = 1;
      break;
    case FORMAT_OGG:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_THEORA);

      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_OPUS);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_VORBIS);

      default_acodec = 1;
      break;
    case FORMAT_MOV:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_QTRLE);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_MPEG4);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_H264);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_H265);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_MJPEG);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_PRORES);

      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_AAC);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_AC3);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP2);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_MP3);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_PCM_S16LE);

      default_vcodec = 2;
      break;
    case FORMAT_WAV:
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_PCM_S16LE);
      break;
    case FORMAT_WEBM:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_VP8);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_VP9);

      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_OPUS);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_VORBIS);

      default_vcodec = 1;
      break;
    case FORMAT_WMV:
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_WMV1);
      add_codec_to_combobox(vcodecCombobox, AV_CODEC_ID_WMV2);

      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_WMAV1);
      add_codec_to_combobox(acodecCombobox, AV_CODEC_ID_WMAV2);

      default_vcodec = 1;
      default_acodec = 1;
      break;
    default:
      qCritical() << "Invalid format selection - this is a bug, please inform the developers";
  }

  vcodecCombobox->setCurrentIndex(default_vcodec);
  acodecCombobox->setCurrentIndex(default_acodec);

  bool video_enabled = vcodecCombobox->count() != 0;
  bool audio_enabled = acodecCombobox->count() != 0;
  videoGroupbox->setChecked(video_enabled);
  audioGroupbox->setChecked(audio_enabled);
  videoGroupbox->setEnabled(video_enabled);
  audioGroupbox->setEnabled(audio_enabled);
}

void ExportDialog::export_thread_finished() {
  // Determine if the export succeeded: no error and not cancelled
  bool succeeded = !export_thread_->WasInterrupted() && export_thread_->GetError().isEmpty();
  export_success_ = succeeded;

  // If it failed and we didn't cancel it, it must have errored out. Show an error message.
  if (!succeeded && !export_thread_->WasInterrupted()) {
    QMessageBox::critical(this, tr("Export Failed"), tr("Export failed - %1").arg(export_thread_->GetError()),
                          QMessageBox::Ok);
  }

  // Restore rendering state on the main thread (ExportThread can't do this —
  // set_rendering_state(false) starts autorecovery_timer, a QTimer owned by
  // the main thread, which triggers "Timers cannot be started from another
  // thread" if called from ExportThread).
  amber::Global->set_rendering_state(false);

  // Clear audio buffer
  clear_audio_ibuffer();

  // Re-enable/disable UI widgets based on the rendering state
  prep_ui_for_render(false);

  // Restore playhead to its pre-export position
  if (panel_sequence_viewer != nullptr && amber::ActiveSequence) {
    panel_sequence_viewer->seek(pre_export_playhead_);
  }

  // Update the application UI
  update_ui(false);

  // Disconnect cancel button from export thread
  disconnect(renderCancel, &QPushButton::clicked, export_thread_, &ExportThread::Interrupt);

  // Free the export thread
  export_thread_->deleteLater();

  // Free the GL fallback surface (must be deleted on GUI thread — we're on it)
  delete export_gl_surface_;
  export_gl_surface_ = nullptr;

  // If the export succeeded, close the dialog
  if (succeeded) {
    accept();
  }
}

void ExportDialog::prep_ui_for_render(bool r) {
  export_button->setEnabled(!r);
  cancel_button->setEnabled(!r);
  videoGroupbox->setEnabled(!r);
  audioGroupbox->setEnabled(!r);
  renderCancel->setEnabled(r);
}

static QString GetFormatExtension(int format, int video_codec, bool has_video, bool has_audio) {
  switch (format) {
    case FORMAT_3GPP:
      return QStringLiteral("3gp");
    case FORMAT_AIFF:
      return QStringLiteral("aiff");
    case FORMAT_APNG:
      return QStringLiteral("apng");
    case FORMAT_AVI:
      return QStringLiteral("avi");
    case FORMAT_DNXHD:
      return QStringLiteral("mxf");
    case FORMAT_AC3:
      return QStringLiteral("ac3");
    case FORMAT_FLV:
      return QStringLiteral("flv");
    case FORMAT_GIF:
      return QStringLiteral("gif");
    case FORMAT_MP3:
      return QStringLiteral("mp3");
    case FORMAT_MPEGTS:
      return QStringLiteral("ts");
    case FORMAT_OGG:
      return QStringLiteral("ogg");
    case FORMAT_MOV:
      return QStringLiteral("mov");
    case FORMAT_WAV:
      return QStringLiteral("wav");
    case FORMAT_WEBM:
      return QStringLiteral("webm");
    case FORMAT_IMG:
      switch (video_codec) {
        case AV_CODEC_ID_BMP:
          return QStringLiteral("bmp");
        case AV_CODEC_ID_MJPEG:
          return QStringLiteral("jpg");
        case AV_CODEC_ID_JPEG2000:
          return QStringLiteral("jp2");
        case AV_CODEC_ID_PSD:
          return QStringLiteral("psd");
        case AV_CODEC_ID_PNG:
          return QStringLiteral("png");
        case AV_CODEC_ID_TIFF:
          return QStringLiteral("tif");
        default:
          return QString();
      }
    case FORMAT_MPEG1:
      if (has_video && !has_audio) return QStringLiteral("m1v");
      if (!has_video && has_audio) return QStringLiteral("m1a");
      return QStringLiteral("mpg");
    case FORMAT_MPEG2:
      if (has_video && !has_audio) return QStringLiteral("m2v");
      if (!has_video && has_audio) return QStringLiteral("m2a");
      return QStringLiteral("mpg");
    case FORMAT_MPEG4:
      if (has_video && !has_audio) return QStringLiteral("m4v");
      if (!has_video && has_audio) return QStringLiteral("m4a");
      return QStringLiteral("mp4");
    case FORMAT_MKV:
      return has_video ? QStringLiteral("mkv") : QStringLiteral("mka");
    case FORMAT_WMV:
      return has_video ? QStringLiteral("wmv") : QStringLiteral("wma");
    default:
      return QString();
  }
}

void ExportDialog::StartExport() {
  if (widthSpinbox->value() % 2 == 1 || heightSpinbox->value() % 2 == 1) {
    QMessageBox::critical(this, tr("Invalid dimensions"),
                          tr("Export width and height must both be even numbers/divisible by 2."), QMessageBox::Ok);
    return;
  }

  QString ext = GetFormatExtension(formatCombobox->currentIndex(), vcodecCombobox->currentData().toInt(),
                                   videoGroupbox->isChecked(), audioGroupbox->isChecked());
  if (ext.isEmpty()) {
    qCritical() << "Invalid format/codec selection - this is a bug";
    QMessageBox::critical(this, tr("Invalid format"),
                          tr("Couldn't determine output format. This is a bug, please contact the developers."),
                          QMessageBox::Ok);
    return;
  }
  QString filename = QFileDialog::getSaveFileName(this, tr("Export Media"), "",
                                                  format_strings[formatCombobox->currentIndex()] + " (*." + ext + ")");
  if (!filename.isEmpty()) {
    if (!filename.endsWith("." + ext, Qt::CaseInsensitive)) {
      filename += "." + ext;
    }

    if (formatCombobox->currentIndex() == FORMAT_IMG) {
      int ext_location = filename.lastIndexOf('.');
      if (ext_location > filename.lastIndexOf('/')) {
        filename.insert(ext_location, 'd');
        filename.insert(ext_location, '5');
        filename.insert(ext_location, '0');
        filename.insert(ext_location, '%');
      }
    }

    // Set up export parameters to send to the ExportThread
    ExportParams params;
    params.filename = filename;
    params.video_enabled = videoGroupbox->isChecked();
    if (params.video_enabled) {
      params.video_codec = vcodecCombobox->currentData().toInt();
      params.video_width = widthSpinbox->value();
      params.video_height = heightSpinbox->value();
      params.video_frame_rate = framerateSpinbox->value();
      params.video_compression_type = compressionTypeCombobox->currentData().toInt();
      params.video_bitrate = videobitrateSpinbox->value();
    }
    params.audio_enabled = audioGroupbox->isChecked();
    if (params.audio_enabled) {
      params.audio_codec = acodecCombobox->currentData().toInt();
      params.audio_sampling_rate = samplingRateSpinbox->value();
      params.audio_bitrate = audiobitrateSpinbox->value();
    }

    params.start_frame = 0;
    params.end_frame = amber::ActiveSequence->getEndFrame();  // entire sequence
    if (rangeCombobox->currentIndex() == 1) {
      params.start_frame = qMax(amber::ActiveSequence->workarea_in, params.start_frame);
      params.end_frame = qMin(amber::ActiveSequence->workarea_out, params.end_frame);
    }

    // Create export thread
    export_thread_ = new ExportThread(params, vcodec_params, this);

    // Pre-create a GL fallback surface on the GUI thread for the export's
    // RenderThread.  Avoids "QWindow outside gui thread" warning on Wayland.
    if (amber::CurrentRuntimeConfig.rhi_backend == RhiBackend::OpenGL) {
      export_gl_surface_ = QRhiGles2InitParams::newFallbackSurface();
      export_thread_->setGlFallbackSurface(export_gl_surface_);
    }

    // Connect export thread signals/slots
    connect(export_thread_, &QThread::finished, this, &ExportDialog::export_thread_finished);
    connect(export_thread_, &ExportThread::ProgressChanged, this, &ExportDialog::update_progress_bar);
    connect(renderCancel, &QPushButton::clicked, export_thread_, &ExportThread::Interrupt);

    // Close all effects in effect controls (prevents UI threading issues)
    panel_effect_controls->Clear();

    // Pause playback and seek BEFORE starting the export thread.
    // These trigger widget repaints (timeline scroll, play button icon,
    // viewer update) which must happen on the main/GUI thread.  Running
    // them on ExportThread causes QRhiWidget::paintEvent on the wrong
    // thread → "Cannot make QOpenGLContext current in a different thread".
    if (panel_sequence_viewer != nullptr) {
      panel_sequence_viewer->pause();
    }

    // Set audio rendering rate before seek — seek can trigger
    // compose_audio() which reads current_audio_freq().
    if (params.audio_enabled) {
      audio_rendering_rate = params.audio_sampling_rate;
    }

    amber::Global->set_rendering_state(true);

    // Save playhead position so we can restore it after export
    pre_export_playhead_ = amber::ActiveSequence ? amber::ActiveSequence->playhead : 0;

    // Seek to export start frame (safe now — audio_rendering is true so
    // ViewerWidget::frame_update() will early-return, but the UI updates
    // like timeline scroll still happen correctly on the main thread).
    if (panel_sequence_viewer != nullptr && panel_sequence_viewer->seq != nullptr) {
      panel_sequence_viewer->seek(params.start_frame);
    }

    // Close all currently open clips
    close_active_clips(amber::ActiveSequence.get());

    amber::Global->save_autorecovery_file();

    prep_ui_for_render(true);

    total_export_time_start = QDateTime::currentMSecsSinceEpoch();

    export_thread_->start();
  }
}

void ExportDialog::update_progress_bar(int value, qint64 remaining_ms) {
  if (value == 100) {
    // if value is 100%, show total render time rather than remaining
    remaining_ms = QDateTime::currentMSecsSinceEpoch() - total_export_time_start;
  }

  // convert ms to H:MM:SS
  int seconds = qFloor(remaining_ms * 0.001) % 60;
  int minutes = qFloor(remaining_ms / 60000) % 60;
  int hours = qFloor(remaining_ms / 3600000);

  if (value == 100) {
    // show value as "total"
    progressBar->setFormat(tr("%p% (Total: %1:%2:%3)")
                               .arg(QString::number(hours), QString::number(minutes).rightJustified(2, '0'),
                                    QString::number(seconds).rightJustified(2, '0')));
  } else {
    // show value as "remaining"
    progressBar->setFormat(tr("%p% (ETA: %1:%2:%3)")
                               .arg(QString::number(hours), QString::number(minutes).rightJustified(2, '0'),
                                    QString::number(seconds).rightJustified(2, '0')));
  }

  progressBar->setValue(value);
}

void ExportDialog::vcodec_changed(int index) {
  compressionTypeCombobox->clear();

  if (vcodecCombobox->count() > 0) {
    if (vcodecCombobox->itemData(index) == AV_CODEC_ID_H264 || vcodecCombobox->itemData(index) == AV_CODEC_ID_H265) {
      compressionTypeCombobox->setEnabled(true);
      compressionTypeCombobox->addItem(tr("Quality-based (Constant Rate Factor)"), COMPRESSION_TYPE_CFR);
      //		compressionTypeCombobox->addItem("File size-based (Two-Pass)", COMPRESSION_TYPE_TARGETSIZE);
      //		compressionTypeCombobox->addItem("Average bitrate (Two-Pass)", COMPRESSION_TYPE_TARGETBR);
    } else {
      compressionTypeCombobox->addItem(tr("Constant Bitrate"), COMPRESSION_TYPE_CBR);
      compressionTypeCombobox->setCurrentIndex(0);
      compressionTypeCombobox->setEnabled(false);
    }

    // set default pix_fmt for this codec
    const AVCodec* codec_info = avcodec_find_encoder(static_cast<AVCodecID>(vcodecCombobox->itemData(index).toInt()));
    if (codec_info == nullptr) {
      QMessageBox::critical(this, tr("Invalid Codec"),
                            tr("Failed to find a suitable encoder for this codec. Export will likely fail."));
    } else {
      const enum AVPixelFormat* pix_fmts = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 0, 0)
      int num_pix_fmts = 0;
      if (avcodec_get_supported_config(nullptr, codec_info, AV_CODEC_CONFIG_PIX_FORMAT, 0, (const void**)&pix_fmts,
                                       &num_pix_fmts) == 0 &&
          pix_fmts && num_pix_fmts > 0) {
#else
      pix_fmts = codec_info->pix_fmts;
      if (pix_fmts && pix_fmts[0] != AV_PIX_FMT_NONE) {
#endif
        vcodec_params.pix_fmt = pix_fmts[0];
      } else {
        vcodec_params.pix_fmt = -1;
        QMessageBox::critical(this, tr("Invalid Codec"),
                              tr("Failed to find pixel format for this encoder. Export will likely fail."));
      }
    }
  }
}

void ExportDialog::comp_type_changed(int) {
  videobitrateSpinbox->setToolTip("");
  videobitrateSpinbox->setMinimum(0);
  videobitrateSpinbox->setMaximum(99.99);
  switch (compressionTypeCombobox->currentData().toInt()) {
    case COMPRESSION_TYPE_CBR:
    case COMPRESSION_TYPE_TARGETBR:
      videoBitrateLabel->setText(tr("Bitrate (Mbps):"));
      videobitrateSpinbox->setValue(qMax(0.5, (double)qRound((0.01528 * amber::ActiveSequence->height) - 4.5)));
      break;
    case COMPRESSION_TYPE_CFR:
      videoBitrateLabel->setText(tr("Quality (CRF):"));
      videobitrateSpinbox->setValue(36);
      videobitrateSpinbox->setMaximum(51);
      videobitrateSpinbox->setToolTip(
          tr("Quality Factor:\n\n0 = lossless\n17-18 = visually lossless (compressed, but unnoticeable)\n23 = high "
             "quality\n51 = lowest quality possible"));
      break;
    case COMPRESSION_TYPE_TARGETSIZE:
      videoBitrateLabel->setText(tr("Target File Size (MB):"));
      videobitrateSpinbox->setValue(100);
      break;
  }
}

void ExportDialog::open_advanced_video_dialog() {
  AdvancedVideoDialog avd(this, static_cast<AVCodecID>(vcodecCombobox->currentData().toInt()), vcodec_params);
  avd.exec();
}

void ExportDialog::setup_ui() {
  QVBoxLayout* verticalLayout = new QVBoxLayout(this);

  // Preset selector row
  QHBoxLayout* preset_layout = new QHBoxLayout();
  preset_layout->addWidget(new QLabel(tr("Preset:"), this));
  preset_combo_ = new QComboBox(this);
  PopulatePresetCombo();
  preset_layout->addWidget(preset_combo_);
  QPushButton* save_preset_btn = new QPushButton(tr("Save Preset..."), this);
  connect(save_preset_btn, &QPushButton::clicked, this, &ExportDialog::save_preset_clicked);
  preset_layout->addWidget(save_preset_btn);
  verticalLayout->addLayout(preset_layout);
  // Connect AFTER PopulatePresetCombo() to avoid spurious signals during init
  connect(preset_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &ExportDialog::preset_selected);

  QHBoxLayout* format_layout = new QHBoxLayout();

  format_layout->addWidget(new QLabel(tr("Format:"), this));

  formatCombobox = new QComboBox();
  format_layout->addWidget(formatCombobox);

  verticalLayout->addLayout(format_layout);

  QHBoxLayout* range_layout = new QHBoxLayout();

  range_layout->addWidget(new QLabel(tr("Range:"), this));

  rangeCombobox = new QComboBox(this);
  rangeCombobox->addItem(tr("Entire Sequence"));
  rangeCombobox->addItem(tr("In to Out"));

  range_layout->addWidget(rangeCombobox);

  verticalLayout->addLayout(range_layout);

  videoGroupbox = new QGroupBox(this);
  videoGroupbox->setTitle(tr("Video"));
  videoGroupbox->setFlat(false);
  videoGroupbox->setCheckable(true);

  QGridLayout* videoGridLayout = new QGridLayout(videoGroupbox);

  videoGridLayout->addWidget(new QLabel(tr("Codec:"), this), 0, 0, 1, 1);
  vcodecCombobox = new QComboBox(videoGroupbox);
  videoGridLayout->addWidget(vcodecCombobox, 0, 1, 1, 1);

  videoGridLayout->addWidget(new QLabel(tr("Width:"), this), 1, 0, 1, 1);
  widthSpinbox = new QSpinBox(videoGroupbox);
  widthSpinbox->setMaximum(16777216);
  videoGridLayout->addWidget(widthSpinbox, 1, 1, 1, 1);

  videoGridLayout->addWidget(new QLabel(tr("Height:"), this), 2, 0, 1, 1);
  heightSpinbox = new QSpinBox(videoGroupbox);
  heightSpinbox->setMaximum(16777216);
  videoGridLayout->addWidget(heightSpinbox, 2, 1, 1, 1);

  videoGridLayout->addWidget(new QLabel(tr("Frame Rate:"), this), 3, 0, 1, 1);
  framerateSpinbox = new QDoubleSpinBox(videoGroupbox);
  framerateSpinbox->setMaximum(60);
  framerateSpinbox->setValue(0);
  videoGridLayout->addWidget(framerateSpinbox, 3, 1, 1, 1);

  videoGridLayout->addWidget(new QLabel(tr("Compression Type:"), this), 4, 0, 1, 1);
  compressionTypeCombobox = new QComboBox(videoGroupbox);
  videoGridLayout->addWidget(compressionTypeCombobox, 4, 1, 1, 1);

  videoBitrateLabel = new QLabel(videoGroupbox);
  videoGridLayout->addWidget(videoBitrateLabel, 5, 0, 1, 1);
  videobitrateSpinbox = new QDoubleSpinBox(videoGroupbox);
  videobitrateSpinbox->setMaximum(100);
  videobitrateSpinbox->setValue(2);
  videoGridLayout->addWidget(videobitrateSpinbox, 5, 1, 1, 1);

  QPushButton* advanced_video_button = new QPushButton(tr("Advanced"));
  connect(advanced_video_button, &QPushButton::clicked, this, &ExportDialog::open_advanced_video_dialog);
  videoGridLayout->addWidget(advanced_video_button, 6, 1);

  verticalLayout->addWidget(videoGroupbox);

  audioGroupbox = new QGroupBox(this);
  audioGroupbox->setTitle(tr("Audio"));
  audioGroupbox->setCheckable(true);

  QGridLayout* audioGridLayout = new QGridLayout(audioGroupbox);

  audioGridLayout->addWidget(new QLabel(tr("Codec:"), this), 0, 0, 1, 1);
  acodecCombobox = new QComboBox(audioGroupbox);
  audioGridLayout->addWidget(acodecCombobox, 0, 1, 1, 1);

  audioGridLayout->addWidget(new QLabel(tr("Sampling Rate:"), this), 1, 0, 1, 1);
  samplingRateSpinbox = new QSpinBox(audioGroupbox);
  samplingRateSpinbox->setMaximum(96000);
  samplingRateSpinbox->setValue(0);
  audioGridLayout->addWidget(samplingRateSpinbox, 1, 1, 1, 1);

  audioGridLayout->addWidget(new QLabel(tr("Bitrate (Kbps/CBR):"), this), 3, 0, 1, 1);
  audiobitrateSpinbox = new QSpinBox(audioGroupbox);
  audiobitrateSpinbox->setMaximum(320);
  audiobitrateSpinbox->setValue(256);
  audioGridLayout->addWidget(audiobitrateSpinbox, 3, 1, 1, 1);

  verticalLayout->addWidget(audioGroupbox);

  QHBoxLayout* progressLayout = new QHBoxLayout();
  progressBar = new QProgressBar(this);
  progressBar->setFormat("%p% (ETA: 0:00:00)");
  progressBar->setEnabled(false);
  progressBar->setValue(0);
  progressLayout->addWidget(progressBar);

  renderCancel = new QPushButton(this);
  renderCancel->setIcon(QIcon(":/icons/error.svg"));
  renderCancel->setEnabled(false);
  progressLayout->addWidget(renderCancel);

  verticalLayout->addLayout(progressLayout);

  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  export_button = new QPushButton(this);
  export_button->setText("Export");
  connect(export_button, &QPushButton::clicked, this, &ExportDialog::StartExport);

  buttonLayout->addWidget(export_button);

  cancel_button = new QPushButton(this);
  cancel_button->setText("Cancel");
  connect(cancel_button, &QPushButton::clicked, this, &QDialog::reject);

  buttonLayout->addWidget(cancel_button);

  buttonLayout->addStretch();

  verticalLayout->addLayout(buttonLayout);

  connect(formatCombobox, qOverload<int>(&QComboBox::currentIndexChanged), this, &ExportDialog::format_changed);
  connect(compressionTypeCombobox, qOverload<int>(&QComboBox::currentIndexChanged), this,
          &ExportDialog::comp_type_changed);
  connect(vcodecCombobox, qOverload<int>(&QComboBox::currentIndexChanged), this, &ExportDialog::vcodec_changed);
}

QString ExportDialog::UserPresetDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/exportpresets";
}

QStringList ExportDialog::PresetDirs() {
  QStringList dirs;
  QDir app_dir(QCoreApplication::applicationDirPath());
  dirs.append(app_dir.filePath("exportpresets"));                        // Windows / dev build
  dirs.append(app_dir.filePath("../ExportPresets"));                     // macOS .app bundle
  dirs.append(app_dir.filePath("../share/amber-editor/exportpresets"));  // Linux FHS / AppImage
  dirs.append(UserPresetDir());                                          // user presets (all platforms)
  return dirs;
}

QString ExportDialog::SanitizePresetName(const QString& name) {
  QString s = name;
  s.replace(QRegularExpression("[^a-zA-Z0-9 _-]"), "_");
  return s;
}

QStringList ExportDialog::GetPresetList() {
  QSet<QString> seen;
  QStringList result;
  for (const QString& path : PresetDirs()) {
    QDir dir(path);
    if (!dir.exists()) continue;
    for (const QString& entry : dir.entryList(QDir::Files, QDir::Name)) {
      if (!seen.contains(entry)) {
        seen.insert(entry);
        result.append(entry);
      }
    }
  }
  result.sort();
  return result;
}

void ExportDialog::PopulatePresetCombo() {
  preset_combo_->blockSignals(true);
  preset_combo_->clear();
  preset_combo_->addItem(tr("Default"));
  QStringList presets = GetPresetList();
  if (!presets.isEmpty()) {
    preset_combo_->insertSeparator(1);
    preset_combo_->addItems(presets);
  }
  preset_combo_->blockSignals(false);
}

void ExportDialog::preset_selected(int index) {
  if (index <= 0) {
    // "Default" selected - reset to built-in defaults
    formatCombobox->setCurrentIndex(FORMAT_MPEG4);
    widthSpinbox->setValue(amber::ActiveSequence->width);
    heightSpinbox->setValue(amber::ActiveSequence->height);
    framerateSpinbox->setValue(amber::ActiveSequence->frame_rate);
    samplingRateSpinbox->setValue(amber::ActiveSequence->audio_frequency);
    audiobitrateSpinbox->setValue(256);
    return;
  }
  QString name = preset_combo_->itemText(index);
  if (!name.isEmpty()) {
    LoadPreset(name);
  }
}

void ExportDialog::save_preset_clicked() {
  ExportSavePresetDialog dlg(this, GetPresetList());
  if (dlg.exec() == QDialog::Accepted) {
    SavePreset(dlg.preset_name());
    PopulatePresetCombo();
  }
}

void ExportDialog::SavePreset(const QString& name) {
  QString dir_path = UserPresetDir();
  QDir dir(dir_path);
  if (!dir.exists()) {
    dir.mkpath(".");
  }

  QString filepath = dir_path + "/" + SanitizePresetName(name);
  QFile file(filepath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::critical(this, tr("Error"), tr("Could not save preset file:\n%1").arg(filepath));
    return;
  }

  QXmlStreamWriter stream(&file);
  stream.setAutoFormatting(true);
  stream.writeStartDocument();
  stream.writeStartElement("export_preset");

  // Format
  stream.writeTextElement("format", QString::number(formatCombobox->currentIndex()));

  // Range
  stream.writeTextElement("range", QString::number(rangeCombobox->currentIndex()));

  // Video settings
  stream.writeTextElement("video_enabled", QString::number(videoGroupbox->isChecked() ? 1 : 0));
  stream.writeTextElement("video_codec_index", QString::number(vcodecCombobox->currentIndex()));
  stream.writeTextElement("width", QString::number(widthSpinbox->value()));
  stream.writeTextElement("height", QString::number(heightSpinbox->value()));
  stream.writeTextElement("frame_rate", QString::number(framerateSpinbox->value(), 'f', 6));
  stream.writeTextElement("compression_type_index", QString::number(compressionTypeCombobox->currentIndex()));
  stream.writeTextElement("video_bitrate", QString::number(videobitrateSpinbox->value(), 'f', 6));

  // Audio settings
  stream.writeTextElement("audio_enabled", QString::number(audioGroupbox->isChecked() ? 1 : 0));
  stream.writeTextElement("audio_codec_index", QString::number(acodecCombobox->currentIndex()));
  stream.writeTextElement("sampling_rate", QString::number(samplingRateSpinbox->value()));
  stream.writeTextElement("audio_bitrate", QString::number(audiobitrateSpinbox->value()));

  stream.writeEndElement();  // export_preset
  stream.writeEndDocument();

  file.close();
}

void ExportDialog::LoadPreset(const QString& name) {
  QString filepath;
  for (const QString& dir : PresetDirs()) {
    QString candidate = dir + "/" + name;
    if (QFile::exists(candidate)) {
      filepath = candidate;
      break;
    }
  }
  if (filepath.isEmpty()) {
    QMessageBox::critical(this, tr("Error"), tr("Preset not found: %1").arg(name));
    return;
  }
  QFile file(filepath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::critical(this, tr("Error"), tr("Could not open preset file:\n%1").arg(filepath));
    return;
  }

  QXmlStreamReader stream(&file);

  int format_index = -1;
  int range_index = -1;
  int video_enabled = -1;
  int video_codec_index = -1;
  int width = -1;
  int height = -1;
  double frame_rate = -1;
  int compression_type_index = -1;
  double video_bitrate = -1;
  int audio_enabled = -1;
  int audio_codec_index = -1;
  int sampling_rate = -1;
  int audio_bitrate = -1;

  while (!stream.atEnd()) {
    stream.readNext();
    if (stream.isStartElement()) {
      QString tag = stream.name().toString();
      if (tag == "format") {
        format_index = stream.readElementText().toInt();
      } else if (tag == "range") {
        range_index = stream.readElementText().toInt();
      } else if (tag == "video_enabled") {
        video_enabled = stream.readElementText().toInt();
      } else if (tag == "video_codec_index") {
        video_codec_index = stream.readElementText().toInt();
      } else if (tag == "width") {
        width = stream.readElementText().toInt();
      } else if (tag == "height") {
        height = stream.readElementText().toInt();
      } else if (tag == "frame_rate") {
        frame_rate = stream.readElementText().toDouble();
      } else if (tag == "compression_type_index") {
        compression_type_index = stream.readElementText().toInt();
      } else if (tag == "video_bitrate") {
        video_bitrate = stream.readElementText().toDouble();
      } else if (tag == "audio_enabled") {
        audio_enabled = stream.readElementText().toInt();
      } else if (tag == "audio_codec_index") {
        audio_codec_index = stream.readElementText().toInt();
      } else if (tag == "sampling_rate") {
        sampling_rate = stream.readElementText().toInt();
      } else if (tag == "audio_bitrate") {
        audio_bitrate = stream.readElementText().toInt();
      }
    }
  }

  file.close();

  if (stream.hasError()) {
    QMessageBox::critical(this, tr("Error"), tr("Failed to parse preset file:\n%1").arg(stream.errorString()));
    return;
  }

  // Apply values in dependency order:
  // 1. Format (triggers format_changed -> repopulates codec combos)
  if (format_index >= 0 && format_index < formatCombobox->count()) {
    formatCombobox->setCurrentIndex(format_index);
  }

  // 2. Range
  if (range_index >= 0 && range_index < rangeCombobox->count()) {
    rangeCombobox->setCurrentIndex(range_index);
  }

  // 3. Video codec (triggers vcodec_changed -> repopulates compression type)
  if (video_codec_index >= 0 && video_codec_index < vcodecCombobox->count()) {
    vcodecCombobox->setCurrentIndex(video_codec_index);
  }

  // 4. Video settings (after codec is set so compression type combo is populated)
  if (width >= 0) widthSpinbox->setValue(width);
  if (height >= 0) heightSpinbox->setValue(height);
  if (frame_rate >= 0) framerateSpinbox->setValue(frame_rate);
  if (compression_type_index >= 0 && compression_type_index < compressionTypeCombobox->count()) {
    compressionTypeCombobox->setCurrentIndex(compression_type_index);
  }
  if (video_bitrate >= 0) videobitrateSpinbox->setValue(video_bitrate);

  // 5. Video/audio group enable (after codecs are set)
  if (video_enabled >= 0) {
    bool has_video = vcodecCombobox->count() > 0;
    videoGroupbox->setChecked(video_enabled != 0 && has_video);
    videoGroupbox->setEnabled(has_video);
  }

  // 6. Audio codec
  if (audio_codec_index >= 0 && audio_codec_index < acodecCombobox->count()) {
    acodecCombobox->setCurrentIndex(audio_codec_index);
  }

  // 7. Audio settings
  if (sampling_rate >= 0) samplingRateSpinbox->setValue(sampling_rate);
  if (audio_bitrate >= 0) audiobitrateSpinbox->setValue(audio_bitrate);

  if (audio_enabled >= 0) {
    bool has_audio = acodecCombobox->count() > 0;
    audioGroupbox->setChecked(audio_enabled != 0 && has_audio);
    audioGroupbox->setEnabled(has_audio);
  }
}
