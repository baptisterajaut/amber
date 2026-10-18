#include "dialogs/renderqueuedialog.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QVBoxLayout>

#include "engine/sequence.h"
#include "panels/panels.h"
#include "rendering/renderqueue.h"
#include "ui/viewerwidget.h"

namespace {
enum Column { kSequence, kOutput, kSettings, kStatus, kColumnCount };

// "1:02:03", as the export dialog shows times
QString hms(qint64 ms) {
  const qint64 s = ms / 1000;
  return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}
}  // namespace

RenderQueueDialog::RenderQueueDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Render Queue"));
  resize(960, 340);

  QVBoxLayout* layout = new QVBoxLayout(this);

  table_ = new QTableWidget(0, kColumnCount, this);
  table_->setHorizontalHeaderLabels({tr("Sequence"), tr("Output"), tr("Settings"), tr("Status")});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setSectionResizeMode(kSettings, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(kStatus, QHeaderView::Stretch);  // reasons in full as tooltips
  table_->setColumnWidth(kSequence, 160);
  table_->setColumnWidth(kOutput, 200);
  layout->addWidget(table_);

  progress_bar_ = new QProgressBar(this);
  progress_bar_->setEnabled(false);
  progress_bar_->setValue(0);
  progress_bar_->setFormat(QString());
  layout->addWidget(progress_bar_);

  QHBoxLayout* buttons = new QHBoxLayout();
  remove_button_ = new QPushButton(tr("Remove"), this);
  remove_button_->setToolTip(tr("Remove the selected rows from the queue (Delete)"));
  buttons->addWidget(remove_button_);
  clear_finished_button_ = new QPushButton(tr("Clear Finished"), this);
  clear_finished_button_->setToolTip(tr("Remove the rows that are done"));
  buttons->addWidget(clear_finished_button_);
  buttons->addStretch();
  render_button_ = new QPushButton(this);
  buttons->addWidget(render_button_);
  close_button_ = new QPushButton(tr("Close"), this);
  buttons->addWidget(close_button_);
  layout->addLayout(buttons);

  connect(remove_button_, &QPushButton::clicked, this, &RenderQueueDialog::remove_selected);
  connect(new QShortcut(QKeySequence::Delete, table_), &QShortcut::activated, this,
          &RenderQueueDialog::remove_selected);
  connect(clear_finished_button_, &QPushButton::clicked, &amber::render_queue(), &RenderQueue::ClearFinished);
  connect(render_button_, &QPushButton::clicked, this, &RenderQueueDialog::render_clicked);
  connect(close_button_, &QPushButton::clicked, this, &QDialog::accept);
  connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &RenderQueueDialog::update_buttons);

  RenderQueue& queue = amber::render_queue();
  connect(&queue, &RenderQueue::Changed, this, &RenderQueueDialog::refresh);
  connect(&queue, &RenderQueue::JobStarted, this, &RenderQueueDialog::job_started);
  connect(&queue, &RenderQueue::ProgressChanged, this, &RenderQueueDialog::progress_changed);
  connect(&queue, &RenderQueue::PassChanged, this, &RenderQueueDialog::pass_changed);
  connect(&queue, &RenderQueue::Finished, this, &RenderQueueDialog::render_finished);

  refresh();
}

void RenderQueueDialog::reject() {
  if (amber::render_queue().IsRendering()) {
    amber::render_queue().Cancel();  // render_finished() follows
    return;
  }
  QDialog::reject();
}

QString RenderQueueDialog::status_text(int row) const {
  const RenderQueue& queue = amber::render_queue();
  if (row == queue.RenderingRow()) return tr("Rendering %1 %").arg(percent_);
  const BatchExport::Outcome& outcome = queue.Jobs().at(row).outcome;
  switch (outcome.status) {
    case BatchExport::Outcome::Exported:
      return tr("Done");
    case BatchExport::Outcome::Failed:
      return tr("Failed: %1").arg(outcome.reason);
    case BatchExport::Outcome::Skipped:
      return tr("Skipped: %1").arg(outcome.reason);
    case BatchExport::Outcome::Cancelled:
      return tr("Cancelled");
    case BatchExport::Outcome::Pending:
      break;
  }
  return tr("Queued");
}

void RenderQueueDialog::refresh() {
  const QVector<amber::QueuedJob>& jobs = amber::render_queue().Jobs();
  // setItem() on the rows already there keeps the selection
  table_->setRowCount(jobs.size());
  for (int row = 0; row < jobs.size(); row++) {
    const amber::QueuedJob& job = jobs.at(row);
    const QString path = QDir::toNativeSeparators(job.params.filename);
    const QString status = status_text(row);
    const QStringList texts = {job.seq->name, QFileInfo(job.params.filename).fileName(), job.summary, status};
    const QStringList tips = {job.seq->name, path, job.summary, status};
    for (int column = 0; column < kColumnCount; column++) {
      QTableWidgetItem* item = new QTableWidgetItem(texts.at(column));
      item->setToolTip(tips.at(column));
      table_->setItem(row, column, item);
    }
  }
  update_buttons();
}

void RenderQueueDialog::update_buttons() {
  const RenderQueue& queue = amber::render_queue();
  const bool rendering = queue.IsRendering();
  bool any_done = false;
  for (const amber::QueuedJob& job : queue.Jobs()) {
    any_done = any_done || job.outcome.status == BatchExport::Outcome::Exported;
  }
  remove_button_->setEnabled(!rendering && table_->selectionModel()->hasSelection());
  clear_finished_button_->setEnabled(!rendering && any_done);
  render_button_->setText(rendering ? tr("Cancel") : tr("Render"));
  render_button_->setToolTip(rendering ? tr("Stop the running export; the rows after it stay queued")
                                       : tr("Render every row that isn't done, one after the other"));
  render_button_->setEnabled(rendering || queue.PendingCount() > 0);
  close_button_->setEnabled(!rendering);
}

void RenderQueueDialog::remove_selected() {
  RenderQueue& queue = amber::render_queue();
  if (queue.IsRendering()) return;
  QList<int> rows;
  for (const QModelIndex& index : table_->selectionModel()->selectedRows()) rows.append(index.row());
  std::sort(rows.begin(), rows.end(), std::greater<int>());  // from the last: the indexes before stay valid
  for (int row : rows) queue.Remove(row);
  table_->clearSelection();
}

void RenderQueueDialog::render_clicked() {
  RenderQueue& queue = amber::render_queue();
  if (queue.IsRendering()) {
    queue.Cancel();
    return;
  }
  // As ExportDialog::start_batch_export()
  panel_effect_controls->Clear();
  if (panel_sequence_viewer != nullptr) panel_sequence_viewer->pause();
  progress_bar_->setEnabled(true);
  queue.Render();
}

void RenderQueueDialog::job_started(int row, int number, int count) {
  setWindowTitle(tr("Render Queue — job %1 of %2").arg(number + 1).arg(count));
  percent_ = 0;
  pass_.clear();
  job_start_ms_ = QDateTime::currentMSecsSinceEpoch();
  // The format first: setValue() repaints at once
  progress_bar_->setFormat(amber::render_queue().Jobs().at(row).seq->name + ": %p%");
  progress_bar_->setValue(0);
  table_->scrollToItem(table_->item(row, kStatus));
}

void RenderQueueDialog::progress_changed(int value, qint64 remaining_ms) {
  percent_ = value;
  const int row = amber::render_queue().RenderingRow();
  if (row < 0) return;  // CBA: never seen, the thread's last progress comes before its end; a guard costs nothing
  QString format = amber::render_queue().Jobs().at(row).seq->name;
  if (!pass_.isEmpty() && value < 100) format += QStringLiteral(" — ") + pass_;
  if (value == 100) {
    format += tr(": %p% (Total: %1)").arg(hms(QDateTime::currentMSecsSinceEpoch() - job_start_ms_));
  } else {
    format += tr(": %p% (ETA: %1)").arg(hms(remaining_ms));
  }
  progress_bar_->setFormat(format);
  progress_bar_->setValue(value);
  QTableWidgetItem* status = table_->item(row, kStatus);
  if (status != nullptr) status->setText(status_text(row));
}

void RenderQueueDialog::pass_changed(int pass, int passes) {
  pass_ = tr("Pass %1 of %2").arg(pass).arg(passes);
  const int row = amber::render_queue().RenderingRow();
  if (row < 0) return;
  progress_bar_->setFormat(amber::render_queue().Jobs().at(row).seq->name + QStringLiteral(" — ") + pass_ + ": %p%");
}

void RenderQueueDialog::render_finished() {
  setWindowTitle(tr("Render Queue"));
  progress_bar_->setEnabled(false);
  progress_bar_->setFormat(QString());
  progress_bar_->setValue(0);
  pass_.clear();
  refresh();

  // The queue has restored the active sequence and the playheads: the viewer shows that frame again
  if (panel_sequence_viewer != nullptr && amber::ActiveSequence) {
    panel_sequence_viewer->seek(amber::ActiveSequence->playhead);
  }
  update_ui(false);

  // Rows tell their outcome; a box only for what didn't render (every Failed or Skipped row is from this run: Render()
  // takes all but the Done ones)
  QStringList lines;
  for (const amber::QueuedJob& job : amber::render_queue().Jobs()) {
    const QString file = QFileInfo(job.params.filename).fileName();
    if (job.outcome.status == BatchExport::Outcome::Failed) {
      lines.append(tr("%1 (%2) failed: %3").arg(job.seq->name, file, job.outcome.reason));
    } else if (job.outcome.status == BatchExport::Outcome::Skipped) {
      lines.append(tr("%1 (%2) skipped: %3").arg(job.seq->name, file, job.outcome.reason));
    }
  }
  if (!lines.isEmpty()) QMessageBox::warning(this, tr("Render Queue Incomplete"), lines.join('\n'));
}
