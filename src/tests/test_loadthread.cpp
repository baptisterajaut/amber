// Headless project loading: runs LoadThread the way the app does (worker thread + main event loop) on a tiny
// hand-written .ove containing a folder and a sequence (plus, in probesFootage, one footage item probed by the real
// PreviewGenerator), with no UI code linked.

#include <QtTest>

#include <QPointer>
#include <QTemporaryDir>

#include "engine/sequence.h"
#include "global/config.h"
#include "global/projectio.h"
#include "project/loadthread.h"
#include "project/media.h"
#include "project/previewgenerator.h"
#include "project/projectmodel.h"
#include "tests/test_appcontext_stub.h"

namespace {

// Counts questions and errors; answers every question "No" (like the base stub).
class CountingAppContext : public TestAppContext {
 public:
  int questions = 0;
  int messages = 0;
  int recents = 0;
  void addRecentProject(const QString&) override { recents++; }
  bool showQuestion(const QString&, const QString&, bool) override {
    questions++;
    return false;
  }
  int showMessage(const QString&, const QString&, int) override {
    messages++;
    return 0;
  }
};

// Same layout Project::save_project() writes. Folder 1 sits at the root; sequence 1 lives in it and is the open one.
// A non-empty footage_url adds footage 1 to folder 1.
QString fixture(int version, const QString& url, const QString& footage_url = QString()) {
  QString media = footage_url.isEmpty() ? QStringLiteral("<media/>")
                                        : QString(R"(<media>
        <footage id="1" folder="1" name="clip" url="%1"/>
    </media>)")
                                              .arg(footage_url.toHtmlEscaped());
  return QString(R"(<?xml version="1.0" encoding="UTF-8"?>
<project>
    <version>%1</version>
    <url>%2</url>
    <previewresolution>1</previewresolution>
    <folders>
        <folder id="1" name="Bin" parent="0" label="2"/>
    </folders>
    %3
    <sequences>
        <sequence id="1" folder="1" name="Main Edit" width="1280" height="720" framerate="25" afreq="48000"
                  alayout="3" open="1" label="4"/>
    </sequences>
</project>
)")
      .arg(QString::number(version), url.toHtmlEscaped(), media);
}

}  // namespace

class TestLoadThread : public QObject {
  Q_OBJECT

 private:
  CountingAppContext ctx_;
  ProjectIO* pio_ = nullptr;
  QTemporaryDir dir_;

  QString write_fixture(const QString& name, int version, const QString& footage_url = QString()) {
    QString path = dir_.filePath(name);
    QFile f(path);
    if (!f.open(QFile::WriteOnly | QFile::Truncate)) return {};
    f.write(fixture(version, path, footage_url).toUtf8());
    return path;
  }

  // Start a LoadThread and pump the event loop until it has finished and deleted itself (which happens after its
  // queued success/error handlers have run on this thread).
  void run_load(const QString& path, bool autorecovery, bool* succeeded, int* failed, bool import = false) {
    auto* lt = new LoadThread(path, autorecovery, import);
    QPointer<LoadThread> guard(lt);
    connect(lt, &LoadThread::success, this, [succeeded]() { *succeeded = true; });
    connect(lt, &LoadThread::error, this, [failed]() { ++*failed; });
    lt->start();
    QTRY_VERIFY_WITH_TIMEOUT(guard.isNull(), 10000);
  }

 private slots:
  void initTestCase() {
    QVERIFY(dir_.isValid());
    // Keep the probe's thumbnail cache out of the user's home (test mode would write to ~/.qttest)
    qputenv("XDG_DATA_HOME", dir_.filePath("data").toUtf8());
    qputenv("XDG_CACHE_HOME", dir_.filePath("cache").toUtf8());
    qputenv("XDG_CONFIG_HOME", dir_.filePath("config").toUtf8());
    amber::app_ctx = &ctx_;
    pio_ = new ProjectIO();
    amber::project_io = pio_;
  }

  void cleanupTestCase() {
    amber::ActiveSequence = nullptr;
    amber::project_model.clear();
    amber::project_io = nullptr;
    delete pio_;
    amber::app_ctx = nullptr;
  }

  void init() {
    amber::ActiveSequence = nullptr;
    amber::project_model.clear();
    pio_->setProjectFilename(QString());
    pio_->setModified(true);
    ctx_.questions = 0;
    ctx_.messages = 0;
    ctx_.recents = 0;
  }

  void loadsFolderAndSequence() {
    QString path = write_fixture("project.ove", amber::kSaveVersion);
    QVERIFY(!path.isEmpty());

    bool succeeded = false;
    int failed = 0;  // error() emissions
    run_load(path, false, &succeeded, &failed);
    QVERIFY(succeeded);
    QVERIFY(!failed);
    QCOMPARE(ctx_.questions, 0);
    QCOMPARE(ctx_.messages, 0);

    // Model: root -> "Bin" folder -> "Main Edit" sequence
    QCOMPARE(amber::project_model.childCount(), 1);
    Media* folder = amber::project_model.child(0);
    QCOMPARE(folder->get_type(), MEDIA_TYPE_FOLDER);
    QCOMPARE(folder->get_name(), QString("Bin"));
    QCOMPARE(folder->color_label(), 2);

    QCOMPARE(amber::project_model.childCount(folder), 1);
    Media* seq_item = amber::project_model.child(0, folder);
    QCOMPARE(seq_item->get_type(), MEDIA_TYPE_SEQUENCE);
    QCOMPARE(seq_item->color_label(), 4);
    SequencePtr seq = seq_item->to_sequence();
    QVERIFY(seq != nullptr);
    QCOMPARE(seq->name, QString("Main Edit"));
    QCOMPARE(seq->width, 1280);
    QCOMPARE(seq->height, 720);
    QCOMPARE(seq->frame_rate, 25.0);

    // ProjectIO: the open sequence became active, project is clean, filename untouched (the caller sets it before
    // loading a regular project)
    QCOMPARE(amber::ActiveSequence, seq);
    QCOMPARE(pio_->isModified(), false);
    QCOMPARE(pio_->projectFilename(), QString());
  }

  // File > Import of a .ove into the open project: the content is merged, the open project stays modified, keeps
  // its active sequence, and the imported file doesn't become a recent project.
  void importKeepsOpenProjectState() {
    QString path = write_fixture("import.ove", amber::kSaveVersion);
    QVERIFY(!path.isEmpty());
    pio_->setModified(false);

    bool succeeded = false;
    int failed = 0;  // error() emissions
    run_load(path, false, &succeeded, &failed, true);
    QVERIFY(succeeded);
    QVERIFY(!failed);

    QCOMPARE(amber::project_model.childCount(), 1);
    QCOMPARE(pio_->isModified(), true);
    QVERIFY(amber::ActiveSequence == nullptr);  // the imported open="1" sequence isn't activated
    QCOMPARE(ctx_.recents, 0);
    QCOMPARE(pio_->projectFilename(), QString());
  }

  void autorecoveryRenamesProject() {
    QString path = write_fixture("recover.ove", amber::kSaveVersion);
    QVERIFY(!path.isEmpty());

    bool succeeded = false;
    int failed = 0;  // error() emissions
    run_load(path, true, &succeeded, &failed);
    QVERIFY(succeeded);
    QVERIFY(!failed);

    // The <url> file exists, so the recovered project gets a fresh name next to it and is marked modified
    QCOMPARE(pio_->projectFilename(), dir_.filePath("recover (recovered).ove"));
    QCOMPARE(pio_->isModified(), true);
    QCOMPARE(amber::project_model.childCount(), 1);
    QVERIFY(amber::ActiveSequence != nullptr);
  }

  void probesFootage() {
    const QString clip = QStringLiteral(SOURCE_DIR "/tests/fixtures/t1_yuv_input.mp4");
    QVERIFY(QFileInfo::exists(clip));
    QString path = write_fixture("footage.ove", amber::kSaveVersion, clip);
    QVERIFY(!path.isEmpty());

    bool succeeded = false;
    int failed = 0;  // error() emissions
    run_load(path, false, &succeeded, &failed);
    QVERIFY(succeeded);
    QVERIFY(!failed);

    Media* folder = amber::project_model.child(0);
    QCOMPARE(folder->get_type(), MEDIA_TYPE_FOLDER);
    Media* item = nullptr;
    for (int i = 0; i < amber::project_model.childCount(folder); i++) {
      Media* m = amber::project_model.child(i, folder);
      if (m->get_type() == MEDIA_TYPE_FOOTAGE) item = m;
    }
    QVERIFY(item != nullptr);
    Footage* f = item->to_footage();
    QCOMPARE(f->url, clip);

    // LoadThread started the probe before finishing; it runs on its own thread and deletes itself on the main loop.
    // Nothing can delete it before the event loop is pumped again, so guarding it here is race-free.
    QPointer<PreviewGenerator> gen(f->preview_gen.load());
    QTRY_VERIFY_WITH_TIMEOUT(gen.isNull(), 10000);

    QVERIFY(f->ready);
    QVERIFY(!f->invalid);
    QVERIFY(f->length > 0);
    QCOMPARE(f->audio_tracks.size(), 0);
    QCOMPARE(f->video_tracks.size(), 1);
    const FootageStream& vs = f->video_tracks.at(0);
    QCOMPARE(vs.video_width, 64);
    QCOMPARE(vs.video_height, 64);
    QCOMPARE(vs.video_frame_rate, 30.0);
    QVERIFY(!vs.infinite_length);
    // Thumbnail generation ran too (or was read back from the previews cache of an earlier run)
    QVERIFY(vs.preview_done);
    QVERIFY(!vs.video_preview.isNull());
  }

  void versionMismatchAsksAndAborts() {
    QString path = write_fixture("old.ove", 1);
    QVERIFY(!path.isEmpty());

    bool succeeded = false;
    int failed = 0;  // error() emissions
    run_load(path, false, &succeeded, &failed);

    // The worker blocked on the question, the main thread answered "No" through app_ctx, loading stopped
    QCOMPARE(ctx_.questions, 1);
    QVERIFY(!succeeded);
    // error() once (the app clears the project on it), no message box: the user chose to stop
    QCOMPARE(failed, 1);
    QCOMPARE(ctx_.messages, 0);
    QCOMPARE(amber::project_model.childCount(), 0);
    QVERIFY(amber::ActiveSequence == nullptr);
    QCOMPARE(pio_->isModified(), true);
  }

  // A file that can't be opened is a load error like any other: error() once, one message
  void unreadableFileEmitsErrorOnce() {
    bool succeeded = false;
    int failed = 0;  // error() emissions
    run_load(dir_.filePath("does-not-exist.ove"), false, &succeeded, &failed);
    QVERIFY(!succeeded);
    QCOMPARE(failed, 1);
    QCOMPARE(ctx_.messages, 1);
  }

  void malformedXmlEmitsErrorOnce() {
    QString path = dir_.filePath("broken.ove");
    {
      QFile f(path);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write("<?xml version=\"1.0\"?>\n<project>\n<version>190219</version>\n<folders><folder id=\"1\"</project>\n");
    }
    bool succeeded = false;
    int failed = 0;  // error() emissions
    run_load(path, false, &succeeded, &failed);
    QVERIFY(!succeeded);
    QCOMPARE(failed, 1);
    QCOMPARE(ctx_.messages, 1);
  }
};

QTEST_GUILESS_MAIN(TestLoadThread)
#include "test_loadthread.moc"
