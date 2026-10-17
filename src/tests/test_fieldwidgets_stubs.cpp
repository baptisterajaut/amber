// Link stubs for test_fieldwidgets. The real Effect, EffectRow, Clip and Cacher pull most of the app (rendering,
// panels, FFmpeg decoding), so only what the fields under test rely on is reproduced: an Effect holding its rows and
// its clip, a row holding its fields, a clip without a sequence (EffectField::Now() is then 0). Everything else is
// only referenced by the vtables, moc code and undo commands compiled in, and never called.

#include "effects/effect.h"
#include "effects/effectrow.h"
#include "effects/transition.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "global/global.h"
#include "panels/viewer.h"
#include "rendering/renderfunctions.h"

// What the fields use

Effect::Effect(Clip* c, const EffectMeta* em) : parent_clip(c), meta(em) {}
Effect::~Effect() = default;  // rows and fields are QObject children

void Effect::AddRow(EffectRow* row) {
  row->setParent(this);
  rows.append(row);
}

void Effect::FieldChanged() {}

EffectRow::EffectRow(Effect* parent, const QString& n, bool savable, bool keyframable)
    : QObject(parent), name_(n), keyframable_(keyframable), savable_(savable) {
  parent->AddRow(this);
}

void EffectRow::AddField(EffectField* field) {
  field->setParent(this);
  fields_.append(field);
}

Effect* EffectRow::GetParentEffect() { return static_cast<Effect*>(parent()); }
bool EffectRow::IsKeyframing() { return keyframing_; }

ClipSpeed::ClipSpeed() = default;
Clip::Clip(Sequence* s) : sequence(s), cacher(this) {}
Clip::~Clip() = default;

Cacher::Cacher(Clip* c) : clip(c) {}

// Never called

void Effect::refresh() {}
EffectPtr Effect::copy(Clip*) { return nullptr; }
void Effect::load(QXmlStreamReader&) {}
void Effect::custom_load(QXmlStreamReader&) {}
void Effect::save(QXmlStreamWriter&) {}
void Effect::startEffect() {}
void Effect::endEffect() {}
void Effect::process_image(double, uint8_t*, uint8_t*, int, int, int) {}
void Effect::process_shader(double, GLTextureCoords&, int, QByteArray&, QSize) {}
void Effect::process_coords(double, GLTextureCoords&, int) {}
QRhiTexture* Effect::process_superimpose(QRhi*, QRhiResourceUpdateBatch*, double) { return nullptr; }
void Effect::process_audio(double, double, quint8*, int, int) {}
void Effect::gizmo_draw(double, GLTextureCoords&) {}
bool Effect::AlwaysUpdate() { return false; }
void Effect::redraw(double) {}
void Effect::SetEnabled(bool) {}
void Effect::SetExpanded(bool) {}
void Effect::delete_self() {}
void Effect::move_up() {}
void Effect::move_down() {}
void Effect::save_to_file() {}
void Effect::load_from_file() {}
bool Effect::IsEnabled() { return true; }
void Effect::close() {}
void Effect::load_from_string(const QByteArray&) {}
QByteArray Effect::save_to_string() { return {}; }
EffectPtr Effect::Create(Clip*, const EffectMeta*) { return nullptr; }

void EffectRow::SetKeyframingInternal(bool) {}
void EffectRow::SetKeyframingEnabled(bool) {}
void EffectRow::GoToPreviousKeyframe() {}
void EffectRow::ToggleKeyframe() {}
void EffectRow::GoToNextKeyframe() {}
void EffectRow::FocusRow() {}

TransitionPtr Transition::CreateFromMeta(Clip*, Clip*, const EffectMeta*) { return nullptr; }
Clip* Transition::get_opened_clip() { return nullptr; }
Clip* Transition::get_closed_clip() { return nullptr; }
int Transition::get_true_length() { return 0; }
void Transition::set_length(int) {}

int Clip::IndexOfEffect(Effect*) { return -1; }
void Cacher::run() {}

SequencePtr amber::ActiveSequence = nullptr;
long playhead_to_clip_frame(Clip*, long) { return 0; }
void close_active_clips(Sequence*) {}
long timecode_to_frame(const QString&, int, double) { return 0; }
QString frame_to_timecode(long, int, double) { return {}; }
void OliveGlobal::SetNativeStyling(QWidget*) {}
