#ifndef CORE_APPCONTEXT_H
#define CORE_APPCONTEXT_H

#include <QStringList>
#include <QVector>

#include <memory>

class EffectRow;
class Effect;
class Media;
using MediaPtr = std::shared_ptr<Media>;

// Abstract interface decoupling engine from UI panels.
// UI layer provides a concrete implementation (AppContextImpl).
// Test code provides a stub/mock.
class AppContext {
 public:
  virtual ~AppContext() = default;

  virtual void refreshViewer() = 0;
  virtual void refreshEffectControls() = 0;
  virtual void updateKeyframes() = 0;
  virtual void clearEffectControls(bool clear_cache) = 0;
  virtual void refreshTimeline() = 0;
  virtual void deselectArea(long in, long out, int track) = 0;
  virtual void seekPlayhead(long frame) = 0;
  virtual void pausePlayback() = 0;
  virtual void setGraphEditorRow(EffectRow* row) = 0;
  virtual bool isPlaying() = 0;
  virtual bool isEffectSelected(Effect* e) = 0;
  virtual void setAudioMonitorValues(const QVector<double>& values) = 0;
  virtual QVector<Media*> listAllSequences() = 0;
  virtual void processFileList(QStringList& files, bool recursive, MediaPtr replace, Media* parent) = 0;
  virtual bool isToolbarVisible() = 0;
  virtual bool isModified() = 0;
  virtual void setModified(bool modified) = 0;
  virtual void updateUi(bool modified) = 0;
  virtual int showMessage(const QString& title, const QString& text, int type) = 0;
};

// Global app context -- set by UI layer at startup, used by engine code.
namespace amber {
extern AppContext* app_ctx;
}

#endif  // CORE_APPCONTEXT_H
