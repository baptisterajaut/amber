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

#include "frei0reffect.h"

#ifndef NOFREI0R

#include <QMessageBox>
#include <QDir>
#include <QHash>
#include <QMutex>
#include <cstring>

using f0rConstructFunc = f0r_instance_t (*)(unsigned int width, unsigned int height);
using f0rInitFunc = int (*) ();
using f0rDeinitFunc = void (*) ();
using f0rUpdateFunc = void (*) (f0r_instance_t instance,
            double time, const uint32_t* inframe, uint32_t* outframe);
using f0rDestructFunc = void (*)(f0r_instance_t instance);
using f0rGetPluginInfo = void (*)(f0r_plugin_info_t* info);
using f0rGetParamValue = void (*)(f0r_instance_t instance, f0r_param_t param, int param_index);
using f0rSetParamValue = void (*) (f0r_instance_t instance,
        f0r_param_t param, int param_index);

// Frei0r spec: f0r_init / f0r_deinit must be called exactly once per plugin library.
// Instances sharing a library must not re-init or double-deinit.
// Heap-allocated and never freed to survive static destruction order during app exit.
static QMutex& frei0r_refcount_mutex() { static auto* m = new QMutex(); return *m; }
static QHash<QString, int>& frei0r_refcount() { static auto* h = new QHash<QString, int>(); return *h; }

// Per parameter, the default of each of its fields (two for a position) in the fields' units
using Frei0rDefaults = QVector<QVariantList>;

// A fresh instance's parameter values, which are the plugin's defaults: doubles and positions scaled to the fields'
// 0-100 (ApplyFrei0rParam sends them x0.01). Empty lists when the plugin can't build an instance.
// CBA: read at 640x480, the frame size not being known yet: a default the plugin derives from the size (pixeliz0r's
// block size) is that of 640x480, still the same share of the frame.
static Frei0rDefaults read_frei0r_defaults(QLibrary& handle, int param_count, f0rGetParamInfo get_param_info) {
  Frei0rDefaults defaults(param_count);
  auto construct = reinterpret_cast<f0rConstructFunc>(handle.resolve("f0r_construct"));
  auto destruct = reinterpret_cast<f0rDestructFunc>(handle.resolve("f0r_destruct"));
  auto get_value = reinterpret_cast<f0rGetParamValue>(handle.resolve("f0r_get_param_value"));
  if (construct == nullptr || destruct == nullptr || get_value == nullptr) return defaults;
  f0r_instance_t instance = construct(640, 480);
  if (instance == nullptr) return defaults;
  for (int i = 0; i < param_count; i++) {
    f0r_param_info_t param_info;
    get_param_info(&param_info, i);
    switch (param_info.type) {
      case F0R_PARAM_BOOL: {
        double b = 0;
        get_value(instance, &b, i);
        defaults[i] = {b >= 0.5};
      } break;
      case F0R_PARAM_DOUBLE: {
        double d = 0;
        get_value(instance, &d, i);
        defaults[i] = {d * 100.0};
      } break;
      case F0R_PARAM_COLOR: {
        f0r_param_color c{0, 0, 0};
        get_value(instance, &c, i);
        defaults[i] = {QColor::fromRgbF(qBound(0.f, c.r, 1.f), qBound(0.f, c.g, 1.f), qBound(0.f, c.b, 1.f))};
      } break;
      case F0R_PARAM_POSITION: {
        f0r_param_position pos{0, 0};
        get_value(instance, &pos, i);
        defaults[i] = {pos.x * 100.0, pos.y * 100.0};
      } break;
      case F0R_PARAM_STRING: {
        f0r_param_string str = nullptr;  // the plugin points it at its own copy
        get_value(instance, &str, i);
        defaults[i] = {QString::fromUtf8(str != nullptr ? str : "")};
      } break;
    }
  }
  destruct(instance);
  return defaults;
}

// The plugin's defaults, read once per plugin: building an instance can be slow (a face detector loads its model)
static Frei0rDefaults frei0r_defaults(const QString& dll_fn, QLibrary& handle, int param_count,
                                      f0rGetParamInfo get_param_info) {
  static auto* mutex = new QMutex();
  static auto* cache = new QHash<QString, Frei0rDefaults>();
  QMutexLocker lock(mutex);
  auto it = cache->constFind(dll_fn);
  if (it == cache->constEnd()) it = cache->insert(dll_fn, read_frei0r_defaults(handle, param_count, get_param_info));
  return it.value();
}

// Start field k of a parameter at the plugin's default for it, which Reset to Defaults then restores
static void seed_default(EffectField* field, const QVariantList& defaults, int k) {
  if (k >= defaults.size()) return;
  if (field->type() == EffectField::EFFECT_FIELD_DOUBLE) {
    static_cast<DoubleField*>(field)->SetDefault(defaults.at(k).toDouble());  // the slider's default too
  }
  field->SetValueAt(0, defaults.at(k));
  field->SetDefaultData(defaults.at(k));
}

Frei0rEffect::Frei0rEffect(Clip* c, const EffectMeta *em) :
  Effect(c, em)
  
{
  SetFlags(ImageFlag);

  // Windows DLL loading routine
  QString dll_fn = QDir(em->path).filePath(em->filename);

  handle.setFileName(dll_fn);


  if (!handle.load()) {
    QString dll_error = handle.errorString();
    QMessageBox::critical(nullptr, tr("Error loading Frei0r plugin"),
                          tr("Failed to load Frei0r plugin \"%1\": %2").arg(dll_fn, dll_error));

    return;
  }

  f0rInitFunc init = reinterpret_cast<f0rInitFunc>(handle.resolve("f0r_init"));
  if (init == nullptr) {
    QMessageBox::critical(nullptr, tr("Error loading Frei0r plugin"),
                          tr("Symbol f0r_init not found in \"%1\"").arg(dll_fn));
    return;
  }
  {
    QMutexLocker lock(&frei0r_refcount_mutex());
    auto& map = frei0r_refcount();
    if (map.value(dll_fn, 0) == 0) init();
    map[dll_fn] = map.value(dll_fn, 0) + 1;
  }

  // The instance is built by process_image() at the size of the frames it receives

  f0r_plugin_info_t info;
  f0rGetPluginInfo info_func = reinterpret_cast<f0rGetPluginInfo>(handle.resolve("f0r_get_plugin_info"));
  if (info_func == nullptr) {
    QMessageBox::critical(nullptr, tr("Error loading Frei0r plugin"),
                          tr("Symbol f0r_get_plugin_info not found in \"%1\"").arg(dll_fn));
    return;
  }
  info_func(&info);

  get_param_info = reinterpret_cast<f0rGetParamInfo>(handle.resolve("f0r_get_param_info"));
  // f0r_get_param_info is mandatory, but a plugin lacking it would be called through null: no parameters then
  param_count = get_param_info != nullptr ? info.num_params : 0;
  const Frei0rDefaults defaults = frei0r_defaults(dll_fn, handle, param_count, get_param_info);
  for (int i=0;i<param_count;i++) {
    f0r_param_info_t param_info;
    get_param_info(&param_info, i);

    if (param_info.type >= 0 && param_info.type <= F0R_PARAM_STRING) {
      EffectRow* row = new EffectRow(this, param_info.name);
      switch (param_info.type) {
      case F0R_PARAM_BOOL:
        seed_default(new BoolField(row, QString::number(i)), defaults.at(i), 0);
        break;
      case F0R_PARAM_DOUBLE:
      {
        DoubleField* f = new DoubleField(row, QString::number(i));
        f->SetMinimum(0);
        f->SetMaximum(100);
        seed_default(f, defaults.at(i), 0);
      }
        break;
      case F0R_PARAM_COLOR:
        seed_default(new ColorField(row, QString::number(i)), defaults.at(i), 0);
        break;
      case F0R_PARAM_POSITION:
      {
        DoubleField* fx = new DoubleField(row, QString("%1X").arg(QString::number(i)));
        fx->SetMinimum(0);
        fx->SetMaximum(100);
        DoubleField* fy = new DoubleField(row, QString("%1Y").arg(QString::number(i)));
        fy->SetMinimum(0);
        fy->SetMaximum(100);
        seed_default(fx, defaults.at(i), 0);
        seed_default(fy, defaults.at(i), 1);
      }
        break;
      case F0R_PARAM_STRING:
        seed_default(new StringField(row, QString::number(i), false), defaults.at(i), 0);
        break;
      }
    }
  }
}

Frei0rEffect::~Frei0rEffect() {
  destruct_module();

  if (handle.isLoaded()) {
    QString path = handle.fileName();
    bool last;
    {
      QMutexLocker lock(&frei0r_refcount_mutex());
      auto& map = frei0r_refcount();
      int& cnt = map[path];
      if (cnt > 0) cnt--;
      last = (cnt == 0);
      if (last) map.remove(path);
    }
    if (last) {
      f0rDeinitFunc deinit = reinterpret_cast<f0rDeinitFunc>(handle.resolve("f0r_deinit"));
      if (deinit != nullptr) deinit();
    }

    handle.unload();
  }
}

void Frei0rEffect::process_image(double timecode, uint8_t* input, uint8_t* output, int width, int height, int stride) {
  const int row_bytes = width * 4;
  if (width <= 0 || height <= 0 || stride < row_bytes) {
    if (stride > 0 && height > 0) memcpy(output, input, size_t(stride) * height);  // unusable size: pass through
    return;
  }

  // Build the instance at the size of the frame actually decoded, not the footage stream's: a proxied clip decodes
  // smaller frames, and a mid-stream resolution change alters them
  if (width != instance_width || height != instance_height) {
    destruct_module();
    construct_module(width, height);
  }

  // construct_module() leaves open=false when f0r_construct is missing or returns null: pass the frame through
  f0rUpdateFunc update_func = reinterpret_cast<f0rUpdateFunc>(handle.resolve("f0r_update"));
  f0rSetParamValue set_param = reinterpret_cast<f0rSetParamValue>(handle.resolve("f0r_set_param_value"));
  if (!open || instance == nullptr || update_func == nullptr || set_param == nullptr) {
    memcpy(output, input, size_t(stride) * height);
    return;
  }

  for (int i=0, row_idx=0; i<param_count; i++) {
    f0r_param_info_t param_info;
    get_param_info(&param_info, i);

    if (param_info.type < 0 || param_info.type > F0R_PARAM_STRING) continue;

    EffectRow* param_row = row(row_idx++);

    switch (param_info.type) {
    case F0R_PARAM_BOOL:
    {
      double b = param_row->Field(0)->GetValueAt(timecode).toBool() ? 1.0 : 0.0;
      set_param(instance, &b, i);
    }
      break;
    case F0R_PARAM_DOUBLE:
    {
      double d = param_row->Field(0)->GetValueAt(timecode).toDouble()*0.01;
      set_param(instance, &d, i);
    }
      break;
    case F0R_PARAM_COLOR:
    {
      QColor qcolor = param_row->Field(0)->GetValueAt(timecode).value<QColor>();

      f0r_param_color fcolor;
      fcolor.r = float(qcolor.redF());
      fcolor.g = float(qcolor.greenF());
      fcolor.b = float(qcolor.blueF());

      set_param(instance, &fcolor, i);
    }
      break;
    case F0R_PARAM_POSITION:
    {
      // The fields show 0-100 like the double params; Frei0r expects positions in [0, 1].
      f0r_param_position pos;
      pos.x = param_row->Field(0)->GetValueAt(timecode).toDouble() * 0.01;
      pos.y = param_row->Field(1)->GetValueAt(timecode).toDouble() * 0.01;
      set_param(instance, &pos, i);
    }
      break;
    case F0R_PARAM_STRING:
    {
      QByteArray bytes = param_row->Field(0)->GetValueAt(timecode).toString().toUtf8();
      char* byte_data = bytes.data();
      set_param(instance, &byte_data, i);
    }
      break;
    }
  }

  if (stride == row_bytes) {
    update_func(instance, timecode, reinterpret_cast<uint32_t*>(input), reinterpret_cast<uint32_t*>(output));
    return;
  }

  // Frei0r frames are tightly packed (pitch = width * 4), decoded rows are padded for alignment when the width isn't
  // a multiple of it. CBA: two extra frame copies on such widths; the plugin API has no stride.
  packed_in_.resize(size_t(width) * height);
  packed_out_.resize(size_t(width) * height);
  uint8_t* packed_in = reinterpret_cast<uint8_t*>(packed_in_.data());
  const uint8_t* packed_out = reinterpret_cast<const uint8_t*>(packed_out_.data());
  for (int y = 0; y < height; y++) memcpy(packed_in + size_t(y) * row_bytes, input + size_t(y) * stride, row_bytes);
  update_func(instance, timecode, packed_in_.data(), packed_out_.data());
  for (int y = 0; y < height; y++) memcpy(output + size_t(y) * stride, packed_out + size_t(y) * row_bytes, row_bytes);
}

void Frei0rEffect::refresh() {
  // Rebuilt (from a fresh state) on the next frame
  destruct_module();
  instance_width = 0;
  instance_height = 0;
}

namespace {
// Saved in every Frei0r effect since position fields reach the plugin scaled from 0-100 to 0-1
constexpr char kPositionUnitTag[] = "positionunit";

bool in_unit_range(const QVariant& v) { return v.toDouble() >= 0.0 && v.toDouble() <= 1.0; }
}  // namespace

void Frei0rEffect::load(QXmlStreamReader& stream) {
  positions_in_percent_ = false;
  Effect::load(stream);
  if (!positions_in_percent_) migrate_old_positions();
}

void Frei0rEffect::custom_load(QXmlStreamReader& stream) {
  if (stream.name() == QLatin1String(kPositionUnitTag)) positions_in_percent_ = true;
}

void Frei0rEffect::save(QXmlStreamWriter& stream) {
  Effect::save(stream);
  // A child element, not an attribute: a Missing Effect (plugin not installed) keeps its children through a re-save
  stream.writeEmptyElement(kPositionUnitTag);
  stream.writeAttribute("value", "percent");
}

// Data saved without kPositionUnitTag dates from when a position field's value went to the plugin as is: only 0-1
// meant anything (0.5 = centre). A position whose values, static and keyframed, on both axes, are all within 0-1 is
// such an old one and is scaled to the fields' 0-100; any value above 1 can only be the 0-100 unit, left alone.
// CBA: the marker-less data of a build between the 0-100 fix and the marker (never released) is misread when a
// position sits within 1% of the top-left corner on both axes; re-entering it once fixes it, and the marker saved
// from then on ends the guess.
void Frei0rEffect::migrate_old_positions() {
  for (int i = 0; i < row_count(); i++) {
    EffectRow* r = row(i);
    if (r->FieldCount() != 2) continue;  // only a position param has two fields (X, Y)
    bool old_unit = true;
    for (int j = 0; j < 2; j++) {
      EffectField* f = r->Field(j);
      old_unit = old_unit && in_unit_range(f->persistent_data_);
      for (const EffectKeyframe& key : f->keyframes) old_unit = old_unit && in_unit_range(key.data);
    }
    if (!old_unit) continue;
    for (int j = 0; j < 2; j++) {
      EffectField* f = r->Field(j);
      f->persistent_data_ = f->persistent_data_.toDouble() * 100.0;
      for (EffectKeyframe& key : f->keyframes) {
        key.data = key.data.toDouble() * 100.0;
        key.pre_handle_y *= 100.0;  // bezier handles are value offsets
        key.post_handle_y *= 100.0;
      }
      f->Changed();
    }
  }
}

void Frei0rEffect::destruct_module() {
  if (open) {
    f0rDestructFunc destruct = reinterpret_cast<f0rDestructFunc>(handle.resolve("f0r_destruct"));
    if (destruct != nullptr) destruct(instance);

    open = false;
    instance = nullptr;
  }
}

void Frei0rEffect::construct_module(int width, int height) {
  instance_width = width;
  instance_height = height;

  f0rConstructFunc construct = reinterpret_cast<f0rConstructFunc>(handle.resolve("f0r_construct"));
  if (construct == nullptr) return;
  instance = construct(instance_width, instance_height);

  if (instance == nullptr) {
    qWarning() << "Frei0r: f0r_construct returned null for" << meta->name;
    return;
  }
  open = true;
}

#endif
