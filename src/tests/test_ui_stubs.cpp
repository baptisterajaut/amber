// Stub implementations of UI-layer symbols referenced by amber-engine objects
// but not available in headless test binaries.
//
// These are never called by the tests — they exist solely to satisfy the
// linker when the amber-engine OBJECT library is linked into test targets.

#include <QIcon>
#include <memory>

// Include the debug dialog header early so ::DebugDialog type is known
// before the amber namespace block tries to use it.
#include "dialogs/debugdialog.h"
#include "engine/sequence.h"
#include "global/global.h"
#include "panels/project.h"
#include "project/media.h"
#include "project/previewgenerator.h"

// --- amber namespace globals ---

namespace amber {
std::unique_ptr<AmberGlobal> Global;

namespace icon {
QIcon MediaSequence;
QIcon MediaFolder;
}  // namespace icon

// amber::DebugDialog is a pointer variable with the same name as the class
// ::DebugDialog — use the global qualifier to reference the class type.
::DebugDialog* DebugDialog = nullptr;
}  // namespace amber

// --- Panel globals ---
Project* panel_project = nullptr;

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
