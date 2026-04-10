// Stub implementations of UI-layer symbols referenced by amber-engine objects
// but not available in headless test binaries.
//
// These are never called by the tests — they exist solely to satisfy the
// linker when the amber-engine OBJECT library is linked into test targets.

#include <QDockWidget>
#include <QIcon>
#include <QScrollBar>
#include <memory>

// Include the debug dialog header early so ::DebugDialog type is known
// before the amber namespace block tries to use it.
#include "dialogs/debugdialog.h"
#include "engine/sequence.h"
#include "global/config.h"
#include "global/global.h"
#include "panels/project.h"
#include "project/media.h"
#include "project/previewgenerator.h"

// Forward declarations for panel types we only need as opaque pointers
class EffectControls;
class Viewer;
class Timeline;
class GraphEditor;
class UndoHistoryPanel;

// --- amber namespace globals ---

namespace amber {
std::unique_ptr<AmberGlobal> Global;
QString ActiveProjectFilename;
QString AppName;

namespace icon {
QIcon LeftArrow;
QIcon RightArrow;
QIcon UpArrow;
QIcon DownArrow;
QIcon Diamond;
QIcon Clock;
QIcon MediaVideo;
QIcon MediaAudio;
QIcon MediaImage;
QIcon MediaError;
QIcon MediaSequence;
QIcon MediaFolder;
QIcon ViewerGoToStart;
QIcon ViewerPrevFrame;
QIcon ViewerPlay;
QIcon ViewerPause;
QIcon ViewerNextFrame;
QIcon ViewerGoToEnd;

void Initialize() {}
}  // namespace icon

// amber::DebugDialog is a pointer variable with the same name as the class
// ::DebugDialog — use the global qualifier to reference the class type.
::DebugDialog* DebugDialog = nullptr;
}  // namespace amber

// --- Panel globals ---
Project* panel_project = nullptr;
EffectControls* panel_effect_controls = nullptr;
Viewer* panel_sequence_viewer = nullptr;
Viewer* panel_footage_viewer = nullptr;
Timeline* panel_timeline = nullptr;
GraphEditor* panel_graph_editor = nullptr;
UndoHistoryPanel* panel_undo_history = nullptr;

void update_ui(bool, bool) {}
QDockWidget* get_focused_panel(bool) { return nullptr; }
void alloc_panels(QWidget*) {}
void free_panels() {}
void scroll_to_frame_internal(QScrollBar*, long, double, int) {}

// --- AmberGlobal methods referenced from loadthread.cpp / undo_timeline.cpp ---
void AmberGlobal::set_sequence(SequencePtr, bool) {}
void AmberGlobal::update_project_filename(const QString&) {}
void AmberGlobal::set_modified(bool) {}

// --- Project methods referenced from loadthread.cpp ---
MediaPtr Project::create_folder_internal(QString) { return nullptr; }
MediaPtr Project::create_sequence_internal(ComboAction*, SequencePtr, bool, Media*) { return nullptr; }
void Project::add_recent_project(QString) {}

// --- PreviewGenerator methods referenced from footage.cpp / undo_media.cpp ---
void PreviewGenerator::AnalyzeMedia(Media*) {}
void PreviewGenerator::cancel() {}

// --- frame_to_timecode referenced from media.cpp ---
QString frame_to_timecode(long, int, double) { return {}; }
