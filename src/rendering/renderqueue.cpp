#include "rendering/renderqueue.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include "engine/sequence.h"
#include "project/media.h"
#include "project/projectmodel.h"

ExportParams amber::queue_job_params(const QueuedJob& job) {
  // The batch export's range; the size stays the queued one (the dialog's, or in a batch the sequence's at queue time)
  ExportParams params = amber::batch_export_params(job.params, job.seq.get(), job.use_inout, job.params.filename);
  params.video_width = job.params.video_width;
  params.video_height = job.params.video_height;
  return params;
}

static bool folder_has_sequence(Media* folder, const Sequence* seq) {
  for (int i = 0; i < folder->childCount(); i++) {
    Media* item = folder->child(i);
    switch (item->get_type()) {
      case MEDIA_TYPE_SEQUENCE:
        if (item->to_sequence().get() == seq) return true;
        break;
      case MEDIA_TYPE_FOLDER:
        if (folder_has_sequence(item, seq)) return true;
        break;
      default:
        break;
    }
  }
  return false;
}

bool amber::project_has_sequence(const Sequence* seq) {
  return seq != nullptr && folder_has_sequence(amber::project_model.get_root(), seq);
}

bool amber::same_output_path(const QString& a, const QString& b) {
  return QDir::cleanPath(QFileInfo(a).absoluteFilePath())
             .compare(QDir::cleanPath(QFileInfo(b).absoluteFilePath()), Qt::CaseInsensitive) == 0;
}

RenderQueue::RenderQueue(QObject* parent) : QObject(parent) {
  // ProjectModel::clear() is the one reset: AmberGlobal::ClearProject(), which new, open and the load errors go through
  connect(&amber::project_model, &QAbstractItemModel::modelReset, this, &RenderQueue::Clear);
}

QString RenderQueue::Add(const amber::QueuedJob& job) {
  for (const amber::QueuedJob& queued : jobs_) {
    if (queued.outcome.status != BatchExport::Outcome::Exported &&
        amber::same_output_path(queued.params.filename, job.params.filename)) {
      return tr("\"%1\" is already the output of the queued export of %2.")
          .arg(QDir::toNativeSeparators(job.params.filename), queued.seq->name);
    }
  }
  jobs_.append(job);
  emit Changed();
  return QString();
}

bool RenderQueue::Remove(int index) {
  if (IsRendering() || index < 0 || index >= jobs_.size()) return false;
  jobs_.removeAt(index);
  emit Changed();
  return true;
}

void RenderQueue::ClearFinished() {
  if (IsRendering()) return;
  const qsizetype removed =
      jobs_.removeIf([](const amber::QueuedJob& job) { return job.outcome.status == BatchExport::Outcome::Exported; });
  if (removed > 0) emit Changed();
}

void RenderQueue::Clear() {
  // CBA: a project can't close during a render, the editor is blocked (modal); a level-2 render would cancel first
  if (IsRendering() || jobs_.isEmpty()) return;
  jobs_.clear();
  emit Changed();
}

int RenderQueue::PendingCount() const {
  int n = 0;
  for (const amber::QueuedJob& job : jobs_) {
    if (job.outcome.status != BatchExport::Outcome::Exported) n++;
  }
  return n;
}

QStringList RenderQueue::TakenNames(const QString& folder) const {
  // batch_export_path()'s pattern: "%05d" before the extension
  static const QRegularExpression pattern(QStringLiteral("%0") + QString::number(amber::kImageSequenceDigits) +
                                          QStringLiteral("d(\\.[^.]*)$"));
  const QString dir = QDir::cleanPath(QDir(folder).absolutePath());
  QStringList names;
  for (const amber::QueuedJob& job : jobs_) {
    const QFileInfo info(job.params.filename);
    if (QDir::cleanPath(info.absolutePath()).compare(dir, Qt::CaseInsensitive) != 0) continue;
    QString name = info.fileName();
    if (name.contains(pattern)) {
      name.replace(pattern, QStringLiteral("\\1"));
      name.replace(QStringLiteral("%%"), QStringLiteral("%"));
    }
    names.append(name);
  }
  return names;
}

void RenderQueue::Render() {
  if (IsRendering()) return;

  QVector<BatchExport::Job> jobs;
  run_rows_.clear();
  for (int i = 0; i < jobs_.size(); i++) {
    amber::QueuedJob& job = jobs_[i];
    if (job.outcome.status == BatchExport::Outcome::Exported) continue;
    job.outcome.reason.clear();
    job.outcome.name = job.seq->name;
    // Deleted (its item is in the undo stack, an undo brings it back) or its project closed
    if (!amber::project_has_sequence(job.seq.get())) {
      job.outcome.status = BatchExport::Outcome::Skipped;
      job.outcome.reason = tr("the sequence is no longer in the project");
      continue;
    }
    job.outcome.status = BatchExport::Outcome::Pending;
    run_rows_.append(i);
    jobs.append({job.seq, amber::queue_job_params(job), job.vparams});
  }

  started_ = -1;
  batch_ = new BatchExport(jobs, this);
  connect(batch_, &BatchExport::SequenceStarted, this, [this](int index, int count, const QString&) {
    TakeOutcomes(index);  // the jobs before: done, failed or skipped
    started_ = index;
    rendering_row_ = run_rows_.at(index);
    emit Changed();
    emit JobStarted(rendering_row_, index, count);
  });
  connect(batch_, &BatchExport::ProgressChanged, this, &RenderQueue::ProgressChanged);
  connect(batch_, &BatchExport::PassChanged, this, &RenderQueue::PassChanged);
  connect(batch_, &BatchExport::Finished, this, &RenderQueue::BatchFinished);
  emit Changed();
  batch_->Start();
}

void RenderQueue::Cancel() {
  if (batch_ != nullptr) batch_->Cancel();
}

void RenderQueue::TakeOutcomes(int count) {
  for (int k = 0; k < count; k++) {
    BatchExport::Outcome outcome = batch_->Outcomes().at(k);
    // BatchExport marks the jobs a Cancel() kept from starting Cancelled; the queue keeps them for the next Render()
    if (outcome.status == BatchExport::Outcome::Cancelled && k > started_) {
      outcome.status = BatchExport::Outcome::Pending;
    }
    jobs_[run_rows_.at(k)].outcome = outcome;
  }
}

void RenderQueue::BatchFinished() {
  TakeOutcomes(run_rows_.size());
  batch_->deleteLater();
  batch_ = nullptr;
  run_rows_.clear();
  rendering_row_ = -1;
  emit Changed();
  emit Finished();
}

RenderQueue& amber::render_queue() {
  // On first use, after amber::project_model (a global) whose reset it follows
  static RenderQueue queue;
  return queue;
}
