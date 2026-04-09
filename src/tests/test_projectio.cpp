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
#include "core/audio.h"
#include "global/projectio.h"

Q_DECLARE_METATYPE(SequencePtr)

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
    // Clearing modified restores Clean state
    pio.setModified(false);
    QCOMPARE(pio.canCloseProject(), ProjectIO::Clean);
  }

  // --- Signal tests ---

  void modifiedSignal() {
    ProjectIO pio;
    QSignalSpy spy(&pio, &ProjectIO::modifiedChanged);
    pio.setModified(true);
    pio.setModified(false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).toBool(), true);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
  }

  void sequenceChangedSignal() {
    ProjectIO pio;
    amber::ActiveSequence = nullptr;
    QSignalSpy spy(&pio, &ProjectIO::sequenceChanged);
    auto seq1 = std::make_shared<Sequence>();
    pio.setSequence(seq1);
    QCOMPARE(spy.count(), 1);
    // The emitted pointer must match
    SequencePtr emitted = spy.at(0).at(0).value<SequencePtr>();
    QCOMPARE(emitted, seq1);
    pio.setSequence(nullptr);
  }

  // --- Autorecovery ---

  void autorecoverySaveRequested() {
    ProjectIO pio;
    QSignalSpy spy(&pio, &ProjectIO::autorecoverySaveRequested);

    // Not modified — saveAutorecoveryFile should NOT emit
    pio.saveAutorecoveryFile();
    QCOMPARE(spy.count(), 0);

    // Mark modified, then save — signal emitted once
    pio.setModified(true);
    pio.saveAutorecoveryFile();
    QCOMPARE(spy.count(), 1);

    // Second save without re-modifying — changed_since_last_autorecovery_ was cleared
    pio.saveAutorecoveryFile();
    QCOMPARE(spy.count(), 1);
  }

  void renderingStatePausesAutorecovery() {
    ProjectIO pio;
    // Initial state: audio_rendering default is false
    audio_rendering = false;

    pio.setRenderingState(true);
    QCOMPARE(audio_rendering.load(), true);

    pio.setRenderingState(false);
    QCOMPARE(audio_rendering.load(), false);

    // Restore
    audio_rendering = false;
  }

  // --- Recent projects edge cases ---

  void recentProjectsCap() {
    ProjectIO pio;
    for (int i = 0; i < 12; ++i) {
      pio.addRecentProject(QString("/tmp/project_%1.ove").arg(i));
    }
    QCOMPARE(pio.recentProjects().size(), 10);
    // Most recent is the last added
    QCOMPARE(pio.recentProjects().first(), "/tmp/project_11.ove");
    // Oldest entries were dropped
    QVERIFY(!pio.recentProjects().contains("/tmp/project_0.ove"));
    QVERIFY(!pio.recentProjects().contains("/tmp/project_1.ove"));
  }

  void recentProjectsNoDuplicateAtFront() {
    ProjectIO pio;
    pio.addRecentProject("/tmp/a.ove");
    QCOMPARE(pio.recentProjects().size(), 1);
    // Adding the same path again must not grow the list
    pio.addRecentProject("/tmp/a.ove");
    QCOMPARE(pio.recentProjects().size(), 1);
    QCOMPARE(pio.recentProjects().first(), "/tmp/a.ove");
  }

  void recentProjectsFileFilter() {
    ProjectIO pio;
    QVERIFY(pio.projectFileFilter().contains("(*.ove)"));
  }

  // --- Sequence edge cases ---

  void clearSequenceHistoryTest() {
    ProjectIO pio;
    amber::ActiveSequence = nullptr;
    auto seq1 = std::make_shared<Sequence>();
    auto seq2 = std::make_shared<Sequence>();
    pio.setSequence(seq1);
    pio.setSequence(seq2, true);
    QVERIFY(pio.canGoBack());
    pio.clearSequenceHistory();
    QVERIFY(!pio.canGoBack());
    pio.setSequence(nullptr);
  }

  void setSequenceNullClearsHistory() {
    ProjectIO pio;
    amber::ActiveSequence = nullptr;
    auto seq1 = std::make_shared<Sequence>();
    auto seq2 = std::make_shared<Sequence>();
    pio.setSequence(seq1);
    pio.setSequence(seq2, true);
    QVERIFY(pio.canGoBack());
    // setSequence(nullptr) must clear history
    pio.setSequence(nullptr);
    QVERIFY(!pio.canGoBack());
    QCOMPARE(amber::ActiveSequence, nullptr);
  }

  void goBackOnEmptyDoesNothing() {
    ProjectIO pio;
    amber::ActiveSequence = nullptr;
    auto seq = std::make_shared<Sequence>();
    pio.setSequence(seq);
    QVERIFY(!pio.canGoBack());
    // Must not crash
    pio.goBackSequence();
    // ActiveSequence unchanged — goBack on empty history is a no-op
    QCOMPARE(amber::ActiveSequence, seq);
    pio.setSequence(nullptr);
  }

 private:
  StubAppContext* stub_ctx_{nullptr};
};

QTEST_MAIN(TestProjectIO)
#include "test_projectio.moc"
