#ifndef CORE_APPCONTEXT_H
#define CORE_APPCONTEXT_H

#include <QStringList>
#include <QVector>

#include <memory>

class EffectRow;
class Effect;
class Media;
class QIcon;
class QWindow;
using MediaPtr = std::shared_ptr<Media>;

// Project panel icon state of a footage item, set by the media probe (PreviewGenerator)
enum IconType { ICON_TYPE_VIDEO, ICON_TYPE_AUDIO, ICON_TYPE_IMAGE, ICON_TYPE_LOADING, ICON_TYPE_ERROR };

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
  // warning: warning icon over the main window, Yes by default (project load prompts)
  virtual bool showQuestion(const QString& title, const QString& text, bool warning = false) = 0;
  virtual QString showSaveFileDialog(const QString& title, const QString& filter) = 0;
  virtual QString showOpenFileDialog(const QString& title, const QString& filter) = 0;
  virtual void clearViewerMedia() = 0;
  // Native window of the main window (transient parent for engine-owned windows); null if none
  virtual QWindow* getMainWindowHandle() = 0;
  virtual void clearGraphEditorIfRow(EffectRow* row) = 0;
  // Record a project file the user opened in the recent projects list (persisted; headless code must not touch it)
  virtual void addRecentProject(const QString& filename) = 0;
  // Footage icon in the project panel (IconType; ICON_TYPE_LOADING starts the throbber). Any thread.
  virtual void setMediaIcon(Media* media, int icon_type) = 0;
  // Drop a footage item from the throbber animation (its probe was cancelled). GUI thread only.
  virtual void stopMediaThrobber(Media* media) = 0;
  // Icon shown for a folder or sequence item (MediaType); null icon for anything else
  virtual QIcon mediaTypeIcon(int media_type) = 0;
};

// Global app context -- set by UI layer at startup, used by engine code.
namespace amber {
extern AppContext* app_ctx;
}

#endif  // CORE_APPCONTEXT_H
