// Headless project loading: runs LoadThread the way the app does (worker thread + main event loop) on a tiny
// hand-written .ove containing a folder and a sequence (plus, in probesFootage, one footage item probed by the real
// PreviewGenerator), with no UI code linked.

#include <QtTest>

#include <QPointer>
#include <QTemporaryDir>

#include "effects/effect.h"
#include "effects/effectfield.h"
#include "effects/effectrow.h"
#include "engine/sequence.h"
#include "global/config.h"
#include "global/projectio.h"
#include "project/loadthread.h"
#include "project/media.h"
#include "project/previewgenerator.h"
#include "project/projectmodel.h"
#include "tests/test_appcontext_stub.h"

// External entry point from effectloaders.cpp (no header export), as in test_render_harness.cpp
extern void load_internal_effects();

namespace {

// Counts questions and errors; answers every question "No" (like the base stub) unless told otherwise.
class CountingAppContext : public TestAppContext {
 public:
  int questions = 0;
  int messages = 0;
  bool interactive = false;  // pretend a user answers (VST trust question)
  bool answer = false;
  bool isInteractive() override { return interactive; }
  bool showQuestion(const QString&, const QString&, bool) override {
    questions++;
    return answer;
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
  void run_load(const QString& path, bool autorecovery, bool* succeeded, int* failed) {
    auto* lt = new LoadThread(path, autorecovery);
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
    ctx_.interactive = false;
    ctx_.answer = false;
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

  // A relative footage url resolves against the project's folder, never against the process's working directory,
  // even when the working directory holds a file of the same name
  void relativeFootageIgnoresWorkingDir() {
    const QString clip = QStringLiteral(SOURCE_DIR "/tests/fixtures/t1_yuv_input.mp4");
    QVERIFY(QDir(dir_.path()).mkpath("proj"));
    QVERIFY(QDir(dir_.path()).mkpath("cwd"));
    const QString proj_copy = dir_.filePath("proj/same.mp4");
    QVERIFY(QFile::exists(proj_copy) || QFile::copy(clip, proj_copy));
    QVERIFY(QFile::exists(dir_.filePath("cwd/same.mp4")) || QFile::copy(clip, dir_.filePath("cwd/same.mp4")));
    QString path = write_fixture("proj/relative.ove", amber::kSaveVersion, "same.mp4");
    QVERIFY(!path.isEmpty());

    const QString old_cwd = QDir::currentPath();
    QVERIFY(QDir::setCurrent(dir_.filePath("cwd")));
    bool succeeded = false;
    int failed = 0;  // error() emissions
    run_load(path, false, &succeeded, &failed);
    QDir::setCurrent(old_cwd);
    QVERIFY(succeeded);
    QVERIFY(!failed);

    Media* folder = amber::project_model.child(0);
    Media* item = nullptr;
    for (int i = 0; i < amber::project_model.childCount(folder); i++) {
      Media* m = amber::project_model.child(i, folder);
      if (m->get_type() == MEDIA_TYPE_FOOTAGE) item = m;
    }
    QVERIFY(item != nullptr);
    Footage* f = item->to_footage();
    QPointer<PreviewGenerator> gen(f->preview_gen.load());
    QTRY_VERIFY_WITH_TIMEOUT(gen.isNull(), 10000);
    QCOMPARE(f->url, proj_copy);
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

  // A VST plugin path set by a project load or a paste (a field change, as LoadFieldElement does) only loads when
  // trusted. The paths don't exist: a load attempt shows up as one "failed to load" message, no attempt as none.
  void vstPluginLoadsOnlyTrustedPaths() {
    if (effects.isEmpty()) load_internal_effects();
    const EffectMeta* meta = get_meta_from_name("VST Plugin 2.x");
    QVERIFY(meta != nullptr);
    const QString dir = dir_.filePath("no-such-dir");
    auto set_path = [meta](const QString& path) {
      EffectPtr e(Effect::Create(nullptr, meta));
      e->row(0)->Field(0)->SetValueAt(0, path);
      QCoreApplication::processEvents();  // the queued change_plugin()
      return e;
    };

    // Headless: never loaded, never asked, the path is kept for saving
    EffectPtr headless = set_path(dir + "/headless.so");
    QCOMPARE(ctx_.questions, 0);
    QCOMPARE(ctx_.messages, 0);
    QCOMPARE(headless->row(0)->Field(0)->GetValueAt(0).toString(), dir + "/headless.so");

    // Interactive, answered No: asked once per path, not loaded
    ctx_.interactive = true;
    EffectPtr no1 = set_path(dir + "/no.so");
    EffectPtr no2 = set_path(dir + "/no.so");
    QCOMPARE(ctx_.questions, 1);
    QCOMPARE(ctx_.messages, 0);

    // Answered Yes: loaded (and failed), trusted from now on, even headless
    ctx_.answer = true;
    EffectPtr yes = set_path(dir + "/yes.so");
    QCOMPARE(ctx_.questions, 2);
    QCOMPARE(ctx_.messages, 1);
    QVERIFY(amber::CurrentConfig.trusted_vst_plugins.contains(dir + "/yes.so"));
    ctx_.interactive = false;
    EffectPtr again = set_path(dir + "/yes.so");
    QCOMPARE(ctx_.questions, 2);
    QCOMPARE(ctx_.messages, 2);
    amber::CurrentConfig.trusted_vst_plugins.clear();
  }
};

QTEST_GUILESS_MAIN(TestLoadThread)
#include "test_loadthread.moc"
