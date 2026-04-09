#include <QtTest>

// Minimal Sequence stub — just enough for shared_ptr to work.
// The real Sequence lives in engine/sequence.h but drags in Clip/Cacher/FFmpeg.
#include "engine/sequence_fwd.h"
class Sequence {
 public:
  Sequence() = default;
  ~Sequence() = default;
};
SequencePtr amber::ActiveSequence;

#include "core/appcontext.h"
#include "global/projectio.h"

class StubAppContext : public AppContext {
 public:
  void refreshViewer() override {}
  void refreshEffectControls() override {}
  void updateKeyframes() override {}
  void clearEffectControls(bool) override {}
  void refreshTimeline() override {}
  void deselectArea(long, long, int) override {}
  void seekPlayhead(long) override {}
  void pausePlayback() override {}
  void setGraphEditorRow(EffectRow*) override {}
  bool isPlaying() override { return false; }
  bool isEffectSelected(Effect*) override { return false; }
  void setAudioMonitorValues(const QVector<double>&) override {}
  QVector<Media*> listAllSequences() override { return {}; }
  void processFileList(QStringList&, bool, MediaPtr, Media*) override {}
  bool isToolbarVisible() override { return false; }
  bool isModified() override { return false; }
  void setModified(bool) override {}
  void updateUi(bool) override {}
  int showMessage(const QString&, const QString&, int) override { return 0; }
  bool showQuestion(const QString&, const QString&) override { return true; }
};

class TestProjectIO : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    stub_ctx_ = new StubAppContext();
    amber::app_ctx = stub_ctx_;
  }

  void cleanupTestCase() {
    amber::app_ctx = nullptr;
    delete stub_ctx_;
  }

  void modifiedStateTracking() {
    ProjectIO pio;
    QCOMPARE(pio.isModified(), false);
    pio.setModified(true);
    QCOMPARE(pio.isModified(), true);
    pio.setModified(false);
    QCOMPARE(pio.isModified(), false);
  }

  void projectFilename() {
    ProjectIO pio;
    QVERIFY(pio.projectFilename().isEmpty());
    QSignalSpy spy(&pio, &ProjectIO::projectFilenameChanged);
    pio.setProjectFilename("/tmp/test.ove");
    QCOMPARE(pio.projectFilename(), "/tmp/test.ove");
    QCOMPARE(spy.count(), 1);
  }

  void recentProjects() {
    ProjectIO pio;
    pio.addRecentProject("/tmp/a.ove");
    pio.addRecentProject("/tmp/b.ove");
    QCOMPARE(pio.recentProjects().size(), 2);
    QCOMPARE(pio.recentProjects().first(), "/tmp/b.ove");
    // Adding duplicate moves to front
    pio.addRecentProject("/tmp/a.ove");
    QCOMPARE(pio.recentProjects().first(), "/tmp/a.ove");
    QCOMPARE(pio.recentProjects().size(), 2);
  }

  void sequenceHistory() {
    ProjectIO pio;
    amber::ActiveSequence = nullptr;

    auto seq1 = std::make_shared<Sequence>();
    auto seq2 = std::make_shared<Sequence>();

    pio.setSequence(seq1);
    QCOMPARE(amber::ActiveSequence, seq1);
    QVERIFY(!pio.canGoBack());

    pio.setSequence(seq2, true);
    QCOMPARE(amber::ActiveSequence, seq2);
    QVERIFY(pio.canGoBack());

    pio.goBackSequence();
    QCOMPARE(amber::ActiveSequence, seq1);
    QVERIFY(!pio.canGoBack());

    pio.setSequence(nullptr);
  }

  void canCloseProject() {
    ProjectIO pio;
    QCOMPARE(pio.canCloseProject(), ProjectIO::Clean);
    pio.setModified(true);
    QCOMPARE(pio.canCloseProject(), ProjectIO::NeedsSave);
  }

 private:
  StubAppContext* stub_ctx_{nullptr};
};

QTEST_MAIN(TestProjectIO)
#include "test_projectio.moc"
