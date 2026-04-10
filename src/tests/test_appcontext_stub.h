#ifndef TEST_APPCONTEXT_STUB_H
#define TEST_APPCONTEXT_STUB_H

#include "core/appcontext.h"

class TestAppContext : public AppContext {
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
  void clearGraphEditorIfRow(EffectRow*) override {}
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
  bool showQuestion(const QString&, const QString&) override { return false; }
  QString showSaveFileDialog(const QString&, const QString&) override { return {}; }
  QString showOpenFileDialog(const QString&, const QString&) override { return {}; }
  void clearViewerMedia() override {}
  QWidget* getMainWindow() override { return nullptr; }
};

#endif  // TEST_APPCONTEXT_STUB_H
