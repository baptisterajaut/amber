// Link stubs for test_undo_markers. undo_timeline.cpp also holds the ripple and sequence-edit commands, which reach
// into Clip, Media, the active sequence and OliveGlobal; the marker commands under test touch none of them, so these
// are only referenced, never called.

#include "engine/clip.h"
#include "engine/sequence.h"
#include "global/global.h"
#include "project/media.h"
#include "rendering/renderfunctions.h"

SequencePtr amber::ActiveSequence = nullptr;
std::unique_ptr<OliveGlobal> amber::Global;

Media* Clip::media() { return nullptr; }
void Clip::move(ComboAction*, long, long, long, int, bool, bool) {}
void Clip::refresh() {}
long Clip::timeline_in(bool) { return 0; }
void close_active_clips(Sequence*) {}
Footage* Media::to_footage() { return nullptr; }
void Media::update_tooltip(const QString&) {}
void OliveGlobal::set_sequence(SequencePtr, bool) {}
