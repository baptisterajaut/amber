/***

    Olive - Non-Linear Video Editor
    Copyright (C) 2019  Olive Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "effect.h"

#include <rhi/qshaderbaker.h>
#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QGridLayout>
#include <QMenu>
#include <QPainter>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtMath>

#include "core/appcontext.h"
#include "core/math.h"
#include "core/path.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "global/config.h"
#include "global/debug.h"
#include "project/clipboard.h"
#include "rendering/renderthread.h"
#include "transition.h"

#include "effects/internal/audionoiseeffect.h"
#include "effects/internal/cornerpineffect.h"
#include "effects/internal/fillleftrighteffect.h"
#include "effects/internal/frei0reffect.h"
#include "effects/internal/paneffect.h"
#include "effects/internal/richtexteffect.h"
#include "effects/internal/shakeeffect.h"
#include "effects/internal/solideffect.h"
#include "effects/internal/subtitleeffect.h"
#include "effects/internal/texteffect.h"
#include "effects/internal/timecodeeffect.h"
#include "effects/internal/toneeffect.h"
#include "effects/internal/transformeffect.h"
#include "effects/internal/volumeeffect.h"
#include "effects/internal/vsthost.h"

QVector<EffectMeta> effects;

namespace {

struct FieldTypeEntry {
  const char* name;
  int type;
};
constexpr FieldTypeEntry kFieldTypes[] = {
    {"DOUBLE", EffectField::EFFECT_FIELD_DOUBLE}, {"BOOL", EffectField::EFFECT_FIELD_BOOL},
    {"COLOR", EffectField::EFFECT_FIELD_COLOR},   {"COMBO", EffectField::EFFECT_FIELD_COMBO},
    {"FONT", EffectField::EFFECT_FIELD_FONT},     {"STRING", EffectField::EFFECT_FIELD_STRING},
    {"FILE", EffectField::EFFECT_FIELD_FILE},
};

int FieldTypeFromString(const QString& type_str) {
  for (const auto& e : kFieldTypes) {
    if (type_str == QLatin1String(e.name)) return e.type;
  }
  return -1;
}

// Find the field index in `row` matching the "id" attribute in `stream`.
// Returns -1 if not found.
int FindFieldByStreamId(QXmlStreamReader& stream, EffectRow* row) {
  for (const auto& attr : stream.attributes()) {
    if (attr.name() == QLatin1String("id")) {
      for (int l = 0; l < row->FieldCount(); l++) {
        if (row->Field(l)->id() == attr.value()) return l;
      }
      break;
    }
  }
  return -1;
}

// Apply the "value" attribute from `stream` to `field`'s persistent data.
void ApplyFieldValue(QXmlStreamReader& stream, EffectField* field) {
  for (const auto& attr : stream.attributes()) {
    if (attr.name() == QLatin1String("value")) {
      field->persistent_data_ = field->ConvertStringToValue(attr.value().toString());
      break;
    }
  }
}

// Parse a single <key> element from `stream` into an EffectKeyframe.
EffectKeyframe ParseKeyframe(QXmlStreamReader& stream, EffectField* field) {
  EffectKeyframe key;
  for (const auto& attr : stream.attributes()) {
    if (attr.name() == QLatin1String("value")) {
      key.data = field->ConvertStringToValue(attr.value().toString());
    } else if (attr.name() == QLatin1String("frame")) {
      key.time = attr.value().toLong();
    } else if (attr.name() == QLatin1String("type")) {
      key.type = attr.value().toInt();
    } else if (attr.name() == QLatin1String("prehx")) {
      key.pre_handle_x = attr.value().toDouble();
    } else if (attr.name() == QLatin1String("prehy")) {
      key.pre_handle_y = attr.value().toDouble();
    } else if (attr.name() == QLatin1String("posthx")) {
      key.post_handle_x = attr.value().toDouble();
    } else if (attr.name() == QLatin1String("posthy")) {
      key.post_handle_y = attr.value().toDouble();
    }
  }
  return key;
}

// Load a single <field> element (including nested <key> children) from `stream`.
void LoadFieldElement(QXmlStreamReader& stream, EffectRow* row) {
  int field_number = FindFieldByStreamId(stream, row);
  if (field_number < 0) return;

  EffectField* field = row->Field(field_number);
  ApplyFieldValue(stream, field);

  while (!stream.atEnd() && !(stream.name() == QLatin1String("field") && stream.isEndElement())) {
    stream.readNext();
    if (stream.name() == QLatin1String("key") && stream.isStartElement()) {
      row->SetKeyframingInternal(true);
      field->keyframes.append(ParseKeyframe(stream, field));
    }
  }

  field->Changed();
}

// Load a single <row> element (including nested <field> children) from `stream`.
void LoadRowElement(QXmlStreamReader& stream, EffectRow* row) {
  while (!stream.atEnd() && !(stream.name() == QLatin1String("row") && stream.isEndElement())) {
    stream.readNext();
    if (stream.name() == QLatin1String("field") && stream.isStartElement()) {
      LoadFieldElement(stream, row);
    }
  }
}

// Write a single field UBO value for `process_shader`.
void WriteFieldUboValue(EffectField* field, double timecode, const UniformEntry& entry, QByteArray& uboData) {
  switch (field->type()) {
    case EffectField::EFFECT_FIELD_DOUBLE: {
      float v = float(static_cast<DoubleField*>(field)->GetDoubleAt(timecode));
      memcpy(uboData.data() + entry.offset, &v, 4);
    } break;
    case EffectField::EFFECT_FIELD_COLOR: {
      ColorField* cf = static_cast<ColorField*>(field);
      float rgb[3] = {float(cf->GetColorAt(timecode).redF()), float(cf->GetColorAt(timecode).greenF()),
                      float(cf->GetColorAt(timecode).blueF())};
      memcpy(uboData.data() + entry.offset, rgb, qMin(entry.size, 12));
    } break;
    case EffectField::EFFECT_FIELD_BOOL: {
      int v = field->GetValueAt(timecode).toBool() ? 1 : 0;
      memcpy(uboData.data() + entry.offset, &v, 4);
    } break;
    case EffectField::EFFECT_FIELD_COMBO: {
      int v = field->GetValueAt(timecode).toInt();
      memcpy(uboData.data() + entry.offset, &v, 4);
    } break;
    default:
      break;
  }
}

}  // namespace

static EffectPtr CreateInternalEffect(Clip* c, const EffectMeta* em) {
  switch (em->internal) {
    case EFFECT_INTERNAL_TRANSFORM:
      return std::make_shared<TransformEffect>(c, em);
    case EFFECT_INTERNAL_TEXT:
      return std::make_shared<TextEffect>(c, em);
    case EFFECT_INTERNAL_TIMECODE:
      return std::make_shared<TimecodeEffect>(c, em);
    case EFFECT_INTERNAL_SOLID:
      return std::make_shared<SolidEffect>(c, em);
    case EFFECT_INTERNAL_NOISE:
      return std::make_shared<AudioNoiseEffect>(c, em);
    case EFFECT_INTERNAL_VOLUME:
      return std::make_shared<VolumeEffect>(c, em);
    case EFFECT_INTERNAL_PAN:
      return std::make_shared<PanEffect>(c, em);
    case EFFECT_INTERNAL_TONE:
      return std::make_shared<ToneEffect>(c, em);
    case EFFECT_INTERNAL_SHAKE:
      return std::make_shared<ShakeEffect>(c, em);
    case EFFECT_INTERNAL_CORNERPIN:
      return std::make_shared<CornerPinEffect>(c, em);
    case EFFECT_INTERNAL_FILLLEFTRIGHT:
      return std::make_shared<FillLeftRightEffect>(c, em);
    case EFFECT_INTERNAL_VST:
      return std::make_shared<VSTHost>(c, em);
#ifndef NOFREI0R
    case EFFECT_INTERNAL_FREI0R:
      return std::make_shared<Frei0rEffect>(c, em);
#endif
    case EFFECT_INTERNAL_RICHTEXT:
      return std::make_shared<RichTextEffect>(c, em);
    case EFFECT_INTERNAL_SUBTITLE:
      return std::make_shared<SubtitleEffect>(c, em);
    default:
      return nullptr;
  }
}

EffectPtr Effect::Create(Clip* c, const EffectMeta* em) {
  if (em->internal >= 0 && em->internal < EFFECT_INTERNAL_COUNT) {
    return CreateInternalEffect(c, em);
  }
  if (!em->filename.isEmpty()) {
    return std::make_shared<Effect>(c, em);
  }
  qCritical() << "Invalid effect data";
  amber::app_ctx->showMessage(
      QCoreApplication::translate("Effect", "Invalid effect"),
      QCoreApplication::translate(
          "Effect", "No candidate for effect '%1'. This effect may be corrupt. Try reinstalling it or Amber.")
          .arg(em->name),
      3);
  return nullptr;
}

const EffectMeta* Effect::GetInternalMeta(int internal_id, int type) {
  for (const auto& effect : effects) {
    if (effect.internal == internal_id && effect.type == type) {
      return &effect;
    }
  }
  return nullptr;
}

Effect::Effect(Clip* c, const EffectMeta* em)
    : parent_clip(c),
      meta(em)

{
  if (em != nullptr) {
    name = em->name;

    if (!em->filename.isEmpty() && em->internal == -1) {
      parseEffectXml();
    }
  }
}

static EffectField* ParseDoubleField(EffectRow* row, const QString& id, const QXmlStreamAttributes& attrs) {
  DoubleField* f = new DoubleField(row, id);
  for (const auto& attr : attrs) {
    if (attr.name() == QLatin1String("default")) {
      f->SetDefault(attr.value().toDouble());
    } else if (attr.name() == QLatin1String("min")) {
      f->SetMinimum(attr.value().toDouble());
    } else if (attr.name() == QLatin1String("max")) {
      f->SetMaximum(attr.value().toDouble());
    }
  }
  return f;
}

static EffectField* ParseColorField(EffectRow* row, const QString& id, const QXmlStreamAttributes& attrs) {
  QColor color;
  EffectField* f = new ColorField(row, id);
  for (const auto& attr : attrs) {
    if (attr.name() == QLatin1String("r")) {
      color.setRed(attr.value().toInt());
    } else if (attr.name() == QLatin1String("g")) {
      color.setGreen(attr.value().toInt());
    } else if (attr.name() == QLatin1String("b")) {
      color.setBlue(attr.value().toInt());
    } else if (attr.name() == QLatin1String("rf")) {
      color.setRedF(attr.value().toDouble());
    } else if (attr.name() == QLatin1String("gf")) {
      color.setGreenF(attr.value().toDouble());
    } else if (attr.name() == QLatin1String("bf")) {
      color.setBlueF(attr.value().toDouble());
    } else if (attr.name() == QLatin1String("hex")) {
      color = QColor::fromString(attr.value().toString());
    }
  }
  f->SetValueAt(0, color);
  f->SetDefaultData(color);
  return f;
}

static EffectField* ParseStringDefaultField(EffectRow* row, const QString& id, const QXmlStreamAttributes& attrs,
                                            const char* default_attr_name, bool is_file) {
  EffectField* f =
      is_file ? static_cast<EffectField*>(new FileField(row, id)) : static_cast<EffectField*>(new StringField(row, id));
  for (const auto& attr : attrs) {
    if (attr.name() == QLatin1String(default_attr_name)) {
      f->SetValueAt(0, attr.value().toString());
      f->SetDefaultData(attr.value().toString());
      break;
    }
  }
  return f;
}

static EffectField* ParseBoolField(EffectRow* row, const QString& id, const QXmlStreamAttributes& attrs) {
  EffectField* f = new BoolField(row, id);
  for (const auto& attr : attrs) {
    if (attr.name() == QLatin1String("default")) {
      bool v = (attr.value() == QLatin1String("1"));
      f->SetValueAt(0, v);
      f->SetDefaultData(v);
      break;
    }
  }
  return f;
}

static EffectField* ParseComboField(EffectRow* row, const QString& id, const QXmlStreamAttributes& attrs,
                                    QXmlStreamReader& reader) {
  ComboField* f = new ComboField(row, id);
  int combo_default_index = 0;
  for (const auto& attr : attrs) {
    if (attr.name() == QLatin1String("default")) {
      combo_default_index = attr.value().toInt();
      break;
    }
  }
  int combo_item_count = 0;
  while (!reader.atEnd() && !(reader.name() == QLatin1String("field") && reader.isEndElement())) {
    reader.readNext();
    if (reader.name() == QLatin1String("option") && reader.isStartElement()) {
      reader.readNext();
      f->AddItem(reader.text().toString(), combo_item_count++);
    }
  }
  f->SetValueAt(0, combo_default_index);
  f->SetDefaultData(combo_default_index);
  return f;
}

void Effect::parseFieldElement(QXmlStreamReader& reader, EffectRow* row) {
  if (!row) {
    qWarning() << "parseFieldElement: row is null";
    return;
  }
  int type = -1;
  QString id;

  const QXmlStreamAttributes& attributes = reader.attributes();
  for (const auto& attr : attributes) {
    if (attr.name() == QLatin1String("type")) {
      type = FieldTypeFromString(attr.value().toString().toUpper());
    } else if (attr.name() == QLatin1String("id")) {
      id = attr.value().toString();
    }
  }

  if (id.isEmpty()) {
    qCritical() << "Couldn't load field from" << meta->filename << "- ID cannot be empty.";
    return;
  }

  switch (type) {
    case EffectField::EFFECT_FIELD_DOUBLE:
      ParseDoubleField(row, id, attributes);
      break;
    case EffectField::EFFECT_FIELD_COLOR:
      ParseColorField(row, id, attributes);
      break;
    case EffectField::EFFECT_FIELD_STRING:
      ParseStringDefaultField(row, id, attributes, "default", false);
      break;
    case EffectField::EFFECT_FIELD_BOOL:
      ParseBoolField(row, id, attributes);
      break;
    case EffectField::EFFECT_FIELD_COMBO:
      ParseComboField(row, id, attributes, reader);
      break;
    case EffectField::EFFECT_FIELD_FONT:
      ParseStringDefaultField(row, id, attributes, "default", false);
      break;
    case EffectField::EFFECT_FIELD_FILE:
      ParseStringDefaultField(row, id, attributes, "filename", true);
      break;
  }
}

void Effect::parseRowElement(QXmlStreamReader& reader) {
  QString row_name;
  for (const auto& attr : reader.attributes()) {
    if (attr.name() == QLatin1String("name")) {
      row_name = attr.value().toString();
      break;
    }
  }
  if (row_name.isEmpty()) return;

  EffectRow* row = new EffectRow(this, row_name);
  while (!reader.atEnd() && !(reader.name() == QLatin1String("row") && reader.isEndElement())) {
    reader.readNext();
    if (reader.name() == QLatin1String("field") && reader.isStartElement()) {
      parseFieldElement(reader, row);
    }
  }
}

void Effect::parseShaderElement(QXmlStreamReader& reader) {
  SetFlags(Flags() | ShaderFlag);
  for (const auto& attr : reader.attributes()) {
    if (attr.name() == QLatin1String("vert")) {
      vertPath = attr.value().toString();
    } else if (attr.name() == QLatin1String("frag")) {
      fragPath = attr.value().toString();
    } else if (attr.name() == QLatin1String("iterations")) {
      setIterations(attr.value().toInt());
    }
  }
}

void Effect::parseEffectXml() {
  QFile effect_file(meta->filename);
  if (!effect_file.open(QFile::ReadOnly)) {
    qCritical() << "Failed to open effect file" << meta->filename;
    return;
  }

  QXmlStreamReader reader(&effect_file);

  while (!reader.atEnd()) {
    if (reader.name() == QLatin1String("row") && reader.isStartElement()) {
      parseRowElement(reader);
    } else if (reader.name() == QLatin1String("shader") && reader.isStartElement()) {
      parseShaderElement(reader);
    }
    reader.readNext();
  }

  effect_file.close();
}

Effect::~Effect() {
  if (isOpen) {
    close();
  }

  // Clear graph editor if it's using one of these rows.
  // Guard: during static destruction (exit()), app_ctx may already be null.
  if (amber::app_ctx) {
    for (int i = 0; i < row_count(); i++) {
      amber::app_ctx->clearGraphEditorIfRow(row(i));
    }
  }

  for (auto gizmo_dragging_action : gizmo_dragging_actions_) {
    delete gizmo_dragging_action;
  }
}

void Effect::AddRow(EffectRow* row) {
  row->setParent(this);
  rows.append(row);
}

void Effect::copy_field_keyframes(EffectPtr e) {
  int row_count = qMin(rows.size(), e->rows.size());
  for (int i = 0; i < row_count; i++) {
    EffectRow* row = rows.at(i);
    EffectRow* copy_row = e->rows.at(i);
    copy_row->SetKeyframingInternal(row->IsKeyframing());
    int field_count = qMin(row->FieldCount(), copy_row->FieldCount());
    for (int j = 0; j < field_count; j++) {
      // Get field from this (the source) effect
      EffectField* field = row->Field(j);

      // Get field from the destination effect
      EffectField* copy_field = copy_row->Field(j);

      // Copy keyframes between effects
      copy_field->keyframes = field->keyframes;

      // Copy persistet data between effects
      copy_field->persistent_data_ = field->persistent_data_;
    }
  }
}

EffectRow* Effect::row(int i) { return rows.at(i); }

int Effect::row_count() { return rows.size(); }

EffectGizmo* Effect::add_gizmo(int type) {
  EffectGizmo* gizmo = new EffectGizmo(this, type);
  gizmos.append(gizmo);
  return gizmo;
}

EffectGizmo* Effect::gizmo(int i) { return gizmos.at(i); }

int Effect::gizmo_count() { return gizmos.size(); }

void Effect::refresh() {}

void Effect::FieldChanged() { amber::app_ctx->updateUi(false); }

void Effect::delete_self() {
  auto* cmd = new EffectDeleteCommand(this);
  cmd->setText(tr("Delete Effect"));
  amber::UndoStack.push(cmd);
  amber::app_ctx->updateUi(true);
}

void Effect::move_up() {
  int index_of_effect = parent_clip->IndexOfEffect(this);
  if (index_of_effect == 0) {
    return;
  }

  MoveEffectCommand* command = new MoveEffectCommand();
  command->setText(tr("Move Effect Up"));
  command->clip = parent_clip;
  command->from = index_of_effect;
  command->to = command->from - 1;
  amber::UndoStack.push(command);
  amber::app_ctx->refreshEffectControls();
  amber::app_ctx->refreshViewer();
}

void Effect::move_down() {
  int index_of_effect = parent_clip->IndexOfEffect(this);
  if (index_of_effect == parent_clip->effects.size() - 1) {
    return;
  }

  MoveEffectCommand* command = new MoveEffectCommand();
  command->setText(tr("Move Effect Down"));
  command->clip = parent_clip;
  command->from = index_of_effect;
  command->to = command->from + 1;
  amber::UndoStack.push(command);
  amber::app_ctx->refreshEffectControls();
  amber::app_ctx->refreshViewer();
}

void Effect::save_to_file() {
  // save effect settings to file
  QString file =
      amber::app_ctx->showSaveFileDialog(tr("Save Effect Settings"), tr("Effect XML Settings %1").arg("(*.xml)"));

  // if the user picked a file
  if (!file.isEmpty()) {
    // ensure file ends with .xml extension
    if (!file.endsWith(".xml", Qt::CaseInsensitive)) {
      file.append(".xml");
    }

    QFile file_handle(file);
    if (file_handle.open(QFile::WriteOnly)) {
      file_handle.write(save_to_string());

      file_handle.close();
    } else {
      amber::app_ctx->showMessage(tr("Save Settings Failed"), tr("Failed to open \"%1\" for writing.").arg(file), 3);
    }
  }
}

void Effect::load_from_file() {
  // load effect settings from file
  QString file =
      amber::app_ctx->showOpenFileDialog(tr("Load Effect Settings"), tr("Effect XML Settings %1").arg("(*.xml)"));

  // if the user picked a file
  if (!file.isEmpty()) {
    QFile file_handle(file);
    if (file_handle.open(QFile::ReadOnly)) {
      auto* cmd = new SetEffectData(this, file_handle.readAll());
      cmd->setText(tr("Load Effect Settings"));
      amber::UndoStack.push(cmd);

      file_handle.close();

      amber::app_ctx->updateUi(false);
    } else {
      amber::app_ctx->showMessage(tr("Load Settings Failed"), tr("Failed to open \"%1\" for reading.").arg(file), 3);
    }
  }
}

bool Effect::AlwaysUpdate() { return false; }

bool Effect::IsEnabled() { return enabled_; }

bool Effect::IsExpanded() { return expanded_; }

void Effect::SetExpanded(bool e) { expanded_ = e; }

void Effect::SetEnabled(bool b) {
  enabled_ = b;
  emit EnabledChanged(b);
}

void Effect::load(QXmlStreamReader& stream) {
  int row_count = 0;

  QString tag = stream.name().toString();

  while (!stream.atEnd() && !(stream.name() == tag && stream.isEndElement())) {
    stream.readNext();
    if (stream.name() == QLatin1String("row") && stream.isStartElement()) {
      if (row_count < rows.size()) {
        LoadRowElement(stream, rows.at(row_count));
      } else {
        qCritical() << "Too many rows for effect" << id << ". Project might be corrupt. (Got" << row_count
                    << ", expected <" << rows.size() - 1 << ")";
      }
      row_count++;
    } else if (stream.isStartElement()) {
      custom_load(stream);
    }
  }
}

void Effect::custom_load(QXmlStreamReader&) {}

void Effect::save(QXmlStreamWriter& stream) {
  stream.writeAttribute("name", meta->category + "/" + meta->name);
  stream.writeAttribute("enabled", QString::number(IsEnabled()));

  for (auto row : rows) {
    if (row->IsSavable()) {
      stream.writeStartElement("row");  // row
      for (int j = 0; j < row->FieldCount(); j++) {
        EffectField* field = row->Field(j);

        if (!field->id().isEmpty()) {
          stream.writeStartElement("field");  // field
          stream.writeAttribute("id", field->id());
          stream.writeAttribute("value", field->ConvertValueToString(field->persistent_data_));
          for (int k = 0; k < field->keyframes.size(); k++) {
            const EffectKeyframe& key = field->keyframes.at(k);
            stream.writeStartElement("key");
            stream.writeAttribute("value", field->ConvertValueToString(key.data));
            stream.writeAttribute("frame", QString::number(key.time));
            stream.writeAttribute("type", QString::number(key.type));
            stream.writeAttribute("prehx", QString::number(key.pre_handle_x));
            stream.writeAttribute("prehy", QString::number(key.pre_handle_y));
            stream.writeAttribute("posthx", QString::number(key.post_handle_x));
            stream.writeAttribute("posthy", QString::number(key.post_handle_y));
            stream.writeEndElement();  // key
          }
          stream.writeEndElement();  // field
        }
      }
      stream.writeEndElement();  // row
    }
  }
}

void Effect::load_from_string(const QByteArray& s) {
  // clear existing keyframe data
  for (auto row : rows) {
    row->SetKeyframingInternal(false);
    for (int j = 0; j < row->FieldCount(); j++) {
      EffectField* field = row->Field(j);
      field->keyframes.clear();
    }
  }

  // write settings with xml writer
  QXmlStreamReader stream(s);

  while (!stream.atEnd()) {
    stream.readNext();

    // find the effect opening tag
    if (stream.name() == QLatin1String("effect") && stream.isStartElement()) {
      // check the name to see if it matches this effect
      const QXmlStreamAttributes& attributes = stream.attributes();
      for (const auto& attr : attributes) {
        if (attr.name() == QLatin1String("name")) {
          if (get_meta_from_name(attr.value().toString()) == meta) {
            // pass off to standard loading function
            load(stream);
          } else {
            amber::app_ctx->showMessage(tr("Load Settings Failed"), tr("This settings file doesn't match this effect."),
                                        3);
          }
          break;
        }
      }

      // we've found what we're looking for
      break;
    }
  }
}

QByteArray Effect::save_to_string() {
  QByteArray save_data;

  // write settings to string with xml writer
  QXmlStreamWriter stream(&save_data);

  stream.writeStartDocument();

  stream.writeStartElement("effect");

  // pass off to standard saving function
  save(stream);

  stream.writeEndElement();  // effect

  stream.writeEndDocument();

  return save_data;
}

bool Effect::is_open() { return isOpen; }

void Effect::validate_meta_path() {
  if (!meta->path.isEmpty() || (vertPath.isEmpty() && fragPath.isEmpty())) return;
  QList<QString> effects_paths = get_effects_paths();
  const QString& test_fn = vertPath.isEmpty() ? fragPath : vertPath;
  for (const auto& effects_path : effects_paths) {
    if (QFileInfo::exists(effects_path + "/" + test_fn)) {
      for (auto& effect : effects) {
        if (&effect == meta) {
          effect.path = effects_path;
          return;
        }
      }
      return;
    }
  }
}

QShader Effect::bakeOrLoadCached(const QString& path, QShader::Stage stage) {
  QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/shaders";
  QDir().mkpath(cacheDir);

  QFile src(path);
  if (!src.open(QIODevice::ReadOnly)) {
    qWarning() << "Failed to open shader source:" << path;
    return {};
  }
  QByteArray source = src.readAll();

  QByteArray hashBytes = QCryptographicHash::hash(source, QCryptographicHash::Sha1);
  QString hash = hashBytes.toHex();
  QString cachePath = cacheDir + "/" + hash + ".qsb";

  // Try loading from cache
  QFile cacheFile(cachePath);
  if (cacheFile.open(QIODevice::ReadOnly)) {
    QShader cached = QShader::fromSerialized(cacheFile.readAll());
    if (cached.isValid()) return cached;
  }

  // Bake from source
  QShaderBaker baker;
  baker.setSourceString(source, stage);
  baker.setGeneratedShaderVariants({QShader::StandardShader});
  baker.setGeneratedShaders({
      {QShader::SpirvShader, QShaderVersion(100)},
      {QShader::GlslShader, QShaderVersion(150)},
      {QShader::HlslShader, QShaderVersion(50)},
      {QShader::MslShader, QShaderVersion(12)},
  });
  QShader result = baker.bake();
  if (!result.isValid()) {
    qWarning() << "Shader bake failed:" << path << baker.errorMessage();
    return {};
  }

  // Write to cache
  QFile writeFile(cachePath);
  if (writeFile.open(QIODevice::WriteOnly)) {
    writeFile.write(result.serialized());
  }
  return result;
}

void Effect::open() {
  if (isOpen) {
    qWarning() << "Tried to open an effect that was already open";
    close();
  }
  if (amber::CurrentRuntimeConfig.shaders_are_enabled && (Flags() & ShaderFlag)) {
    validate_meta_path();
    if (!vertPath.isEmpty()) {
      vertexShader_ = bakeOrLoadCached(meta->path + "/" + vertPath, QShader::VertexStage);
      if (vertexShader_.isValid()) {
        QShaderDescription desc = vertexShader_.description();
        for (const auto& block : desc.uniformBlocks()) {
          if (block.binding == 1) {
            vertUboSize_ = block.size;
            for (const auto& member : block.members) {
              vertUniformEntries_.append({member.name, member.offset, int(member.size)});
            }
            break;
          }
        }
      }
    }
    if (!fragPath.isEmpty()) {
      fragmentShader_ = bakeOrLoadCached(meta->path + "/" + fragPath, QShader::FragmentStage);
      if (fragmentShader_.isValid()) {
        QShaderDescription desc = fragmentShader_.description();
        for (const auto& block : desc.uniformBlocks()) {
          if (block.binding == 1) {
            fragUboSize_ = block.size;
            for (const auto& member : block.members) {
              uniformEntries_.append({member.name, member.offset, int(member.size)});
            }
            break;
          }
        }
      }
    }
  }
  isOpen = true;
}

void Effect::close() {
  if (!isOpen) {
    qWarning() << "Tried to close an effect that was already closed";
  }
  delete_texture();
  vertexShader_ = {};
  fragmentShader_ = {};
  uniformEntries_.clear();
  vertUniformEntries_.clear();
  fragUboSize_ = 0;
  vertUboSize_ = 0;
  isOpen = false;
}

bool Effect::is_glsl_linked() { return is_shader_valid(); }

bool Effect::is_shader_valid() { return vertexShader_.isValid() && fragmentShader_.isValid(); }

void Effect::startEffect() {
  if (!isOpen) {
    open();
    qWarning() << "Tried to start a closed effect - opening";
  }
}

void Effect::endEffect() {}

int Effect::Flags() { return flags_; }

void Effect::SetFlags(int flags) { flags_ = flags; }

int Effect::getIterations() { return iterations; }

void Effect::setIterations(int i) { iterations = i; }

void Effect::process_image(double, uint8_t*, uint8_t*, int) {}

EffectPtr Effect::copy(Clip* c) {
  EffectPtr copy = Effect::Create(c, meta);
  copy->SetEnabled(IsEnabled());
  copy_field_keyframes(copy);
  return copy;
}

void Effect::process_shader(double timecode, GLTextureCoords&, int iteration, QByteArray& uboData, QSize renderSize) {
  if (uboData.size() < fragUboSize_) uboData.resize(fragUboSize_);

  // Use actual FBO/render size for resolution uniforms (shaders that use gl_FragCoord need this
  // to match the framebuffer dimensions, not the source media dimensions).
  float resW = renderSize.isValid() ? float(renderSize.width()) : float(parent_clip->media_width());
  float resH = renderSize.isValid() ? float(renderSize.height()) : float(parent_clip->media_height());

  // Set automatic uniforms by looking up their entries
  for (const auto& entry : uniformEntries_) {
    if (entry.name == QLatin1String("resolution")) {
      float res[2] = {resW, resH};
      memcpy(uboData.data() + entry.offset, res, qMin(entry.size, 8));
    } else if (entry.name == QLatin1String("resolution_x")) {
      memcpy(uboData.data() + entry.offset, &resW, 4);
    } else if (entry.name == QLatin1String("resolution_y")) {
      memcpy(uboData.data() + entry.offset, &resH, 4);
    } else if (entry.name == QLatin1String("time")) {
      float v = float(timecode);
      memcpy(uboData.data() + entry.offset, &v, 4);
    } else if (entry.name == QLatin1String("iteration")) {
      int v = iteration;
      memcpy(uboData.data() + entry.offset, &v, 4);
    }
  }

  // Set field uniforms by name lookup
  for (int r = 0; r < row_count(); r++) {
    EffectRow* erow = row(r);
    for (int j = 0; j < erow->FieldCount(); j++) {
      EffectField* field = erow->Field(j);
      if (field->id().isEmpty()) continue;
      for (const auto& entry : uniformEntries_) {
        if (entry.name == field->id()) {
          WriteFieldUboValue(field, timecode, entry, uboData);
          break;  // found matching entry
        }
      }
    }
  }
}

void Effect::process_coords(double, GLTextureCoords&, int) {}

QRhiTexture* Effect::process_superimpose(QRhi* rhi, QRhiResourceUpdateBatch* u, double timecode) {
  bool dimensions_changed = false;
  bool redrew_image = false;

  int width = parent_clip->media_width();
  int height = parent_clip->media_height();

  if (width != img.width() || height != img.height()) {
    img = QImage(width, height, QImage::Format_RGBA8888_Premultiplied);
    dimensions_changed = true;
  }

  if (valueHasChanged(timecode) || dimensions_changed || AlwaysUpdate()) {
    redraw(timecode);
    redrew_image = true;
  }

  if (superimposeTex_ == nullptr || dimensions_changed) {
    RenderThread::DeferRhiResourceDeletion(superimposeTex_);
    superimposeTex_ = rhi->newTexture(QRhiTexture::RGBA8, QSize(width, height));
    superimposeTex_->create();
    redrew_image = true;
  }

  if (redrew_image) {
    QRhiTextureSubresourceUploadDescription desc(img.constBits(), int(img.sizeInBytes()));
    desc.setSourceSize(QSize(width, height));
    u->uploadTexture(superimposeTex_, QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, desc)}));
  }

  return superimposeTex_;
}

void Effect::process_audio(double, double, quint8*, int, int) {}

void Effect::gizmo_draw(double, GLTextureCoords&) {}

void Effect::gizmo_move(EffectGizmo* gizmo, int x_movement, int y_movement, double timecode, bool done) {
  // Verify gizmo belongs to this effect
  if (!gizmos.contains(gizmo)) return;

  // On drag start, record undo snapshots for each bound field
  if (!done && gizmo_dragging_actions_.isEmpty()) {
    if (gizmo->x_field1 != nullptr) gizmo_dragging_actions_.append(new KeyframeDataChange(gizmo->x_field1));
    if (gizmo->y_field1 != nullptr) gizmo_dragging_actions_.append(new KeyframeDataChange(gizmo->y_field1));
    if (gizmo->x_field2 != nullptr) gizmo_dragging_actions_.append(new KeyframeDataChange(gizmo->x_field2));
    if (gizmo->y_field2 != nullptr) gizmo_dragging_actions_.append(new KeyframeDataChange(gizmo->y_field2));
  }

  // Update field values
  if (gizmo->x_field1 != nullptr)
    gizmo->x_field1->SetValueAt(timecode, gizmo->x_field1->GetDoubleAt(timecode) + x_movement * gizmo->x_field_multi1);
  if (gizmo->y_field1 != nullptr)
    gizmo->y_field1->SetValueAt(timecode, gizmo->y_field1->GetDoubleAt(timecode) + y_movement * gizmo->y_field_multi1);
  if (gizmo->x_field2 != nullptr)
    gizmo->x_field2->SetValueAt(timecode, gizmo->x_field2->GetDoubleAt(timecode) + x_movement * gizmo->x_field_multi2);
  if (gizmo->y_field2 != nullptr)
    gizmo->y_field2->SetValueAt(timecode, gizmo->y_field2->GetDoubleAt(timecode) + y_movement * gizmo->y_field_multi2);

  // On drag end, push the undo action
  if (done && !gizmo_dragging_actions_.isEmpty()) {
    ComboAction* ca = new ComboAction(tr("Move Gizmo"));
    for (auto gizmo_dragging_action : gizmo_dragging_actions_) {
      gizmo_dragging_action->SetNewKeyframes();
      ca->append(gizmo_dragging_action);
    }
    amber::UndoStack.push(ca);
    gizmo_dragging_actions_.clear();
  }
}

void Effect::gizmo_world_to_screen(const QMatrix4x4& mvp) {
  for (auto g : gizmos) {
    for (int j = 0; j < g->get_point_count(); j++) {
      QVector4D screen_pos = mvp * QVector4D(g->world_pos[j].x(), g->world_pos[j].y(), 0, 1.0);

      float w = screen_pos.w();
      if (qFuzzyIsNull(w)) w = 1.0f;

      int adjusted_sx1 = qRound(((screen_pos.x() / w * 0.5f) + 0.5f) * parent_clip->sequence->width);
      int adjusted_sy1 = qRound((1.0f - ((screen_pos.y() / w * 0.5f) + 0.5f)) * parent_clip->sequence->height);

      g->screen_pos[j] = QPoint(adjusted_sx1, adjusted_sy1);
    }
  }
}

bool Effect::are_gizmos_enabled() { return (gizmos.size() > 0); }

void Effect::redraw(double) {}

bool Effect::valueHasChanged(double timecode) {
  if (cachedValues.size() == 0) {
    for (int i = 0; i < row_count(); i++) {
      EffectRow* crow = row(i);
      for (int j = 0; j < crow->FieldCount(); j++) {
        cachedValues.append(crow->Field(j)->GetValueAt(timecode));
      }
    }
    return true;

  } else {
    bool changed = false;
    int index = 0;
    for (int i = 0; i < row_count(); i++) {
      EffectRow* crow = row(i);
      for (int j = 0; j < crow->FieldCount(); j++) {
        EffectField* field = crow->Field(j);
        if (cachedValues.at(index) != field->GetValueAt(timecode)) {
          changed = true;
        }
        cachedValues[index] = field->GetValueAt(timecode);
        index++;
      }
    }
    return changed;
  }
}

void Effect::delete_texture() {
  RenderThread::DeferRhiResourceDeletion(superimposeTex_);
  superimposeTex_ = nullptr;
}

const EffectMeta* get_meta_from_name(const QString& input) {
  int split_index = input.indexOf('/');
  QString category;
  if (split_index > -1) {
    category = input.left(split_index);
  }
  QString name = input.mid(split_index + 1);

  for (const auto& effect : effects) {
    if (effect.name == name && (effect.category == category || category.isEmpty())) {
      return &effect;
    }
  }
  return nullptr;
}

qint16 mix_audio_sample(qint16 a, qint16 b) {
  qint32 mixed_sample = static_cast<qint32>(a) + static_cast<qint32>(b);
  mixed_sample = qMax(qMin(mixed_sample, static_cast<qint32>(INT16_MAX)), static_cast<qint32>(INT16_MIN));
  return static_cast<qint16>(mixed_sample);
}
