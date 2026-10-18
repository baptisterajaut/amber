// Render queue model (rendering/renderqueue.h): rows, refusals, the range taken at render time, sequences that left
// the project, the reset with the project. Rendering itself is covered by amber-test-headless-render.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixfmt.h>
}

#include <QSignalSpy>
#include <QtTest>

#include "core/headlessappcontext.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "project/media.h"
#include "project/projectmodel.h"
#include "rendering/renderqueue.h"

namespace {

// A 1920x1080 sequence named name with one clip over [0, frames)
SequencePtr sequence(const QString& name, long frames) {
  SequencePtr seq = std::make_shared<Sequence>();
  seq->name = name;
  seq->width = 1920;
  seq->height = 1080;
  seq->frame_rate = 30.0;
  ClipPtr c = std::make_shared<Clip>(seq.get());
  c->set_timeline_in(0);
  c->set_timeline_out(frames);
  c->set_clip_in(0);
  c->set_track(-1);
  seq->clips.append(c);
  return seq;
}

// A job of seq to filename, at 1280x720 (not the sequence's size)
amber::QueuedJob job(const SequencePtr& seq, const QString& filename, bool use_inout = false) {
  amber::QueuedJob j;
  j.seq = seq;
  j.params.filename = filename;
  j.params.video_enabled = true;
  j.params.video_codec = AV_CODEC_ID_H264;
  j.params.video_width = 1280;
  j.params.video_height = 720;
  j.params.video_bitrate = 8.0;
  j.params.video_encoder = "libx264";
  j.params.start_frame = 0;
  j.params.end_frame = seq->getEndFrame();
  j.vparams.pix_fmt = AV_PIX_FMT_YUV420P;
  j.vparams.threads = 3;
  j.use_inout = use_inout;
  j.summary = "MPEG-4 Video, H.264, 1280×720";
  return j;
}

}  // namespace

class TestRenderQueue : public QObject {
  Q_OBJECT

  HeadlessAppContext ctx_;

 private slots:
  void initTestCase() { amber::app_ctx = &ctx_; }
  void cleanupTestCase() {
    amber::project_model.clear();
    amber::app_ctx = nullptr;
  }
  void init() {
    amber::project_model.clear();
    amber::render_queue().Clear();
  }

  void addRemoveClear() {
    RenderQueue& q = amber::render_queue();
    QSignalSpy changed(&q, &RenderQueue::Changed);
    SequencePtr a = sequence("A", 10);
    SequencePtr b = sequence("B", 10);
    QVERIFY(q.Add(job(a, "/out/a.mp4")).isEmpty());
    QVERIFY(q.Add(job(b, "/out/b.mp4")).isEmpty());
    QVERIFY(q.Add(job(a, "/out/a.mov")).isEmpty());
    QCOMPARE(changed.count(), 3);
    QCOMPARE(q.Count(), 3);
    QCOMPARE(q.PendingCount(), 3);
    QCOMPARE(q.Jobs().at(1).seq, b);
    QCOMPARE(q.Jobs().at(1).outcome.status, BatchExport::Outcome::Pending);

    QVERIFY(q.Remove(1));
    QCOMPARE(changed.count(), 4);
    QCOMPARE(q.Count(), 2);
    QCOMPARE(q.Jobs().at(1).params.filename, QString("/out/a.mov"));
    QVERIFY(!q.Remove(2));
    QVERIFY(!q.Remove(-1));
    QCOMPARE(changed.count(), 4);

    // Nothing Done: Clear Finished keeps every row and says nothing
    q.ClearFinished();
    QCOMPARE(q.Count(), 2);
    QCOMPARE(changed.count(), 4);

    q.Clear();
    QCOMPARE(q.Count(), 0);
    QCOMPARE(changed.count(), 5);
    q.Clear();  // already empty
    QCOMPARE(changed.count(), 5);
  }

  // Clear Finished drops the Done rows only; a Done row no longer holds its output path
  void clearFinishedDropsDoneRows() {
    RenderQueue& q = amber::render_queue();
    SequencePtr a = sequence("A", 10);
    amber::QueuedJob done = job(a, "/out/done.mp4");
    done.outcome.status = BatchExport::Outcome::Exported;
    amber::QueuedJob failed = job(a, "/out/failed.mp4");
    failed.outcome.status = BatchExport::Outcome::Failed;
    QVERIFY(q.Add(done).isEmpty());
    QVERIFY(q.Add(failed).isEmpty());
    QVERIFY(q.Add(job(a, "/out/queued.mp4")).isEmpty());
    QCOMPARE(q.PendingCount(), 2);
    QVERIFY(q.Add(job(a, "/out/done.mp4")).isEmpty());  // rendered already: queuing it again is a re-render
    QCOMPARE(q.PendingCount(), 3);

    QSignalSpy changed(&q, &RenderQueue::Changed);
    q.ClearFinished();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(q.Count(), 3);
    QCOMPARE(q.Jobs().at(0).params.filename, QString("/out/failed.mp4"));
    QCOMPARE(q.Jobs().at(2).params.filename, QString("/out/done.mp4"));
  }

  void duplicateOutputRefused() {
    RenderQueue& q = amber::render_queue();
    SequencePtr a = sequence("A", 10);
    SequencePtr b = sequence("B", 10);
    QVERIFY(q.Add(job(a, "/out/film.mp4")).isEmpty());
    QSignalSpy changed(&q, &RenderQueue::Changed);

    const QString same = q.Add(job(b, "/out/film.mp4"));
    QVERIFY(!same.isEmpty());
    QVERIFY2(same.contains("film.mp4") && same.contains("A"), qPrintable(same));
    // The same file however written: case (Windows and macOS file systems), "..", a doubled separator
    QVERIFY(!q.Add(job(b, "/out/FILM.MP4")).isEmpty());
    QVERIFY(!q.Add(job(b, "/out/x/../film.mp4")).isEmpty());
    QVERIFY(!q.Add(job(b, "/out//film.mp4")).isEmpty());
    QCOMPARE(changed.count(), 0);
    QCOMPARE(q.Count(), 1);
    QVERIFY(q.Add(job(b, "/out/film2.mp4")).isEmpty());
  }

  // The range comes from the sequence when the job renders; size, codecs and encoder settings stay the queued ones
  void jobParamsTakeTheRangeAtRenderTime() {
    SequencePtr seq = sequence("A", 10);
    seq->using_workarea = true;
    seq->workarea_in = 2;
    seq->workarea_out = 8;
    amber::QueuedJob whole = job(seq, "/out/whole.mp4");
    amber::QueuedJob inout = job(seq, "/out/inout.mp4", true);

    ExportParams p = amber::queue_job_params(whole);
    QCOMPARE(p.start_frame, 0L);
    QCOMPARE(p.end_frame, 10L);
    p = amber::queue_job_params(inout);
    QCOMPARE(p.start_frame, 2L);
    QCOMPARE(p.end_frame, 8L);

    // Edits after queuing: a longer clip, another in/out range
    seq->clips.first()->set_timeline_out(25);
    seq->workarea_in = 5;
    seq->workarea_out = 20;
    p = amber::queue_job_params(whole);
    QCOMPARE(p.start_frame, 0L);
    QCOMPARE(p.end_frame, 25L);
    p = amber::queue_job_params(inout);
    QCOMPARE(p.start_frame, 5L);
    QCOMPARE(p.end_frame, 20L);
    QCOMPARE(p.video_width, 1280);  // not the sequence's 1920x1080
    QCOMPARE(p.video_height, 720);
    QCOMPARE(p.video_codec, int(AV_CODEC_ID_H264));
    QCOMPARE(p.video_encoder, QString("libx264"));
    QCOMPARE(p.video_bitrate, 8.0);
    QCOMPARE(p.filename, QString("/out/inout.mp4"));

    // In/out range cleared: the whole sequence
    seq->using_workarea = false;
    p = amber::queue_job_params(inout);
    QCOMPARE(p.start_frame, 0L);
    QCOMPARE(p.end_frame, 25L);
  }

  void projectHasSequence() {
    SequencePtr top = sequence("Top", 10);
    SequencePtr nested = sequence("In a folder", 10);
    SequencePtr outside = sequence("Outside", 10);
    amber::create_sequence_item(nullptr, top, false, nullptr);
    MediaPtr folder = amber::create_folder_item("Folder");
    amber::project_model.appendChild(nullptr, folder);
    amber::create_sequence_item(nullptr, nested, false, folder.get());
    QVERIFY(amber::project_has_sequence(top.get()));
    QVERIFY(amber::project_has_sequence(nested.get()));
    QVERIFY(!amber::project_has_sequence(outside.get()));
    QVERIFY(!amber::project_has_sequence(nullptr));
  }

  // A row whose sequence is no longer in the project is Skipped with the reason, without rendering anything; Done
  // rows are left as they are
  void goneSequenceSkipped() {
    RenderQueue& q = amber::render_queue();
    SequencePtr gone = sequence("Gone", 10);
    amber::QueuedJob done = job(gone, "/out/done.mp4");
    done.outcome.status = BatchExport::Outcome::Exported;
    QVERIFY(q.Add(done).isEmpty());
    QVERIFY(q.Add(job(gone, "/out/gone.mp4")).isEmpty());
    QSignalSpy started(&q, &RenderQueue::JobStarted);
    QSignalSpy finished(&q, &RenderQueue::Finished);

    q.Render();
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(!q.IsRendering());
    QCOMPARE(started.count(), 0);
    QCOMPARE(q.Jobs().at(0).outcome.status, BatchExport::Outcome::Exported);
    QCOMPARE(q.Jobs().at(1).outcome.status, BatchExport::Outcome::Skipped);
    QCOMPARE(q.Jobs().at(1).outcome.reason, QString("the sequence is no longer in the project"));
    QCOMPARE(q.PendingCount(), 1);  // a Skipped row is tried again by the next Render()
  }

  // New, open and close clear the project model (Project::clear()): the queue goes with it. An import doesn't reset it.
  void clearedWithTheProject() {
    RenderQueue& q = amber::render_queue();
    SequencePtr a = sequence("A", 10);
    amber::create_sequence_item(nullptr, a, false, nullptr);
    QVERIFY(q.Add(job(a, "/out/a.mp4")).isEmpty());
    amber::create_sequence_item(nullptr, sequence("Imported", 10), false, nullptr);
    QCOMPARE(q.Count(), 1);

    QSignalSpy changed(&q, &RenderQueue::Changed);
    amber::project_model.clear();
    QCOMPARE(q.Count(), 0);
    QCOMPARE(changed.count(), 1);
  }

  // A batch queued into a folder gets names that no row of that folder has: its files, and an image sequence's pattern
  // as the name it was made from
  void takenNames() {
    RenderQueue& q = amber::render_queue();
    SequencePtr a = sequence("A", 10);
    QVERIFY(q.Add(job(a, "/out/Wide.mp4")).isEmpty());
    QVERIFY(q.Add(job(a, "/out/Tall%05d.png")).isEmpty());
    QVERIFY(q.Add(job(a, "/out/100%% (2)%05d.png")).isEmpty());
    QVERIFY(q.Add(job(a, "/elsewhere/Other.mp4")).isEmpty());
    QVERIFY(q.Add(job(a, "/out/sub/Deeper.mp4")).isEmpty());
    QCOMPARE(q.TakenNames("/out"), (QStringList{"Wide.mp4", "Tall.png", "100% (2).png"}));
    QCOMPARE(q.TakenNames("/OUT/"), (QStringList{"Wide.mp4", "Tall.png", "100% (2).png"}));
    QCOMPARE(q.TakenNames("/elsewhere"), QStringList{"Other.mp4"});
    QCOMPARE(q.TakenNames("/nowhere"), QStringList{});
    // As ExportDialog::QueueExport() names a batch: after the queued names
    QCOMPARE(amber::batch_export_filename("Wide", "mp4", q.TakenNames("/out")), QString("Wide (2).mp4"));
    QCOMPARE(amber::batch_export_filename("Tall", "png", q.TakenNames("/out")), QString("Tall (2).png"));
  }

  // Quitting clears the queue from exec(), while the app is still whole: its sequences don't wait for static
  // destruction. Last: it runs and leaves the application's event loop.
  void clearedAtQuit() {
    RenderQueue& q = amber::render_queue();
    QVERIFY(q.Add(job(sequence("Deleted", 10), "/out/deleted.mp4")).isEmpty());
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    QCoreApplication::exec();
    QCOMPARE(q.Count(), 0);
  }
};

QTEST_GUILESS_MAIN(TestRenderQueue)
#include "test_render_queue.moc"
