#ifndef RENDERQUEUE_H
#define RENDERQUEUE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "engine/sequence_fwd.h"
#include "rendering/batchexport.h"
#include "rendering/exportthread.h"

namespace amber {

// One export of the render queue: the export dialog's settings when it was queued. The frames are not part of it:
// queue_job_params() takes them from the sequence when the job renders.
struct QueuedJob {
  SequencePtr seq;
  ExportParams params;           // output file, size, codecs, encoder options
  VideoCodecParams vparams;      // the dialog's Advanced settings
  bool use_inout{false};         // the dialog's "In to Out"
  QString summary;               // "MPEG-4 Video, H.264, 1920×1080, AAC", for the Render Queue dialog
  BatchExport::Outcome outcome;  // of its last render; Pending until rendered
};

// The params job renders with: its queued settings and size, the range of its sequence as it is now (whole, or its
// in/out range with use_inout and one set), so edits made after queuing are exported
ExportParams queue_job_params(const QueuedJob& job);

// Whether seq is the sequence of an item of the project (amber::project_model), in any folder. A deleted sequence
// lives on in the undo stack, out of the model.
bool project_has_sequence(const Sequence* seq);

// Whether two output paths are the same file. Case-insensitive on every system, as batch_export_filename() compares
// names: two outputs differing only by case would overwrite each other on Windows and macOS.
// CBA: also case-insensitive on Linux, where they are two files; a per-volume check (QStorageInfo) if anyone minds
bool same_output_path(const QString& a, const QString& b);

}  // namespace amber

// The render queue: exports queued from the export dialog (Queue), each with its own settings, rendered one after
// the other through a BatchExport by the Render Queue dialog. Rows stay until removed; a rendered row is Done and isn't
// rendered again, any other is on the next Render(). Cleared with the project (the project model's reset: new, open,
// close), as the sequences belong to it. GUI thread only.
// CBA: in memory only; saving the queue with the project if asked
// CBA: a sequence deleted from the project while queued stays in memory, its clips as they were (open cachers too),
// until its row is removed or the queue cleared; closing its clips on delete if the memory matters
// CBA: modal render (level 1); exporting a copy of the sequence to keep editing is level 2 (see report)
class RenderQueue : public QObject {
  Q_OBJECT
 public:
  explicit RenderQueue(QObject* parent = nullptr);

  // Appends job, its outcome as given (Pending for a new one). Refused, with the reason, when its output path is
  // the one of a row still to render.
  QString Add(const amber::QueuedJob& job);
  // Not while rendering (false then)
  bool Remove(int index);
  // Drops the Done rows; not while rendering
  void ClearFinished();
  // Not while rendering
  void Clear();

  const QVector<amber::QueuedJob>& Jobs() const { return jobs_; }
  int Count() const { return jobs_.size(); }
  // Rows Render() would render: all but the Done ones
  int PendingCount() const;

  // The names (file names, an image sequence's pattern as the name it was made from) of the rows' outputs in folder,
  // for batch_export_filename() to give a batch queued there other names
  QStringList TakenNames(const QString& folder) const;

  // Renders every row but the Done ones, in order. A row whose sequence left the project is Skipped. Finished() may
  // come before this returns, when nothing can start.
  void Render();
  bool IsRendering() const { return batch_ != nullptr; }
  // The row being rendered, -1 when none
  int RenderingRow() const { return rendering_row_; }

 public slots:
  // Interrupts the running export (its partial file removed): that row is Cancelled, the rows after it stay as they
  // were, to render next time
  void Cancel();

 signals:
  // A row added, removed or its outcome changed
  void Changed();
  // row of Jobs(); number from 0 of count, the rows this Render() renders. Comes before the Changed() of that start.
  void JobStarted(int row, int number, int count);
  // The running export's
  void ProgressChanged(int value, qint64 remaining_ms);
  void PassChanged(int pass, int passes);
  // Every row of the run rendered, failed, skipped or cancelled; the active sequence and playheads restored
  void Finished();

 private:
  // Copies the outcomes of the batch's first count jobs to their rows
  void TakeOutcomes(int count);
  void BatchFinished();

  QVector<amber::QueuedJob> jobs_;
  BatchExport* batch_{nullptr};
  QVector<int> run_rows_;  // the row of each job of batch_
  int started_{-1};        // index in batch_ of the last job started
  int rendering_row_{-1};
};

namespace amber {
// The editor's queue, created on first use
RenderQueue& render_queue();
}  // namespace amber

#endif  // RENDERQUEUE_H
