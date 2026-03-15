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

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtMath>
#include <rhi/qshaderbaker.h>

#include "global/config.h"
#include "global/debug.h"
#include "global/math.h"
#include "global/path.h"
#include "panels/effectcontrols.h"
#include "panels/grapheditor.h"
#include "panels/panels.h"
#include "panels/project.h"
#include "panels/timeline.h"
#include "panels/viewer.h"
#include "project/clipboard.h"
#include "timeline/clip.h"
#include "timeline/sequence.h"
#include "transition.h"
#include "ui/collapsiblewidget.h"
#include "ui/mainwindow.h"
#include "ui/viewerwidget.h"
#include "undo/undo.h"
#include "undo/undostack.h"

#include "effects/internal/audionoiseeffect.h"
#include "effects/internal/cornerpineffect.h"
#include "effects/internal/fillleftrighteffect.h"
#include "effects/internal/frei0reffect.h"
#include "effects/internal/paneffect.h"
#include "effects/internal/richtexteffect.h"
#include "effects/internal/shakeeffect.h"
#include "effects/internal/solideffect.h"
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

}  // namespace

EffectPtr Effect::Create(Clip* c, const EffectMeta* em) {
  if (em->internal >= 0 && em->internal < EFFECT_INTERNAL_COUNT) {
    // must be an internal effect
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
    }
  } else if (!em->filename.isEmpty()) {
    // load effect from file
    return std::make_shared<Effect>(c, em);
  } else {
    qCritical() << "Invalid effect data";
    QMessageBox::critical(
        olive::MainWindow, QCoreApplication::translate("Effect", "Invalid effect"),
        QCoreApplication::translate(
            "Effect", "No candidate for effect '%1'. This effect may be corrupt. Try reinstalling it or Amber.")
            .arg(em->name));
  }
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
    // set up UI from effect file
    name = em->name;

    if (!em->filename.isEmpty() && em->internal == -1) {
      QFile effect_file(em->filename);
      if (effect_file.open(QFile::ReadOnly)) {
        QXmlStreamReader reader(&effect_file);

        while (!reader.atEnd()) {
          if (reader.name() == QLatin1String("row") && reader.isStartElement()) {
            QString row_name;
            const QXmlStreamAttributes& attributes = reader.attributes();
            for (const auto& attr : attributes) {
              if (attr.name() == QLatin1String("name")) {
                row_name = attr.value().toString();
              }
            }
            if (!row_name.isEmpty()) {
              EffectRow* row = new EffectRow(this, row_name);
              while (!reader.atEnd() && !(reader.name() == QLatin1String("row") && reader.isEndElement())) {
                reader.readNext();
                if (reader.name() == QLatin1String("field") && reader.isStartElement()) {
                  int type = -1;
                  QString id;

                  // get field type and id from attributes
                  const QXmlStreamAttributes& attributes = reader.attributes();
                  for (const auto& attr : attributes) {
                    if (attr.name() == QLatin1String("type")) {
                      type = FieldTypeFromString(attr.value().toString().toUpper());
                    } else if (attr.name() == QLatin1String("id")) {
                      id = attr.value().toString();
                    }
                  }

                  if (id.isEmpty()) {
                    qCritical() << "Couldn't load field from" << em->filename << "- ID cannot be empty.";
                  } else {
                    EffectField* field = nullptr;

                    switch (type) {
                      case EffectField::EFFECT_FIELD_DOUBLE: {
                        DoubleField* double_field = new DoubleField(row, id);

                        for (const auto& attr : attributes) {
                          if (attr.name() == QLatin1String("default")) {
                            double_field->SetDefault(attr.value().toDouble());
                          } else if (attr.name() == QLatin1String("min")) {
                            double_field->SetMinimum(attr.value().toDouble());
                          } else if (attr.name() == QLatin1String("max")) {
                            double_field->SetMaximum(attr.value().toDouble());
                          }
                        }

                        field = double_field;
                      } break;
                      case EffectField::EFFECT_FIELD_COLOR: {
                        QColor color;

                        field = new ColorField(row, id);

                        for (const auto& attr : attributes) {
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

                        field->SetValueAt(0, color);
                      } break;
                      case EffectField::EFFECT_FIELD_STRING:
                        field = new StringField(row, id);
                        for (const auto& attr : attributes) {
                          if (attr.name() == QLatin1String("default")) {
                            field->SetValueAt(0, attr.value().toString());
                          }
                        }
                        break;
                      case EffectField::EFFECT_FIELD_BOOL:
                        field = new BoolField(row, id);
                        for (const auto& attr : attributes) {
                          if (attr.name() == QLatin1String("default")) {
                            field->SetValueAt(0, attr.value() == QLatin1String("1"));
                          }
                        }
                        break;
                      case EffectField::EFFECT_FIELD_COMBO: {
                        ComboField* combo_field = new ComboField(row, id);
                        int combo_default_index = 0;
                        for (const auto& attr : attributes) {
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
                            combo_field->AddItem(reader.text().toString(), combo_item_count);
                            combo_item_count++;
                          }
                        }
                        combo_field->SetValueAt(0, combo_default_index);
                        field = combo_field;
                      } break;
                      case EffectField::EFFECT_FIELD_FONT:
                        field = new FontField(row, id);
                        for (const auto& attr : attributes) {
                          if (attr.name() == QLatin1String("default")) {
                            field->SetValueAt(0, attr.value().toString());
                          }
                        }
                        break;
                      case EffectField::EFFECT_FIELD_FILE:
                        field = new FileField(row, id);
                        for (const auto& attr : attributes) {
                          if (attr.name() == QLatin1String("filename")) {
                            field->SetValueAt(0, attr.value().toString());
                          }
                        }
                        break;
                    }
                  }
                }
              }
            }
          } else if (reader.name() == QLatin1String("shader") && reader.isStartElement()) {
            SetFlags(Flags() | ShaderFlag);
            const QXmlStreamAttributes& attributes = reader.attributes();
            for (const auto& attr : attributes) {
              if (attr.name() == QLatin1String("vert")) {
                vertPath = attr.value().toString();
              } else if (attr.name() == QLatin1String("frag")) {
                fragPath = attr.value().toString();
              } else if (attr.name() == QLatin1String("iterations")) {
                setIterations(attr.value().toInt());
              }
            }
          }
          reader.readNext();
        }

        effect_file.close();
      } else {
        qCritical() << "Failed to open effect file" << em->filename;
      }
    }
  }
}

Effect::~Effect() {
  if (isOpen) {
    close();
  }

  // Clear graph editor if it's using one of these rows
  if (panel_graph_editor != nullptr) {
    for (int i = 0; i < row_count(); i++) {
      if (row(i) == panel_graph_editor->get_row()) {
        panel_graph_editor->set_row(nullptr);
        break;
      }
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
  for (int i = 0; i < rows.size(); i++) {
    EffectRow* row = rows.at(i);
    EffectRow* copy_row = e->rows.at(i);
    copy_row->SetKeyframingInternal(row->IsKeyframing());
    for (int j = 0; j < row->FieldCount(); j++) {
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

void Effect::FieldChanged() { update_ui(false); }

void Effect::delete_self() {
  olive::UndoStack.push(new EffectDeleteCommand(this));
  update_ui(true);
}

void Effect::move_up() {
  int index_of_effect = parent_clip->IndexOfEffect(this);
  if (index_of_effect == 0) {
    return;
  }

  MoveEffectCommand* command = new MoveEffectCommand();
  command->clip = parent_clip;
  command->from = index_of_effect;
  command->to = command->from - 1;
  olive::UndoStack.push(command);
  panel_effect_controls->Reload();
  panel_sequence_viewer->viewer_widget->frame_update();
}

void Effect::move_down() {
  int index_of_effect = parent_clip->IndexOfEffect(this);
  if (index_of_effect == parent_clip->effects.size() - 1) {
    return;
  }

  MoveEffectCommand* command = new MoveEffectCommand();
  command->clip = parent_clip;
  command->from = index_of_effect;
  command->to = command->from + 1;
  olive::UndoStack.push(command);
  panel_effect_controls->Reload();
  panel_sequence_viewer->viewer_widget->frame_update();
}

void Effect::save_to_file() {
  // save effect settings to file
  QString file = QFileDialog::getSaveFileName(olive::MainWindow, tr("Save Effect Settings"), QString(),
                                              tr("Effect XML Settings %1").arg("(*.xml)"));

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
      QMessageBox::critical(olive::MainWindow, tr("Save Settings Failed"),
                            tr("Failed to open \"%1\" for writing.").arg(file), QMessageBox::Ok);
    }
  }
}

void Effect::load_from_file() {
  // load effect settings from file
  QString file = QFileDialog::getOpenFileName(olive::MainWindow, tr("Load Effect Settings"), QString(),
                                              tr("Effect XML Settings %1").arg("(*.xml)"));

  // if the user picked a file
  if (!file.isEmpty()) {
    QFile file_handle(file);
    if (file_handle.open(QFile::ReadOnly)) {
      olive::UndoStack.push(new SetEffectData(this, file_handle.readAll()));

      file_handle.close();

      update_ui(false);
    } else {
      QMessageBox::critical(olive::MainWindow, tr("Load Settings Failed"),
                            tr("Failed to open \"%1\" for reading.").arg(file), QMessageBox::Ok);
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
        EffectRow* row = rows.at(row_count);

        while (!stream.atEnd() && !(stream.name() == QLatin1String("row") && stream.isEndElement())) {
          stream.readNext();

          // read field
          if (stream.name() == QLatin1String("field") && stream.isStartElement()) {
            int field_number = -1;

            // match field using ID
            for (int k = 0; k < stream.attributes().size(); k++) {
              const QXmlStreamAttribute& attr = stream.attributes().at(k);
              if (attr.name() == QLatin1String("id")) {
                for (int l = 0; l < row->FieldCount(); l++) {
                  if (row->Field(l)->id() == attr.value()) {
                    field_number = l;
                    break;
                  }
                }
                break;
              }
            }

            if (field_number > -1) {
              EffectField* field = row->Field(field_number);

              // get current field value
              for (int k = 0; k < stream.attributes().size(); k++) {
                const QXmlStreamAttribute& attr = stream.attributes().at(k);
                if (attr.name() == QLatin1String("value")) {
                  field->persistent_data_ = field->ConvertStringToValue(attr.value().toString());
                  break;
                }
              }

              while (!stream.atEnd() && !(stream.name() == QLatin1String("field") && stream.isEndElement())) {
                stream.readNext();

                // read keyframes
                if (stream.name() == QLatin1String("key") && stream.isStartElement()) {
                  row->SetKeyframingInternal(true);

                  EffectKeyframe key;
                  for (int k = 0; k < stream.attributes().size(); k++) {
                    const QXmlStreamAttribute& attr = stream.attributes().at(k);
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
                  field->keyframes.append(key);
                }
              }

              field->Changed();
            }
          }
        }

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
            QMessageBox::critical(olive::MainWindow, tr("Load Settings Failed"),
                                  tr("This settings file doesn't match this effect."), QMessageBox::Ok);
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

  QString hash = QString::number(qHash(source), 16);
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
  if (olive::CurrentRuntimeConfig.shaders_are_enabled && (Flags() & ShaderFlag)) {
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

void Effect::process_shader(double timecode, GLTextureCoords&, int iteration, QByteArray& uboData) {
  if (uboData.size() < fragUboSize_) uboData.resize(fragUboSize_);

  // Set automatic uniforms by looking up their entries
  for (const auto& entry : uniformEntries_) {
    if (entry.name == QLatin1String("resolution")) {
      float res[2] = {float(parent_clip->media_width()), float(parent_clip->media_height())};
      memcpy(uboData.data() + entry.offset, res, qMin(entry.size, 8));
    } else if (entry.name == QLatin1String("resolution_x")) {
      float v = float(parent_clip->media_width());
      memcpy(uboData.data() + entry.offset, &v, 4);
    } else if (entry.name == QLatin1String("resolution_y")) {
      float v = float(parent_clip->media_height());
      memcpy(uboData.data() + entry.offset, &v, 4);
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
      // Find matching uniform entry
      for (const auto& entry : uniformEntries_) {
        if (entry.name == field->id()) {
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
              int v = field->GetValueAt(timecode).toBool() ? 1 : 0;  // GLSL bool = 4 bytes
              memcpy(uboData.data() + entry.offset, &v, 4);
            } break;
            case EffectField::EFFECT_FIELD_COMBO: {
              int v = field->GetValueAt(timecode).toInt();
              memcpy(uboData.data() + entry.offset, &v, 4);
            } break;
            default:
              break;
          }
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
    delete superimposeTex_;
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
  // Loop through each gizmo to find `gizmo`
  for (auto i : gizmos) {
    if (i == gizmo) {
      if (!done && gizmo_dragging_actions_.isEmpty()) {
        if (gizmo->x_field1 != nullptr) {
          gizmo_dragging_actions_.append(new KeyframeDataChange(gizmo->x_field1));
        }
        if (gizmo->y_field1 != nullptr) {
          gizmo_dragging_actions_.append(new KeyframeDataChange(gizmo->y_field1));
        }
        if (gizmo->x_field2 != nullptr) {
          gizmo_dragging_actions_.append(new KeyframeDataChange(gizmo->x_field2));
        }
        if (gizmo->y_field2 != nullptr) {
          gizmo_dragging_actions_.append(new KeyframeDataChange(gizmo->y_field2));
        }
      }

      // Update the field values
      if (gizmo->x_field1 != nullptr) {
        gizmo->x_field1->SetValueAt(timecode,
                                    gizmo->x_field1->GetDoubleAt(timecode) + x_movement * gizmo->x_field_multi1);
      }
      if (gizmo->y_field1 != nullptr) {
        gizmo->y_field1->SetValueAt(timecode,
                                    gizmo->y_field1->GetDoubleAt(timecode) + y_movement * gizmo->y_field_multi1);
      }
      if (gizmo->x_field2 != nullptr) {
        gizmo->x_field2->SetValueAt(timecode,
                                    gizmo->x_field2->GetDoubleAt(timecode) + x_movement * gizmo->x_field_multi2);
      }
      if (gizmo->y_field2 != nullptr) {
        gizmo->y_field2->SetValueAt(timecode,
                                    gizmo->y_field2->GetDoubleAt(timecode) + y_movement * gizmo->y_field_multi2);
      }

      if (done && !gizmo_dragging_actions_.isEmpty()) {
        ComboAction* ca = new ComboAction();

        for (auto gizmo_dragging_action : gizmo_dragging_actions_) {
          gizmo_dragging_action->SetNewKeyframes();
          ca->append(gizmo_dragging_action);
        }

        olive::UndoStack.push(ca);

        gizmo_dragging_actions_.clear();
      }
      break;
    }
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
  delete superimposeTex_;
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
