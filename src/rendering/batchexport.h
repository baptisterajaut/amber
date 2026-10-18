#ifndef BATCHEXPORT_H
#define BATCHEXPORT_H

#include <QObject>
#include <QOffscreenSurface>
#include <QString>
#include <QStringList>
#include <QVector>

#include "engine/sequence_fwd.h"
#include "rendering/exportthread.h"

namespace amber {

// File name of one sequence of a batch export into a folder: "<sequence_name>.<ext>", the characters file systems
// reject (and control characters) replaced by '_', surrounding spaces and trailing dots dropped, "Sequence" for a name
// left empty. A name already in taken (compared case-insensitively, as Windows and macOS file systems do) gets " (2)",
// " (3)"... before the extension.
QString batch_export_filename(const QString& sequence_name, const QString& ext, const QStringList& taken);

// The export dialog's settings (base) for one sequence of a batch: seq's own size, its whole length or, with use_inout
// and an in/out range set on seq, that range, written to filename. The frame rate needs nothing: ExportThread always
// takes the sequence's.
ExportParams batch_export_params(const ExportParams& base, Sequence* seq, bool use_inout, const QString& filename);

}  // namespace amber

// Exports sequences one after the other, each the way a single export runs (prepare_export(), an ExportThread,
// finish_export()), with that sequence made the active one meanwhile: the Timecode effect reads the active sequence's
// playhead. A failed or skipped sequence doesn't stop the batch, Cancel() does. At the end the user's active sequence
// is back, and every exported sequence's playhead where it was. GUI thread only, as prepare_export().
// CBA: same settings for every sequence; per-sequence overrides belong to the render queue
class BatchExport : public QObject {
  Q_OBJECT
 public:
  struct Job {
    SequencePtr seq;
    ExportParams params;
  };

  struct Outcome {
    enum Status { Pending, Exported, Failed, Skipped, Cancelled };
    Status status{Pending};
    QString name;    // the sequence's
    QString reason;  // Failed: the export error; Skipped: why it didn't start
  };

  BatchExport(const QVector<Job>& jobs, const VideoCodecParams& vparams, QObject* parent = nullptr);

  // Starts the first job. Finished() may come before this returns, when no job can start.
  void Start();

  bool IsRunning() const { return thread_ != nullptr; }
  bool WasCancelled() const { return cancelled_; }
  const QVector<Outcome>& Outcomes() const { return outcomes_; }
  int ExportedCount() const;

  // "3 sequences exported to <folder>", or "1 of 3 exported to <folder>." with a line per sequence that wasn't
  QString Summary(const QString& folder) const;

 public slots:
  // Interrupts the running export (ExportThread removes its partial file) and skips the rest
  void Cancel();

 signals:
  // index from 0, as job index starts
  void SequenceStarted(int index, int count, const QString& name);
  // The running ExportThread's
  void ProgressChanged(int value, qint64 remaining_ms);
  void PassChanged(int pass, int passes);
  // Every job done, failed, skipped or cancelled; the active sequence restored
  void Finished();

 private:
  void StartNext();
  void ThreadFinished();
  QString SkipReason(Job& job) const;

  QVector<Job> jobs_;
  QVector<Outcome> outcomes_;
  VideoCodecParams vparams_;
  int index_{0};
  bool cancelled_{false};
  ExportThread* thread_{nullptr};
  QOffscreenSurface* gl_surface_{nullptr};
  SequencePtr user_sequence_;  // the active sequence when Start() ran
  long saved_playhead_{0};     // the running job's sequence's playhead before prepare_export() moved it
};

#endif  // BATCHEXPORT_H
