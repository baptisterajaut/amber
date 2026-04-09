#ifndef UI_APPCONTEXTIMPL_H
#define UI_APPCONTEXTIMPL_H

#include "core/appcontext.h"

class AppContextImpl : public AppContext {
 public:
  void refreshViewer() override;
  void refreshEffectControls() override;
  void updateKeyframes() override;
  void clearEffectControls(bool clear_cache) override;
  void refreshTimeline() override;
  void deselectArea(long in, long out, int track) override;
  void seekPlayhead(long frame) override;
  void pausePlayback() override;
  void setGraphEditorRow(EffectRow* row) override;
  bool isPlaying() override;
  bool isEffectSelected(Effect* e) override;
  void setAudioMonitorValues(const QVector<double>& values) override;
  QVector<Media*> listAllSequences() override;
  void processFileList(QStringList& files, bool recursive, MediaPtr replace, Media* parent) override;
  bool isToolbarVisible() override;
  bool isModified() override;
  void setModified(bool modified) override;
  void updateUi(bool modified) override;
  int showMessage(const QString& title, const QString& text, int type) override;
  bool showQuestion(const QString& title, const QString& text) override;
};

#endif  // UI_APPCONTEXTIMPL_H
