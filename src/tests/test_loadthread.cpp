// Headless project loading: runs LoadThread the way the app does (worker thread + main event loop) on a tiny
// hand-written .ove containing a folder and a sequence (plus, in probesFootage, one footage item probed by the real
// PreviewGenerator), with no UI code linked.

#include <QtTest>

#include <QPointer>
#include <QTemporaryDir>

#include "effects/effect.h"
#include "effects/effectfield.h"
#include "effects/effectloaders.h"
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
  bool last_default_no = false;  // default button of the last question
  bool isInteractive() override { return interactive; }
  int recents = 0;
  void addRecentProject(const QString&) override { recents++; }
  bool showQuestion(const QString&, const QString&, bool, bool default_no) override {
    questions++;
    last_default_no = default_no;
    return answer;
  }
  int showMessage(const QString&, const QString&, int) override {
    messages++;
    return 0;
  }
};

// Same layout Project::save_project() writes. Folder 1 sits at the root; sequence 1 lives in it and is the open one.
// A non-empty footage_url adds footage 1 to folder 1. Non-empty clip_effects (<effect> elements) put a clip carrying
// them in the sequence.
QString fixture(int version, const QString& url, const QString& footage_url = QString(),
                const QString& clip_effects = QString()) {
  QString media = footage_url.isEmpty() ? QStringLiteral("<media/>")
                                        : QString(R"(<media>
        <footage id="1" folder="1" name="clip" url="%1"/>
    </media>)")
                                              .arg(footage_url.toHtmlEscaped());
  QString sequence_body = clip_effects.isEmpty() ? QStringLiteral("/>")
                                                 : QString(R"(>
            <clip id="0" enabled="1" name="title" clipin="0" in="0" out="30" track="-1" r="192" g="128" b="128"
                  autoscale="1" speed="1" maintainpitch="0" reverse="0">
                <linked></linked>
                %1
            </clip>
        </sequence>)")
                                                       .arg(clip_effects);
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
                  alayout="3" open="1" label="4"%4
    </sequences>
</project>
)")
      .arg(QString::number(version), url.toHtmlEscaped(), media, sequence_body);
}

}  // namespace

class TestLoadThread : public QObject {
  Q_OBJECT

 private:
  CountingAppContext ctx_;
  ProjectIO* pio_ = nullptr;
  QTemporaryDir dir_;

  QString write_fixture(const QString& name, int version, const QString& footage_url = QString(),
                        const QString& clip_effects = QString()) {
    QString path = dir_.filePath(name);
    QFile f(path);
    if (!f.open(QFile::WriteOnly | QFile::Truncate)) return {};
    f.write(fixture(version, path, footage_url, clip_effects).toUtf8());
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
    ctx_.interactive = false;
    ctx_.answer = false;
    ctx_.recents = 0;
    set_project_uses_frei0r(false);  // what AmberGlobal::ClearProject() does before each open
    amber::CurrentConfig.show_frei0r_effects = false;
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

  void frei0rEffectName() {
    QVERIFY(is_frei0r_effect_name("Frei0r/Glow"));
    QVERIFY(!is_frei0r_effect_name("Distort/Transform"));
    QVERIFY(!is_frei0r_effect_name("Glow"));
    QVERIFY(!is_frei0r_effect_name("frei0r/Glow"));  // Effect::save writes the category as registered
    QVERIFY(!is_frei0r_effect_name(QString()));
  }

  void frei0rVisibilityRule() {
    QVERIFY(!frei0r_effects_visible(false, false));
    QVERIFY(frei0r_effects_visible(true, false));
    QVERIFY(frei0r_effects_visible(false, true));
    QVERIFY(frei0r_effects_visible(true, true));

    QVERIFY(!frei0r_effects_visible());
    amber::CurrentConfig.show_frei0r_effects = true;
    QVERIFY(frei0r_effects_visible());
    amber::CurrentConfig.show_frei0r_effects = false;
    set_project_uses_frei0r(true);
    QVERIFY(frei0r_effects_visible());
    QVERIFY(!amber::CurrentConfig.show_frei0r_effects);  // the project override never touches the preference
    set_project_uses_frei0r(false);
    QVERIFY(!frei0r_effects_visible());
  }

  // A project with Frei0r effects shows them in the add-effect menus (built with Frei0r), or says once that they
  // won't render (built without). Either way they load as effects of the clip. The plugins aren't loaded here, so
  // they are "Missing Effect" placeholders in both builds.
  void frei0rProjectShowsFrei0rEffects() {
    QString path = write_fixture(
        "frei0r.ove", amber::kSaveVersion, QString(),
        R"(<effect name="Frei0r/Glow" enabled="1"></effect><effect name="Frei0r/Vertigo" enabled="1"></effect>)");
    QVERIFY(!path.isEmpty());

    bool succeeded = false;
    int failed = 0;  // error() emissions
    run_load(path, false, &succeeded, &failed);
    QVERIFY(succeeded);
    QVERIFY(!failed);
    QVERIFY(amber::ActiveSequence != nullptr);
    QCOMPARE(amber::ActiveSequence->clips.size(), 1);
    QCOMPARE(amber::ActiveSequence->clips.at(0)->effects.size(), 2);
    QVERIFY(!amber::CurrentConfig.show_frei0r_effects);
#ifdef NOFREI0R
    QCOMPARE(ctx_.messages, 1);
    QVERIFY(!frei0r_effects_visible());
#else
    QCOMPARE(ctx_.messages, 0);
    QVERIFY(frei0r_effects_visible());
#endif
  }

  // A project referencing a ShaderToy effect whose .shadertoy isn't installed loads it as a Missing Effect, which
  // saves back what it read (name, enabled, field values), so the project survives a round trip on that machine
  void missingShaderToyEffectSavesBack() {
    QString path = write_fixture(
        "shadertoy.ove", amber::kSaveVersion, QString(),
        R"(<effect name="ShaderToy/Missing" enabled="1"><row><field id="speed" value="2.5"/></row></effect>)");
    bool succeeded = false;
    int failed = 0;  // error() emissions
    run_load(path, false, &succeeded, &failed);
    QVERIFY(succeeded);
    QVERIFY(!failed);
    QVERIFY(amber::ActiveSequence != nullptr);
    QCOMPARE(amber::ActiveSequence->clips.size(), 1);
    QCOMPARE(amber::ActiveSequence->clips.at(0)->effects.size(), 1);
    Effect* e = amber::ActiveSequence->clips.at(0)->effects.at(0).get();
    QCOMPARE(e->name, QStringLiteral("ShaderToy/Missing"));
    QCOMPARE(e->row(0)->name(), QStringLiteral("Missing Effect"));

    QByteArray saved;
    {
      QXmlStreamWriter w(&saved);
      w.writeStartElement("effect");
      e->save(w);
      w.writeEndElement();
    }
    QVERIFY2(saved.contains(R"(name="ShaderToy/Missing")"), saved.constData());
    QVERIFY2(saved.contains(R"(<field id="speed" value="2.5")"), saved.constData());
  }

  // Importing a project with Frei0r effects into the open one shows them too; a project without any leaves them hidden
  void frei0rImportShowsFrei0rEffects() {
    QString plain = write_fixture("plain.ove", amber::kSaveVersion, QString(),
                                  R"(<effect name="Distort/Transform" enabled="1"></effect>)");
    bool succeeded = false;
    int failed = 0;  // error() emissions
    run_load(plain, false, &succeeded, &failed);
    QVERIFY(succeeded);
    QVERIFY(!frei0r_effects_visible());
    QCOMPARE(ctx_.messages, 0);

    QString path = write_fixture("frei0r-import.ove", amber::kSaveVersion, QString(),
                                 R"(<effect name="Frei0r/Glow" enabled="1"></effect>)");
    succeeded = false;
    run_load(path, false, &succeeded, &failed, true);
    QVERIFY(succeeded);
    QVERIFY(!failed);
#ifdef NOFREI0R
    QCOMPARE(ctx_.messages, 1);
    QVERIFY(!frei0r_effects_visible());
#else
    QCOMPARE(ctx_.messages, 0);
    QVERIFY(frei0r_effects_visible());
#endif
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
    QVERIFY(!ctx_.last_default_no);
    QVERIFY(!succeeded);
    // error() once (the app clears the project on it), no message box: the user chose to stop
    QCOMPARE(failed, 1);
    QCOMPARE(ctx_.messages, 0);
    QCOMPARE(amber::project_model.childCount(), 0);
    QVERIFY(amber::ActiveSequence == nullptr);
    QCOMPARE(pio_->isModified(), true);
  }

  // AmberGlobal::LoadProject() closes its dialog on finished() and tells a cancelled load by neither success() nor
  // error() having been emitted
  void cancelledLoadFinishesSilently() {
    QString path = write_fixture("cancel.ove", amber::kSaveVersion);
    QVERIFY(!path.isEmpty());
    auto* lt = new LoadThread(path, false);
    QPointer<LoadThread> guard(lt);
    bool succeeded = false;
    int failed = 0;
    connect(lt, &LoadThread::success, this, [&succeeded]() { succeeded = true; });
    connect(lt, &LoadThread::error, this, [&failed]() { ++failed; });
    lt->cancel();
    lt->start();
    QTRY_VERIFY_WITH_TIMEOUT(guard.isNull(), 10000);
    QVERIFY(!succeeded);
    QCOMPARE(failed, 0);
    QCOMPARE(ctx_.messages, 0);
    QCOMPARE(ctx_.recents, 0);
  }

  // A load cancelled after it has parsed footage never started the probes that release each footage's ready lock:
  // clearing the project afterwards (what AmberGlobal does) must not destroy a locked QMutex
  void cancelledLoadLeavesNoLockedMutex() {
    QTest::failOnWarning(QRegularExpression("destroying locked mutex"));
    QString media;
    for (int i = 1; i <= 2000; i++) {
      media += QString(R"(<footage id="%1" folder="1" name="clip %1" url="%2"/>)")
                   .arg(QString::number(i), dir_.filePath("missing-%1.mp4").arg(i).toHtmlEscaped());
    }
    QString path = dir_.filePath("cancel-footage.ove");
    {
      QFile f(path);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(fixture(amber::kSaveVersion, path).replace("<media/>", "<media>" + media + "</media>").toUtf8());
    }

    auto* lt = new LoadThread(path, false);
    QPointer<LoadThread> guard(lt);
    bool succeeded = false;
    int failed = 0;
    connect(lt, &LoadThread::success, this, [&succeeded]() { succeeded = true; });
    connect(lt, &LoadThread::error, this, [&failed]() { ++failed; });
    // Hold the worker a tenth of the way through, then cancel: it still has ~1800 footage elements to go
    QSemaphore reached, go;
    std::atomic<bool> held{false};
    connect(
        lt, &LoadThread::report_progress, lt,
        [&](int p) {
          if (p >= 10 && !held.exchange(true)) {
            reached.release();
            go.acquire();
          }
        },
        Qt::DirectConnection);
    lt->start();
    QVERIFY(reached.tryAcquire(1, 10000));
    go.release();
    lt->cancel();
    QTRY_VERIFY_WITH_TIMEOUT(guard.isNull(), 10000);
    QVERIFY(!succeeded);
    QCOMPARE(failed, 0);

    Media* folder = amber::project_model.child(0);
    QVERIFY(folder != nullptr);
    QVERIFY(amber::project_model.childCount(folder) > 0);  // footage was parsed before the cancel
    amber::project_model.clear();
  }

  // Load progress counts each folder/footage/sequence/clip/effect once: it only rises, and reaches 100% with the last
  // one. Older projects have no <previewresolution>: looking for it scans the whole file, which must count nothing.
  void progressRisesToHundredAtTheEnd() {
    const QString effects = QStringLiteral(R"(<effect name="Distort/Transform" enabled="1"></effect>
                <effect name="Distort/Transform" enabled="1"></effect>)");
    for (bool old_style : {true, false}) {
      amber::project_model.clear();
      QString path = dir_.filePath(old_style ? "progress-old.ove" : "progress.ove");
      {
        QFile f(path);
        QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
        QString xml = fixture(amber::kSaveVersion, path, QString(), effects);
        if (old_style) xml.remove("<previewresolution>1</previewresolution>");
        f.write(xml.toUtf8());
      }
      auto* lt = new LoadThread(path, false);
      QPointer<LoadThread> guard(lt);
      bool succeeded = false;
      QVector<int> values;
      connect(lt, &LoadThread::success, this, [&succeeded]() { succeeded = true; });
      connect(lt, &LoadThread::report_progress, this, [&values](int p) { values.append(p); });
      lt->start();
      QTRY_VERIFY_WITH_TIMEOUT(guard.isNull(), 10000);
      QVERIFY(succeeded);
      // folder, sequence, clip, two effects
      QCOMPARE(values, QVector<int>({20, 40, 60, 80, 100}));
    }
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
    if (effects.empty()) load_internal_effects();
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
    QVERIFY(ctx_.last_default_no);  // Enter must not load native code

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
