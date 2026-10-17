#ifndef CORE_HEADLESSAPPCONTEXT_H
#define CORE_HEADLESSAPPCONTEXT_H

#include "core/appcontext.h"

// AppContext for running the engine without a UI (tests, a future command-line tool). Install it with
// `amber::app_ctx = &ctx;` before touching engine code; the editor uses AppContextImpl instead.
//
// Policy: messages are logged on stderr (qInfo/qWarning); questions are logged and answered Yes (e.g. a project from
// another version keeps loading), but isInteractive() is false so a VST plugin path nobody trusted is never loaded;
// file dialogs return an empty path (= cancelled); UI refreshes are no-ops; the recent projects list is never touched.
class HeadlessAppContext : public AppContext {
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
  int showMessage(const QString& title, const QString& text, int type) override;
  bool showQuestion(const QString& title, const QString& text, bool warning, bool default_no) override;
  bool isInteractive() override { return false; }
  QString showSaveFileDialog(const QString&, const QString&) override { return {}; }
  QString showOpenFileDialog(const QString&, const QString&) override { return {}; }
  void clearViewerMedia() override {}
  QWindow* getMainWindowHandle() override { return nullptr; }
  void clearGraphEditorIfRow(EffectRow*) override {}
  void addRecentProject(const QString&) override {}
  void setMediaIcon(Media*, int) override {}
  void stopMediaThrobber(Media*) override {}
  QIcon mediaTypeIcon(int media_type) override;
};

#endif  // CORE_HEADLESSAPPCONTEXT_H
