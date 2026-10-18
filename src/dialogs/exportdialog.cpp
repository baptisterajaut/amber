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
#include <QTimer>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtMath>

#include "dialogs/advancedvideodialog.h"
#include "dialogs/exportsavepresetdialog.h"
#include "global/config.h"
#include "global/global.h"
#include "panels/panels.h"
#include "rendering/exportthread.h"
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

// Rows of the video grid: encoder options from kVideoOptionRow down, the Advanced button below them all. Audio
// options from kAudioOptionRow down.
constexpr int kVideoOptionRow = 8;
constexpr int kLastRow = 50;
constexpr int kAudioOptionRow = 4;

ExportDialog::ExportDialog(QWidget* parent, const QVector<SequencePtr>& batch)
    : QDialog(parent), batch_(batch), ref_seq_(batch.isEmpty() ? amber::ActiveSequence.get() : batch.first().get()) {
  if (batch_.isEmpty()) {
    setWindowTitle(tr("Export \"%1\"").arg(amber::ActiveSequence->name));
  } else {
    setWindowTitle(tr("Export %1 Sequences").arg(batch_.size()));
  }
  setup_ui();

  rangeCombobox->setCurrentIndex(0);
  if (batch_.isEmpty()) {
    if (amber::ActiveSequence->using_workarea) {
      rangeCombobox->setEnabled(true);
      rangeCombobox->setCurrentIndex(1);
    }
  } else {
    // "In to Out" per sequence: offered when any has an in/out range
    bool any_inout = false;
    QStringList names;
    for (const SequencePtr& s : batch_) {
      any_inout = any_inout || s->using_workarea;
      names.append(s->name);
    }
    rangeCombobox->setEnabled(any_inout);
    rangeCombobox->setCurrentIndex(any_inout ? 1 : 0);
    rangeCombobox->setToolTip(
        tr("In to Out exports each sequence's in/out range; a sequence without one exports whole."));

    QLabel* list_label = new QLabel(names.join(", "), this);
    list_label->setWordWrap(true);
    static_cast<QVBoxLayout*>(layout())->insertWidget(0, list_label);

    // Each sequence's own size and frame rate (batch_export_params(), ExportThread::SetupVideo): the fields show so
    const QString size_tip = tr("Each sequence exports at its own size. Change it in Sequence Settings.");
    for (QSpinBox* spin : {widthSpinbox, heightSpinbox}) {
      spin->setSpecialValueText(tr("Each sequence's own size"));
      spin->setEnabled(false);
      spin->setToolTip(size_tip);
    }
    framerateSpinbox->setSpecialValueText(tr("Each sequence's own rate"));
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
  if (batch_.isEmpty()) {
    widthSpinbox->setValue(amber::ActiveSequence->width);
    heightSpinbox->setValue(amber::ActiveSequence->height);
    framerateSpinbox->setValue(amber::ActiveSequence->frame_rate);
  } else {
    // The minimum, which shows the special value text
    widthSpinbox->setValue(widthSpinbox->minimum());
    heightSpinbox->setValue(heightSpinbox->minimum());
    framerateSpinbox->setValue(framerateSpinbox->minimum());
  }
  samplingRateSpinbox->setValue(ref_seq_->audio_frequency);

  // set some advanced defaults
  vcodec_params.threads = 0;
}

// The encoders the dialog offers for codec: the table's installed ones, or else FFmpeg's default encoder of the codec
// (what the export used before the table, e.g. H.264 without libx264). AV1 is only listed with a table encoder
// (format_changed).
static QStringList dialog_encoders(AVCodecID codec) {
  QStringList encoders = amber::available_encoders(codec);
  const AVCodec* fallback = encoders.isEmpty() ? avcodec_find_encoder(codec) : nullptr;
  if (fallback != nullptr) encoders.append(QString::fromUtf8(fallback->name));
  return encoders;
}

void ExportDialog::add_codec_to_combobox(QComboBox* box, enum AVCodecID codec) {
  QString codec_name;

  // A codec with an encoder choice is named for itself, a single encoder by its own name (as FFmpeg's default one was)
  const QStringList encoders = dialog_encoders(codec);
  const AVCodecDescriptor* desc = avcodec_descriptor_get(codec);
  const AVCodec* codec_info = encoders.isEmpty() ? nullptr : amber::find_encoder(encoders.first(), codec);

  if (encoders.size() > 1 && desc != nullptr) {
    codec_name = desc->long_name;
  } else if (codec_info == nullptr) {
    codec_name = tr("Unknown codec name %1").arg(static_cast<int>(codec));
  } else {
    codec_name = codec_info->long_name;
  }

  box->addItem(codec_name, codec);
}

// Codec lists for format_changed — each entry is {format, {vcodeqs...}, {acodecs...}, def_v, def_a}
struct FormatCodecs {
  int format;
  QVector<AVCodecID> vcodecs;
  QVector<AVCodecID> acodecs;
  int default_vcodec;
  int default_acodec;
};

static const QVector<FormatCodecs>& get_format_codec_table() {
  static const QVector<FormatCodecs> kTable = {
      {FORMAT_3GPP,
       {AV_CODEC_ID_MPEG4, AV_CODEC_ID_H264, AV_CODEC_ID_H265},
       {AV_CODEC_ID_AAC},
       1,
       0},
      {FORMAT_AIFF, {}, {AV_CODEC_ID_PCM_S16LE}, 0, 0},
      {FORMAT_APNG, {AV_CODEC_ID_APNG}, {}, 0, 0},
      {FORMAT_AVI,
       {AV_CODEC_ID_H264, AV_CODEC_ID_MPEG4, AV_CODEC_ID_MJPEG, AV_CODEC_ID_MSVIDEO1, AV_CODEC_ID_RAWVIDEO,
        AV_CODEC_ID_HUFFYUV, AV_CODEC_ID_DVVIDEO},
       {AV_CODEC_ID_AAC, AV_CODEC_ID_AC3, AV_CODEC_ID_FLAC, AV_CODEC_ID_MP2, AV_CODEC_ID_MP3, AV_CODEC_ID_PCM_S16LE},
       3,
       5},
      {FORMAT_DNXHD, {AV_CODEC_ID_DNXHD}, {AV_CODEC_ID_PCM_S16LE}, 0, 0},
      {FORMAT_AC3, {}, {AV_CODEC_ID_AC3, AV_CODEC_ID_EAC3}, 0, 0},
      {FORMAT_FLV, {AV_CODEC_ID_FLV1}, {AV_CODEC_ID_MP3}, 0, 0},
      {FORMAT_GIF, {AV_CODEC_ID_GIF}, {}, 0, 0},
      {FORMAT_IMG,
       {AV_CODEC_ID_BMP, AV_CODEC_ID_MJPEG, AV_CODEC_ID_JPEG2000, AV_CODEC_ID_PSD, AV_CODEC_ID_PNG, AV_CODEC_ID_TIFF},
       {},
       4,
       0},
      {FORMAT_MP2, {}, {AV_CODEC_ID_MP2}, 0, 0},
      {FORMAT_MP3, {}, {AV_CODEC_ID_MP3}, 0, 0},
      {FORMAT_MPEG1,
       {AV_CODEC_ID_MPEG1VIDEO},
       {AV_CODEC_ID_AC3, AV_CODEC_ID_MP2, AV_CODEC_ID_MP3, AV_CODEC_ID_PCM_S16LE},
       0,
       1},
      {FORMAT_MPEG2,
       {AV_CODEC_ID_MPEG2VIDEO},
       {AV_CODEC_ID_AC3, AV_CODEC_ID_MP2, AV_CODEC_ID_MP3, AV_CODEC_ID_PCM_S16LE},
       0,
       1},
      // AV1 stays last in its rows: presets store codec indexes, and it's left out when no AV1 encoder is installed
      {FORMAT_MPEG4,
       {AV_CODEC_ID_MPEG4, AV_CODEC_ID_H264, AV_CODEC_ID_H265, AV_CODEC_ID_AV1},
       {AV_CODEC_ID_AAC, AV_CODEC_ID_AC3, AV_CODEC_ID_MP2, AV_CODEC_ID_MP3},
       1,
       0},
      {FORMAT_MPEGTS,
       {AV_CODEC_ID_MPEG2VIDEO},
       {AV_CODEC_ID_AAC, AV_CODEC_ID_AC3, AV_CODEC_ID_MP2, AV_CODEC_ID_MP3},
       0,
       2},
      {FORMAT_MKV,
       {AV_CODEC_ID_MPEG4, AV_CODEC_ID_H264, AV_CODEC_ID_H265, AV_CODEC_ID_AV1},
       {AV_CODEC_ID_AAC, AV_CODEC_ID_AC3, AV_CODEC_ID_EAC3, AV_CODEC_ID_FLAC, AV_CODEC_ID_MP2, AV_CODEC_ID_MP3,
        AV_CODEC_ID_OPUS, AV_CODEC_ID_PCM_S16LE, AV_CODEC_ID_VORBIS, AV_CODEC_ID_WAVPACK, AV_CODEC_ID_WMAV1,
        AV_CODEC_ID_WMAV2},
       1,
       0},
      {FORMAT_OGG, {AV_CODEC_ID_THEORA}, {AV_CODEC_ID_OPUS, AV_CODEC_ID_VORBIS}, 0, 1},
      {FORMAT_MOV,
       {AV_CODEC_ID_QTRLE, AV_CODEC_ID_MPEG4, AV_CODEC_ID_H264, AV_CODEC_ID_H265, AV_CODEC_ID_MJPEG,
        AV_CODEC_ID_PRORES},
       {AV_CODEC_ID_AAC, AV_CODEC_ID_AC3, AV_CODEC_ID_MP2, AV_CODEC_ID_MP3, AV_CODEC_ID_PCM_S16LE},
       2,
       0},
      {FORMAT_WAV, {}, {AV_CODEC_ID_PCM_S16LE}, 0, 0},
      {FORMAT_WEBM, {AV_CODEC_ID_VP8, AV_CODEC_ID_VP9, AV_CODEC_ID_AV1}, {AV_CODEC_ID_OPUS, AV_CODEC_ID_VORBIS}, 1, 0},
      {FORMAT_WMV, {AV_CODEC_ID_WMV1, AV_CODEC_ID_WMV2}, {AV_CODEC_ID_WMAV1, AV_CODEC_ID_WMAV2}, 1, 1},
  };
  return kTable;
}

void ExportDialog::format_changed(int index) {
  vcodecCombobox->clear();
  acodecCombobox->clear();

  int default_vcodec = 0;
  int default_acodec = 0;

  const auto& table = get_format_codec_table();
  bool found = false;
  for (const auto& entry : table) {
    if (entry.format != index) continue;
    found = true;
    for (AVCodecID id : entry.vcodecs) {
      if (id == AV_CODEC_ID_AV1 && amber::available_encoders(id).isEmpty()) continue;
      add_codec_to_combobox(vcodecCombobox, id);
    }
    for (AVCodecID id : entry.acodecs) add_codec_to_combobox(acodecCombobox, id);
    default_vcodec = entry.default_vcodec;
    default_acodec = entry.default_acodec;
    break;
  }
  if (!found) {
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

  update_faststart();
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

  // Restore rendering state, clear the audio buffer and free the GL fallback surface, on the main thread
  // (ExportThread can't do this — leaving rendering mode starts the autorecovery timer, a QTimer owned by the main
  // thread, which triggers "Timers cannot be started from another thread" if called from ExportThread).
  finish_export(export_gl_surface_);
  export_gl_surface_ = nullptr;

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
  export_thread_ = nullptr;

  // If the export succeeded, close the dialog
  if (succeeded) {
    accept();
  }
}

void ExportDialog::reject() {
  if (export_thread_ != nullptr) {
    // Still exporting, or finished with export_thread_finished() pending: never close under the thread. Only a running
    // thread is interrupted, so a completed export isn't reported as cancelled.
    if (export_thread_->isRunning()) export_thread_->Interrupt();
    return;
  }
  if (batch_export_ != nullptr) {
    batch_export_->Cancel();  // batch_export_finished() follows
    return;
  }
  QDialog::reject();
}

void ExportDialog::prep_ui_for_render(bool r) {
  export_button->setEnabled(!r);
  cancel_button->setEnabled(!r);
  faststartCheckbox->setEnabled(!r);
  videoGroupbox->setEnabled(!r);  // the two-pass checkbox with it
  audioGroupbox->setEnabled(!r);
  renderCancel->setEnabled(r);
}

static QString GetImageExtension(int video_codec) {
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
}

static QString GetMpegExtension(int mpeg_gen, bool has_video, bool has_audio) {
  // mpeg_gen: 1 or 2
  if (has_video && !has_audio) return QString("m%1v").arg(mpeg_gen);
  if (!has_video && has_audio) return QString("m%1a").arg(mpeg_gen);
  return QStringLiteral("mpg");
}

static QString GetFormatExtension(int format, int video_codec, bool has_video, bool has_audio) {
  // Simple 1-to-1 format → extension table
  struct Entry {
    int fmt;
    const char* ext;
  };
  static const Entry kSimple[] = {
      {FORMAT_3GPP, "3gp"},   {FORMAT_AIFF, "aiff"}, {FORMAT_APNG, "apng"}, {FORMAT_AVI, "avi"},
      {FORMAT_DNXHD, "mxf"},  {FORMAT_AC3, "ac3"},   {FORMAT_FLV, "flv"},   {FORMAT_GIF, "gif"},
      {FORMAT_MP3, "mp3"},    {FORMAT_MPEGTS, "ts"},  {FORMAT_OGG, "ogg"},   {FORMAT_MOV, "mov"},
      {FORMAT_WAV, "wav"},    {FORMAT_WEBM, "webm"},
  };
  for (const auto& e : kSimple) {
    if (format == e.fmt) return QLatin1String(e.ext);
  }

  switch (format) {
    case FORMAT_IMG:
      return GetImageExtension(video_codec);
    case FORMAT_MPEG1:
      return GetMpegExtension(1, has_video, has_audio);
    case FORMAT_MPEG2:
      return GetMpegExtension(2, has_video, has_audio);
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

ExportParams ExportDialog::build_export_params(const QString& filename) const {
  ExportParams params;
  params.filename = filename;
  params.video_enabled = videoGroupbox->isChecked();
  if (params.video_enabled) {
    params.video_codec = vcodecCombobox->currentData().toInt();
    params.video_width = widthSpinbox->value();
    params.video_height = heightSpinbox->value();
    params.video_compression_type = compressionTypeCombobox->currentData().toInt();
    params.video_bitrate = videobitrateSpinbox->value();
    params.video_encoder = current_video_encoder();
    params.video_options = option_values(video_option_rows_);
    params.two_pass = !twoPassCheckbox->isHidden() && twoPassCheckbox->isChecked();
  }
  params.audio_enabled = audioGroupbox->isChecked();
  if (params.audio_enabled) {
    params.audio_codec = acodecCombobox->currentData().toInt();
    params.audio_sampling_rate = samplingRateSpinbox->value();
    params.audio_bitrate = audiobitrateSpinbox->value();
    params.audio_encoder = aencoder_name_;
    params.audio_options = option_values(audio_option_rows_);
  }
  params.faststart = !faststartCheckbox->isHidden() && faststartCheckbox->isChecked();
  params.start_frame = 0;
  if (!batch_.isEmpty()) return params;  // size and range per sequence: batch_export_params()
  params.end_frame = amber::ActiveSequence->getEndFrame();
  if (rangeCombobox->currentIndex() == 1) {
    params.start_frame = qMax(amber::ActiveSequence->workarea_in, params.start_frame);
    params.end_frame = qMin(amber::ActiveSequence->workarea_out, params.end_frame);
  }
  return params;
}

void ExportDialog::launch_export_thread(const ExportParams& params) {
  export_thread_ = new ExportThread(amber::ActiveSequence.get(), params, vcodec_params, this);

  connect(export_thread_, &QThread::finished, this, &ExportDialog::export_thread_finished);
  connect(export_thread_, &ExportThread::ProgressChanged, this, &ExportDialog::update_progress_bar);
  export_pass_.clear();
  connect(export_thread_, &ExportThread::PassChanged, this, &ExportDialog::pass_changed);
  connect(renderCancel, &QPushButton::clicked, export_thread_, &ExportThread::Interrupt);

  panel_effect_controls->Clear();

  if (panel_sequence_viewer != nullptr) panel_sequence_viewer->pause();
  pre_export_playhead_ = amber::ActiveSequence ? amber::ActiveSequence->playhead : 0;

  // Rendering state, playhead, audio buffer, clips, autorecovery save and GL fallback surface
  export_gl_surface_ = prepare_export(export_thread_, amber::ActiveSequence.get(), params);

  // Show the export start in the UI. Rendering mode keeps the viewer from drawing, and prepare_export() already reset
  // the audio buffer to the start frame, so no scrub audio reset here (it would re-clear the buffer).
  if (panel_sequence_viewer != nullptr && panel_sequence_viewer->seq != nullptr) {
    panel_sequence_viewer->seek(params.start_frame, false);
  }

  prep_ui_for_render(true);
  total_export_time_start = QDateTime::currentMSecsSinceEpoch();
  export_thread_->start();
}

void ExportDialog::start_batch_export(const QString& ext) {
  const QString folder = QFileDialog::getExistingDirectory(this, tr("Export Sequences To"));
  if (folder.isEmpty()) return;
  const bool image_sequence = formatCombobox->currentIndex() == FORMAT_IMG;

  // Names in use: the folder's, then those given to the sequences before. An image sequence's frames are
  // "<name>00001.<ext>"..., taken as "<name>.<ext>".
  QStringList taken;
  static const QRegularExpression frame_number(QStringLiteral("\\d{5,}(\\.[^.]*)$"));
  for (QString entry : QDir(folder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
    if (image_sequence) entry.replace(frame_number, QStringLiteral("\\1"));
    taken.append(entry);
  }

  const ExportParams base = build_export_params(QString());
  const bool use_inout = rangeCombobox->isEnabled() && rangeCombobox->currentIndex() == 1;
  QVector<BatchExport::Job> jobs;
  for (const SequencePtr& s : batch_) {
    const QString name = amber::batch_export_filename(s->name, ext, taken);
    taken.append(name);
    QString filename = QDir(folder).filePath(name);
    if (image_sequence) {
      // As StartExport(), the frame number before the extension; FFmpeg reads the path as a pattern, so a '%' of it
      // is doubled
      QString stem = filename.left(filename.size() - ext.size() - 1);
      filename = stem.replace('%', QStringLiteral("%%")) + "%05d." + ext;
    }
    jobs.append({s, amber::batch_export_params(base, s.get(), use_inout, filename)});
  }

  batch_folder_ = QDir::toNativeSeparators(folder);
  batch_export_ = new BatchExport(jobs, vcodec_params, this);
  connect(batch_export_, &BatchExport::SequenceStarted, this, [this](int index, int count, const QString& name) {
    batch_label_ = tr("Sequence %1 of %2 — %3").arg(QString::number(index + 1), QString::number(count), name);
    export_pass_.clear();
    total_export_time_start = QDateTime::currentMSecsSinceEpoch();
    // The format first: setValue() repaints at once, which would show the last sequence's total at 0 %
    progressBar->setFormat(batch_label_ + ": %p%");
    progressBar->setValue(0);
  });
  connect(batch_export_, &BatchExport::ProgressChanged, this, &ExportDialog::update_progress_bar);
  connect(batch_export_, &BatchExport::PassChanged, this, &ExportDialog::pass_changed);
  connect(batch_export_, &BatchExport::Finished, this, &ExportDialog::batch_export_finished);
  connect(renderCancel, &QPushButton::clicked, batch_export_, &BatchExport::Cancel);

  panel_effect_controls->Clear();
  if (panel_sequence_viewer != nullptr) panel_sequence_viewer->pause();

  prep_ui_for_render(true);
  batch_export_->Start();
}

void ExportDialog::batch_export_finished() {
  BatchExport* batch = batch_export_;
  batch_export_ = nullptr;
  disconnect(renderCancel, &QPushButton::clicked, batch, &BatchExport::Cancel);
  batch_label_.clear();
  export_pass_.clear();

  prep_ui_for_render(false);

  // BatchExport has restored the active sequence and the playheads: the viewer shows that frame again
  if (panel_sequence_viewer != nullptr && amber::ActiveSequence) {
    panel_sequence_viewer->seek(amber::ActiveSequence->playhead);
  }
  update_ui(false);

  const bool cancelled = batch->WasCancelled();
  const int exported = batch->ExportedCount();
  const bool all = exported == batch->Outcomes().size();
  const QString summary = batch->Summary(batch_folder_);
  batch->deleteLater();

  // As a cancelled single export when nothing was written: no message, the dialog stays open
  if (cancelled && exported == 0) return;
  if (all) {
    QMessageBox::information(this, tr("Export Complete"), summary);
  } else {
    QMessageBox::warning(this, cancelled ? tr("Export Cancelled") : tr("Export Incomplete"), summary);
  }
  // As a single export: closes once something was exported, stays open (to change the settings) otherwise
  if (exported > 0 && !cancelled) accept();
}

void ExportDialog::StartExport() {
  // Batch mode checks size and range per sequence (BatchExport skips a sequence that fails them)
  if (batch_.isEmpty() && (widthSpinbox->value() % 2 == 1 || heightSpinbox->value() % 2 == 1)) {
    QMessageBox::critical(this, tr("Invalid dimensions"),
                          tr("Export width and height must both be even numbers/divisible by 2."), QMessageBox::Ok);
    return;
  }

  // end_frame is exclusive: an empty range would write a file with no frames
  const ExportParams range = build_export_params(QString());
  if (batch_.isEmpty() && range.end_frame <= range.start_frame) {
    QMessageBox::critical(this, tr("Nothing to export"), tr("Nothing to export: the sequence or work area is empty."),
                          QMessageBox::Ok);
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
  if (!batch_.isEmpty()) {
    start_batch_export(ext);
    return;
  }
  QString filename = QFileDialog::getSaveFileName(this, tr("Export Media"), "",
                                                  format_strings[formatCombobox->currentIndex()] + " (*." + ext + ")");
  if (filename.isEmpty()) return;

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

  ExportParams params = build_export_params(filename);
  launch_export_thread(params);
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
  // Two-pass: "Pass 1 of 2: 23% (ETA: ...)"; batch: "Sequence 2 of 3 — Interview — Pass 1 of 2: 23% (ETA: ...)"
  const QString prefix = progress_prefix(value == 100);
  if (!prefix.isEmpty()) progressBar->setFormat(prefix + ": " + progressBar->format());

  progressBar->setValue(value);
}

void ExportDialog::pass_changed(int pass, int passes) {
  export_pass_ = tr("Pass %1 of %2").arg(pass).arg(passes);
  progressBar->setFormat(progress_prefix(false) + ": %p%");  // until the pass's first frame gives an ETA
}

QString ExportDialog::progress_prefix(bool done) const {
  QStringList parts;
  if (!batch_label_.isEmpty()) parts.append(batch_label_);
  if (!done && !export_pass_.isEmpty()) parts.append(export_pass_);  // a single export's total shows no pass either
  return parts.join(QStringLiteral(" — "));
}

void ExportDialog::vcodec_changed(int index) {
  // Repopulate the encoders of this codec; vencoder_changed() below does the rest
  vencoderCombobox->blockSignals(true);
  vencoderCombobox->clear();
  if (index >= 0 && vcodecCombobox->count() > 0) {
    const auto codec = static_cast<AVCodecID>(vcodecCombobox->itemData(index).toInt());
    for (const QString& name : dialog_encoders(codec)) {
      amber::EncoderInfo info;
      vencoderCombobox->addItem(amber::find_encoder_info(name, &info) ? info.label : name, name);
    }
  }
  vencoderCombobox->blockSignals(false);
  const bool choice = vencoderCombobox->count() > 1;
  vencoderLabel->setVisible(choice);
  vencoderCombobox->setVisible(choice);

  vencoder_changed(vencoderCombobox->currentIndex());
}

QString ExportDialog::current_video_encoder() const { return vencoderCombobox->currentData().toString(); }

void ExportDialog::vencoder_changed(int) {
  twoPassCheckbox->setChecked(false);  // off by default for every encoder
  compressionTypeCombobox->clear();

  const QString name = current_video_encoder();
  has_vencoder_info_ = !name.isEmpty() && amber::find_encoder_info(name, &vencoder_info_);
  rebuild_option_rows(videoGridLayout, kVideoOptionRow, videoGroupbox, has_vencoder_info_ ? &vencoder_info_ : nullptr,
                      video_option_rows_);
  fit_height();  // covers the encoder row too: vcodec_changed() always ends here

  if (vcodecCombobox->count() == 0) return;
  const auto codec = static_cast<AVCodecID>(vcodecCombobox->currentData().toInt());

  // Quality-based first: older presets saved compression_type_index 0 for it
  if (has_vencoder_info_ && (!vencoder_info_.quality_key.isEmpty() || vencoder_info_.supports_average_bitrate)) {
    compressionTypeCombobox->setEnabled(true);
    if (!vencoder_info_.quality_key.isEmpty()) {
      compressionTypeCombobox->addItem(vencoder_info_.quality_mode_label, COMPRESSION_TYPE_CFR);
    }
    if (vencoder_info_.supports_average_bitrate) {
      compressionTypeCombobox->addItem(tr("Average Bitrate"), COMPRESSION_TYPE_TARGETBR);
    }
  } else {
    compressionTypeCombobox->addItem(tr("Constant Bitrate"), COMPRESSION_TYPE_CBR);
    compressionTypeCombobox->setCurrentIndex(0);
    compressionTypeCombobox->setEnabled(false);
  }

  // set default pix_fmt for this encoder: its first one the export can write
  const AVCodec* codec_info = amber::find_encoder(name, codec);
  if (codec_info == nullptr) {
    QMessageBox::critical(this, tr("Invalid Codec"),
                          tr("Failed to find a suitable encoder for this codec. Export will likely fail."));
  } else {
    vcodec_params.pix_fmt = -1;
    const enum AVPixelFormat* pix_fmts = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    int num_pix_fmts = 0;
    if (avcodec_get_supported_config(nullptr, codec_info, AV_CODEC_CONFIG_PIX_FORMAT, 0, (const void**)&pix_fmts,
                                     &num_pix_fmts) == 0 &&
        pix_fmts) {
      for (int i = 0; i < num_pix_fmts; i++) {
#else
    pix_fmts = codec_info->pix_fmts;
    if (pix_fmts) {
      for (int i = 0; pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
#endif
        if (amber::export_supports_pix_fmt(pix_fmts[i])) {
          vcodec_params.pix_fmt = pix_fmts[i];
          break;
        }
      }
    }
    if (vcodec_params.pix_fmt == -1) {
      QMessageBox::critical(this, tr("Invalid Codec"),
                            tr("Failed to find pixel format for this encoder. Export will likely fail."));
    }
  }
}

void ExportDialog::acodec_changed(int index) {
  aencoder_name_.clear();
  amber::EncoderInfo info;
  bool has_info = false;
  if (index >= 0 && acodecCombobox->count() > 0) {
    const QStringList encoders = dialog_encoders(static_cast<AVCodecID>(acodecCombobox->itemData(index).toInt()));
    if (!encoders.isEmpty()) {
      aencoder_name_ = encoders.first();
      has_info = amber::find_encoder_info(aencoder_name_, &info);
    }
  }
  rebuild_option_rows(audioGridLayout, kAudioOptionRow, audioGroupbox, has_info ? &info : nullptr, audio_option_rows_);
  fit_height();
}

void ExportDialog::rebuild_option_rows(QGridLayout* grid, int first_row, QWidget* parent,
                                       const amber::EncoderInfo* info, QVector<OptionRow>& rows) {
  for (const OptionRow& r : rows) {
    delete r.label;
    delete r.editor;
  }
  rows.clear();
  if (info == nullptr) return;

  int row = first_row;
  for (const amber::EncoderOption& o : info->options) {
    OptionRow r;
    r.option = o;
    // Children of the group box from the start, so an unchecked group disables them too
    r.label = new QLabel(o.label, parent);
    if (o.kind == amber::EncoderOption::Choice) {
      auto* combo = new QComboBox(parent);
      for (int i = 0; i < o.choices.size(); i++) {
        combo->addItem(o.choice_labels.value(i, o.choices.at(i)), o.choices.at(i));
      }
      combo->setCurrentIndex(qMax(0, combo->findData(o.default_value)));
      r.editor = combo;
    } else {
      auto* spin = new QSpinBox(parent);
      // An unset default gets the extra step below min, showing unset_label and passing nothing
      const bool unset = !o.unset_label.isEmpty();
      spin->setRange(unset ? o.min - 1 : o.min, o.max);
      if (unset) spin->setSpecialValueText(o.unset_label);
      spin->setValue(o.default_value.isEmpty() ? spin->minimum() : o.default_value.toInt());
      r.editor = spin;
    }
    r.label->setToolTip(o.tooltip);
    r.editor->setToolTip(o.tooltip);
    grid->addWidget(r.label, row, 0, 1, 1);
    grid->addWidget(r.editor, row, 1, 1, 1);
    row++;
    rows.append(r);
  }
}

amber::EncoderOptionValues ExportDialog::option_values(const QVector<OptionRow>& rows) {
  amber::EncoderOptionValues values;
  for (const OptionRow& r : rows) {
    QString value;
    if (r.option.kind == amber::EncoderOption::Choice) {
      value = static_cast<QComboBox*>(r.editor)->currentData().toString();
    } else {
      const int v = static_cast<QSpinBox*>(r.editor)->value();
      if (v < r.option.min) continue;  // the unset step
      value = QString::number(v);
    }
    // The encoder's defaults aren't passed: an untouched row encodes exactly as before the row existed
    if (value != r.option.default_value) values.append({r.option.key, value});
  }
  return values;
}

void ExportDialog::apply_option_value(const QVector<OptionRow>& rows, const QString& key, const QString& value) {
  for (const OptionRow& r : rows) {
    if (r.option.key != key) continue;
    if (r.option.kind == amber::EncoderOption::Choice) {
      auto* combo = static_cast<QComboBox*>(r.editor);
      const int i = combo->findData(value);
      if (i >= 0) combo->setCurrentIndex(i);
    } else {
      bool ok = false;
      const int v = value.toInt(&ok);
      if (ok && v >= r.option.min && v <= r.option.max) static_cast<QSpinBox*>(r.editor)->setValue(v);
    }
  }
}

void ExportDialog::comp_type_changed(int) {
  videobitrateSpinbox->setToolTip("");
  videobitrateSpinbox->setDecimals(2);
  videobitrateSpinbox->setMinimum(0);
  videobitrateSpinbox->setMaximum(99.99);
  switch (compressionTypeCombobox->currentData().toInt()) {
    case COMPRESSION_TYPE_CBR:
    case COMPRESSION_TYPE_TARGETBR:
      videoBitrateLabel->setText(tr("Bitrate (Mbps):"));
      videobitrateSpinbox->setValue(qMax(0.5, (double)qRound((0.01528 * ref_seq_->height) - 4.5)));
      break;
    case COMPRESSION_TYPE_CFR:
      // Whole steps: the encoder's quality option gets an integer (ExportThread::SetupVideo)
      videobitrateSpinbox->setDecimals(0);
      if (has_vencoder_info_ && !vencoder_info_.quality_key.isEmpty()) {
        const amber::EncoderInfo& e = vencoder_info_;
        videoBitrateLabel->setText(e.quality_label);
        videobitrateSpinbox->setRange(e.quality_min, e.quality_max);
        videobitrateSpinbox->setValue(e.quality_default);
        videobitrateSpinbox->setToolTip(!e.quality_tooltip.isEmpty()
                                            ? e.quality_tooltip
                                            : tr("%1 = best quality, largest file\n%2 = smallest file, lowest quality")
                                                  .arg(e.quality_min)
                                                  .arg(e.quality_max));
      } else {
        videoBitrateLabel->setText(tr("Quality (CRF):"));
        videobitrateSpinbox->setValue(36);
        videobitrateSpinbox->setMaximum(51);
        videobitrateSpinbox->setToolTip(
            tr("Quality Factor:\n\n0 = lossless\n17-18 = visually lossless (compressed, but unnoticeable)\n23 = high "
               "quality\n51 = lowest quality possible"));
      }
      break;
    case COMPRESSION_TYPE_TARGETSIZE:
      videoBitrateLabel->setText(tr("Target File Size (MB):"));
      videobitrateSpinbox->setValue(100);
      break;
  }
  update_two_pass();
}

void ExportDialog::update_two_pass() {
  twoPassCheckbox->setVisible(has_vencoder_info_ && vencoder_info_.supports_two_pass &&
                              compressionTypeCombobox->currentData().toInt() == COMPRESSION_TYPE_TARGETBR);
  fit_height();
}

void ExportDialog::open_advanced_video_dialog() {
  AdvancedVideoDialog avd(this, static_cast<AVCodecID>(vcodecCombobox->currentData().toInt()), current_video_encoder(),
                          vcodec_params);
  avd.exec();
}

void ExportDialog::update_faststart() {
  const QString ext =
      GetFormatExtension(formatCombobox->currentIndex(), vcodecCombobox->currentData().toInt(), true, true);
  faststartCheckbox->setVisible(!ext.isEmpty() && amber::filename_supports_faststart("x." + ext));
  fit_height();
}

void ExportDialog::fit_height() {
  QTimer::singleShot(0, this, [this]() {
    layout()->activate();
    resize(width(), sizeHint().height());
  });
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

  faststartCheckbox = new QCheckBox(tr("Fast start (web playback)"), this);
  faststartCheckbox->setToolTip(
      tr("Moves the index to the start of the file so it plays while downloading. Takes a moment longer at the end of "
         "the export."));
  faststartCheckbox->setChecked(true);
  verticalLayout->addWidget(faststartCheckbox);

  videoGroupbox = new QGroupBox(this);
  videoGroupbox->setTitle(tr("Video"));
  videoGroupbox->setFlat(false);
  videoGroupbox->setCheckable(true);

  videoGridLayout = new QGridLayout(videoGroupbox);

  videoGridLayout->addWidget(new QLabel(tr("Codec:"), this), 0, 0, 1, 1);
  vcodecCombobox = new QComboBox(videoGroupbox);
  videoGridLayout->addWidget(vcodecCombobox, 0, 1, 1, 1);

  // Hidden unless the codec has a choice of encoders (vcodec_changed)
  vencoderLabel = new QLabel(tr("Encoder:"), videoGroupbox);
  videoGridLayout->addWidget(vencoderLabel, 1, 0, 1, 1);
  vencoderCombobox = new QComboBox(videoGroupbox);
  vencoderCombobox->setToolTip(tr("The program that encodes this codec. Faster encoders make larger files."));
  videoGridLayout->addWidget(vencoderCombobox, 1, 1, 1, 1);
  vencoderLabel->setVisible(false);
  vencoderCombobox->setVisible(false);

  videoGridLayout->addWidget(new QLabel(tr("Width:"), this), 2, 0, 1, 1);
  widthSpinbox = new QSpinBox(videoGroupbox);
  widthSpinbox->setMinimum(2);
  widthSpinbox->setMaximum(16777216);
  videoGridLayout->addWidget(widthSpinbox, 2, 1, 1, 1);

  videoGridLayout->addWidget(new QLabel(tr("Height:"), this), 3, 0, 1, 1);
  heightSpinbox = new QSpinBox(videoGroupbox);
  heightSpinbox->setMinimum(2);
  heightSpinbox->setMaximum(16777216);
  videoGridLayout->addWidget(heightSpinbox, 3, 1, 1, 1);

  videoGridLayout->addWidget(new QLabel(tr("Frame Rate:"), this), 4, 0, 1, 1);
  framerateSpinbox = new QDoubleSpinBox(videoGroupbox);
  // Display only: the export always uses the sequence frame rate (ExportThread::SetupVideo)
  framerateSpinbox->setEnabled(false);
  framerateSpinbox->setToolTip(tr("Exports always use the sequence frame rate. Change it in Sequence Settings."));
  framerateSpinbox->setDecimals(3);  // 23.976 and 59.94 shown as they are
  framerateSpinbox->setMinimum(1);
  // CBA: display ceiling only, no sequence (even from high-speed footage) comes near it
  framerateSpinbox->setMaximum(1000);
  framerateSpinbox->setValue(0);
  videoGridLayout->addWidget(framerateSpinbox, 4, 1, 1, 1);

  videoGridLayout->addWidget(new QLabel(tr("Compression Type:"), this), 5, 0, 1, 1);
  compressionTypeCombobox = new QComboBox(videoGroupbox);
  videoGridLayout->addWidget(compressionTypeCombobox, 5, 1, 1, 1);

  // Hidden unless Average bitrate with an encoder that has two-pass (update_two_pass)
  twoPassCheckbox = new QCheckBox(tr("Two-pass encoding"), videoGroupbox);
  twoPassCheckbox->setToolTip(
      tr("Analyses the whole sequence first, then encodes, so the bitrate goes where the picture needs it. The export "
         "takes about twice as long."));
  twoPassCheckbox->setVisible(false);
  videoGridLayout->addWidget(twoPassCheckbox, 6, 1, 1, 1);

  videoBitrateLabel = new QLabel(videoGroupbox);
  videoGridLayout->addWidget(videoBitrateLabel, 7, 0, 1, 1);
  videobitrateSpinbox = new QDoubleSpinBox(videoGroupbox);
  videobitrateSpinbox->setMaximum(100);
  videobitrateSpinbox->setValue(2);
  videoGridLayout->addWidget(videobitrateSpinbox, 7, 1, 1, 1);

  // Encoder option rows go from kVideoOptionRow down (rebuild_option_rows); empty grid rows take no space
  QPushButton* advanced_video_button = new QPushButton(tr("Advanced"));
  connect(advanced_video_button, &QPushButton::clicked, this, &ExportDialog::open_advanced_video_dialog);
  videoGridLayout->addWidget(advanced_video_button, kLastRow, 1);

  verticalLayout->addWidget(videoGroupbox);

  audioGroupbox = new QGroupBox(this);
  audioGroupbox->setTitle(tr("Audio"));
  audioGroupbox->setCheckable(true);

  audioGridLayout = new QGridLayout(audioGroupbox);

  audioGridLayout->addWidget(new QLabel(tr("Codec:"), this), 0, 0, 1, 1);
  acodecCombobox = new QComboBox(audioGroupbox);
  audioGridLayout->addWidget(acodecCombobox, 0, 1, 1, 1);

  audioGridLayout->addWidget(new QLabel(tr("Sampling Rate:"), this), 1, 0, 1, 1);
  samplingRateSpinbox = new QSpinBox(audioGroupbox);
  samplingRateSpinbox->setMinimum(1);
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
  connect(vencoderCombobox, qOverload<int>(&QComboBox::currentIndexChanged), this, &ExportDialog::vencoder_changed);
  connect(acodecCombobox, qOverload<int>(&QComboBox::currentIndexChanged), this, &ExportDialog::acodec_changed);
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
    // "Default" selected - reset to built-in defaults. Already on MP4, the index doesn't change: refill the codecs
    // anyway so the encoder, its option rows and the compression type are reset too
    if (formatCombobox->currentIndex() == FORMAT_MPEG4) {
      format_changed(FORMAT_MPEG4);
    } else {
      formatCombobox->setCurrentIndex(FORMAT_MPEG4);
    }
    if (batch_.isEmpty()) {  // batch mode: each sequence's own size and rate, nothing to reset
      widthSpinbox->setValue(amber::ActiveSequence->width);
      heightSpinbox->setValue(amber::ActiveSequence->height);
      framerateSpinbox->setValue(amber::ActiveSequence->frame_rate);
    }
    samplingRateSpinbox->setValue(ref_seq_->audio_frequency);
    audiobitrateSpinbox->setValue(256);
    faststartCheckbox->setChecked(true);
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

  // Video settings
  stream.writeTextElement("video_enabled", QString::number(videoGroupbox->isChecked() ? 1 : 0));
  stream.writeTextElement("video_codec_index", QString::number(vcodecCombobox->currentIndex()));
  // Not in batch mode, where the fields show no size: loading the preset then keeps the sequence's
  if (batch_.isEmpty()) {
    stream.writeTextElement("width", QString::number(widthSpinbox->value()));
    stream.writeTextElement("height", QString::number(heightSpinbox->value()));
  }
  stream.writeTextElement("compression_type_index", QString::number(compressionTypeCombobox->currentIndex()));
  // The COMPRESSION_TYPE_* value: the index alone changed meaning when VP9 got a quality mode (2.0; 1.x ignores it)
  stream.writeTextElement("compression_type", QString::number(compressionTypeCombobox->currentData().toInt()));
  bool two_pass = !twoPassCheckbox->isHidden() && twoPassCheckbox->isChecked();  // hidden: does not apply
  stream.writeTextElement("two_pass", QString::number(two_pass ? 1 : 0));
  stream.writeTextElement("video_bitrate", QString::number(videobitrateSpinbox->value(), 'f', 6));
  // Encoder and its non-default options (2.0; 1.x ignores these tags)
  if (!current_video_encoder().isEmpty()) stream.writeTextElement("video_encoder", current_video_encoder());
  for (const auto& o : option_values(video_option_rows_)) {
    stream.writeStartElement("video_option");
    stream.writeAttribute("key", o.first);
    stream.writeCharacters(o.second);
    stream.writeEndElement();
  }

  // Audio settings
  stream.writeTextElement("audio_enabled", QString::number(audioGroupbox->isChecked() ? 1 : 0));
  stream.writeTextElement("audio_codec_index", QString::number(acodecCombobox->currentIndex()));
  stream.writeTextElement("sampling_rate", QString::number(samplingRateSpinbox->value()));
  stream.writeTextElement("audio_bitrate", QString::number(audiobitrateSpinbox->value()));
  if (!aencoder_name_.isEmpty()) stream.writeTextElement("audio_encoder", aencoder_name_);
  for (const auto& o : option_values(audio_option_rows_)) {
    stream.writeStartElement("audio_option");
    stream.writeAttribute("key", o.first);
    stream.writeCharacters(o.second);
    stream.writeEndElement();
  }

  stream.writeTextElement("faststart", QString::number(faststartCheckbox->isChecked() ? 1 : 0));

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
  int video_enabled = -1;
  int video_codec_index = -1;
  int width = -1;
  int height = -1;
  int compression_type_index = -1;
  int compression_type = -1;
  int two_pass = -1;
  double video_bitrate = -1;
  int audio_enabled = -1;
  int audio_codec_index = -1;
  int sampling_rate = -1;
  int audio_bitrate = -1;
  QString video_encoder;
  QString audio_encoder;
  amber::EncoderOptionValues video_options;
  amber::EncoderOptionValues audio_options;
  int faststart = -1;

  while (!stream.atEnd()) {
    stream.readNext();
    if (stream.isStartElement()) {
      QString tag = stream.name().toString();
      if (tag == "format") {
        format_index = stream.readElementText().toInt();
      } else if (tag == "video_enabled") {
        video_enabled = stream.readElementText().toInt();
      } else if (tag == "video_codec_index") {
        video_codec_index = stream.readElementText().toInt();
      } else if (tag == "width") {
        width = stream.readElementText().toInt();
      } else if (tag == "height") {
        height = stream.readElementText().toInt();
      } else if (tag == "compression_type_index") {
        compression_type_index = stream.readElementText().toInt();
      } else if (tag == "compression_type") {
        compression_type = stream.readElementText().toInt();
      } else if (tag == "two_pass") {
        two_pass = stream.readElementText().toInt();
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
      } else if (tag == "video_encoder") {
        video_encoder = stream.readElementText();
      } else if (tag == "audio_encoder") {
        audio_encoder = stream.readElementText();
      } else if (tag == "video_option" || tag == "audio_option") {
        const QString key = stream.attributes().value("key").toString();
        const QString value = stream.readElementText();
        (tag == "video_option" ? video_options : audio_options).append({key, value});
      } else if (tag == "faststart") {
        faststart = stream.readElementText().toInt();
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

  // 2. Video codec (triggers vcodec_changed -> repopulates encoders, compression type and option rows)
  if (video_codec_index >= 0 && video_codec_index < vcodecCombobox->count()) {
    vcodecCombobox->setCurrentIndex(video_codec_index);
  }
  // The preset's encoder, when installed; rebuilt even when already selected so its option rows start at their
  // defaults. Options of an encoder that isn't installed (or of an older preset, which has none) are ignored.
  const int encoder_index = video_encoder.isEmpty() ? -1 : vencoderCombobox->findData(video_encoder);
  if (encoder_index >= 0) vencoderCombobox->setCurrentIndex(encoder_index);
  vencoder_changed(vencoderCombobox->currentIndex());

  // 3. Video settings (after codec is set so compression type combo is populated)
  // Batch mode keeps "Each sequence's own size"
  if (width >= 0 && batch_.isEmpty()) widthSpinbox->setValue(width);
  if (height >= 0 && batch_.isEmpty()) heightSpinbox->setValue(height);
  // An older preset's frame_rate is skipped: the export always uses the sequence frame rate
  if (compression_type >= 0) {
    // By value; a mode this encoder doesn't have leaves the default
    const int i = compressionTypeCombobox->findData(compression_type);
    if (i >= 0) compressionTypeCombobox->setCurrentIndex(i);
  } else if (vcodecCombobox->currentData().toInt() == AV_CODEC_ID_VP9 && current_video_encoder() == "libvpx-vp9") {
    // A VP9 preset from before VP9's quality mode: its index 0 was Constant Bitrate, and its bitrate in Mbps would
    // read as a CRF in the new first entry
    const int i = compressionTypeCombobox->findData(COMPRESSION_TYPE_TARGETBR);
    if (i >= 0) compressionTypeCombobox->setCurrentIndex(i);
  } else if (compression_type_index >= 0 && compression_type_index < compressionTypeCombobox->count()) {
    compressionTypeCombobox->setCurrentIndex(compression_type_index);
  }
  twoPassCheckbox->setChecked(two_pass > 0);  // older presets: off
  if (video_bitrate >= 0) videobitrateSpinbox->setValue(video_bitrate);
  if (encoder_index >= 0) {
    for (const auto& o : video_options) apply_option_value(video_option_rows_, o.first, o.second);
  }

  // 4. Video/audio group enable (after codecs are set)
  if (video_enabled >= 0) {
    bool has_video = vcodecCombobox->count() > 0;
    videoGroupbox->setChecked(video_enabled != 0 && has_video);
    videoGroupbox->setEnabled(has_video);
  }

  // 5. Audio codec
  if (audio_codec_index >= 0 && audio_codec_index < acodecCombobox->count()) {
    acodecCombobox->setCurrentIndex(audio_codec_index);
  }
  acodec_changed(acodecCombobox->currentIndex());  // option rows back to their defaults
  if (!audio_encoder.isEmpty() && audio_encoder == aencoder_name_) {
    for (const auto& o : audio_options) apply_option_value(audio_option_rows_, o.first, o.second);
  }

  // 6. Audio settings
  if (sampling_rate >= 0) samplingRateSpinbox->setValue(sampling_rate);
  if (audio_bitrate >= 0) audiobitrateSpinbox->setValue(audio_bitrate);

  if (audio_enabled >= 0) {
    bool has_audio = acodecCombobox->count() > 0;
    audioGroupbox->setChecked(audio_enabled != 0 && has_audio);
    audioGroupbox->setEnabled(has_audio);
  }

  // On by default, older presets included
  faststartCheckbox->setChecked(faststart != 0);
}
