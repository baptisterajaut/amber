#include "rendering/batchexport.h"

#include <QDir>
#include <QRegularExpression>

#include "engine/sequence.h"
#include "global/projectio.h"

QString amber::batch_export_filename(const QString& sequence_name, const QString& ext, const QStringList& taken) {
  QString base;
  base.reserve(sequence_name.size());
  for (const QChar c : sequence_name) {
    // What Windows refuses in a file name; '/' is the only one on Linux and macOS
    // CBA: reserved device names (CON, NUL...) aren't renamed; a sequence called that fails on Windows
    const bool invalid = c.unicode() < 0x20 || QStringLiteral("<>:\"/\\|?*").contains(c);
    base += invalid ? QChar('_') : c;
  }
  base = base.trimmed();
  while (base.endsWith('.')) base.chop(1);  // Windows drops trailing dots
  base = base.trimmed();
  if (base.isEmpty()) base = QStringLiteral("Sequence");

  QString name = base + "." + ext;
  for (int n = 2; taken.contains(name, Qt::CaseInsensitive); n++) {
    name = QStringLiteral("%1 (%2).%3").arg(base).arg(n).arg(ext);
  }
  return name;
}

QStringList amber::batch_export_taken_names(const QStringList& entries, bool image_sequence) {
  if (!image_sequence) return entries;
  // Exactly the pattern's digits, the last ones before the extension: "Sequence 0100001.png" is frame 1 of
  // "Sequence 01", not of "Sequence "
  // CBA: frame numbers past the pattern's width get longer; the sequence's first frames still give its name
  static const QRegularExpression frame_number(QStringLiteral("\\d{%1}(\\.[^.]*)$").arg(amber::kImageSequenceDigits));
  QStringList taken = entries;
  for (const QString& entry : entries) {
    QString name = entry;
    name.replace(frame_number, QStringLiteral("\\1"));
    if (name != entry) taken.append(name);
  }
  return taken;
}

QString amber::batch_export_path(const QString& folder, const QString& name, const QString& ext, bool image_sequence) {
  const QString path = QDir(folder).filePath(name);
  if (!image_sequence) return path;
  // FFmpeg reads the whole path as a pattern
  QString stem = path.left(path.size() - ext.size() - 1);
  stem.replace('%', QStringLiteral("%%"));
  return stem + "%0" + QString::number(amber::kImageSequenceDigits) + "d." + ext;
}

ExportParams amber::batch_export_params(const ExportParams& base, Sequence* seq, bool use_inout,
                                        const QString& filename) {
  ExportParams params = base;
  params.filename = filename;
  params.video_width = seq->width;
  params.video_height = seq->height;
  params.start_frame = 0;
  params.end_frame = seq->getEndFrame();
  // As ExportDialog::build_export_params() for "In to Out"; a sequence without an in/out range exports whole
  if (use_inout && seq->using_workarea) {
    params.start_frame = qMax(seq->workarea_in, params.start_frame);
    params.end_frame = qMin(seq->workarea_out, params.end_frame);
  }
  return params;
}

BatchExport::BatchExport(const QVector<Job>& jobs, QObject* parent)
    : QObject(parent), jobs_(jobs), outcomes_(jobs.size()) {
  for (int i = 0; i < jobs_.size(); i++) outcomes_[i].name = jobs_.at(i).seq->name;
}

void BatchExport::Start() {
  user_sequence_ = amber::ActiveSequence;
  index_ = 0;
  StartNext();
}

int BatchExport::ExportedCount() const {
  int n = 0;
  for (const Outcome& o : outcomes_) {
    if (o.status == Outcome::Exported) n++;
  }
  return n;
}

QString BatchExport::Summary(const QString& folder) const {
  const int exported = ExportedCount();
  if (exported == outcomes_.size()) return tr("%1 sequences exported to %2").arg(exported).arg(folder);

  QStringList lines;
  lines.append(tr("%1 of %2 exported to %3.").arg(exported).arg(outcomes_.size()).arg(folder));
  QStringList cancelled;
  for (const Outcome& o : outcomes_) {
    switch (o.status) {
      case Outcome::Failed:
        lines.append(tr("%1 failed: %2").arg(o.name, o.reason));
        break;
      case Outcome::Skipped:
        lines.append(tr("%1 skipped: %2").arg(o.name, o.reason));
        break;
      case Outcome::Cancelled:
        cancelled.append(o.name);
        break;
      default:
        break;
    }
  }
  if (!cancelled.isEmpty()) lines.append(tr("Cancelled: %1").arg(cancelled.join(", ")));
  return lines.join('\n');
}

void BatchExport::Cancel() {
  cancelled_ = true;
  // A thread that has finished, its ThreadFinished() still queued, exported its sequence: only a running one stops
  if (thread_ != nullptr && thread_->isRunning()) thread_->Interrupt();
}

QString BatchExport::SkipReason(Job& job) const {
  // StartExport() refuses both for a single export
  if (job.params.end_frame <= job.params.start_frame) {
    return tr("nothing to export, the sequence or its in/out range is empty");
  }
  if (job.params.video_enabled && (job.params.video_width % 2 != 0 || job.params.video_height % 2 != 0)) {
    return tr("its width and height must both be even numbers");
  }
  return QString();
}

void BatchExport::StartNext() {
  for (; index_ < jobs_.size(); index_++) {
    Job& job = jobs_[index_];
    Outcome& outcome = outcomes_[index_];
    if (cancelled_) {
      outcome.status = Outcome::Cancelled;
      continue;
    }
    outcome.reason = SkipReason(job);
    if (!outcome.reason.isEmpty()) {
      outcome.status = Outcome::Skipped;
      continue;
    }

    saved_playhead_ = job.seq->playhead;
    // Rendering mode before the switch, which would otherwise have the viewer render a frame of the sequence (and
    // open its clips) just before prepare_export() closes them. The switch resets the audio buffer, prepare_export()
    // then aligns it with the export start.
    amber::project_io->setRenderingState(true);
    if (amber::ActiveSequence != job.seq) amber::project_io->setSequence(job.seq);

    thread_ = new ExportThread(job.seq.get(), job.params, job.vparams, this);
    connect(thread_, &QThread::finished, this, &BatchExport::ThreadFinished);
    connect(thread_, &ExportThread::ProgressChanged, this, &BatchExport::ProgressChanged);
    connect(thread_, &ExportThread::PassChanged, this, &BatchExport::PassChanged);
    gl_surface_ = prepare_export(thread_, job.seq.get(), job.params);
    emit SequenceStarted(index_, jobs_.size(), job.seq->name);
    thread_->start();
    return;
  }

  // Whatever the outcome: the user's sequence back in the timeline and viewer
  if (amber::ActiveSequence != user_sequence_) amber::project_io->setSequence(user_sequence_);
  user_sequence_.reset();
  emit Finished();
}

void BatchExport::ThreadFinished() {
  ExportThread* thread = thread_;
  thread_ = nullptr;
  thread->wait();  // finished() is emitted just before run() returns

  // Only Cancel() interrupts; a failure always leaves an error (ExportThread::run())
  Outcome& outcome = outcomes_[index_];
  if (thread->WasInterrupted()) {
    outcome.status = Outcome::Cancelled;
    cancelled_ = true;
  } else if (!thread->GetError().isEmpty()) {
    outcome.status = Outcome::Failed;
    outcome.reason = thread->GetError();
  } else {
    outcome.status = Outcome::Exported;
  }
  thread->deleteLater();

  // As ExportDialog::export_thread_finished(), on this thread
  finish_export(gl_surface_);
  gl_surface_ = nullptr;
  jobs_[index_].seq->playhead = saved_playhead_;

  index_++;
  StartNext();
}
