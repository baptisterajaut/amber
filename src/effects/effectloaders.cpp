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

#include "effectloaders.h"

#include "effects/effect.h"
#include "effects/transition.h"
#include "core/path.h"
#include "global/config.h"

QMutex effects_loaded_mutex;

#include <QDir>
#include <QXmlStreamReader>
#include <QLibrary>
#include <QDebug>

#ifndef NOFREI0R
#include "include/frei0r.h"
using f0rGetPluginInfo = void (*)(f0r_plugin_info_t* info);
#endif

void load_internal_effects() {
  if (!amber::CurrentRuntimeConfig.shaders_are_enabled) {
    qWarning() << "Shaders are disabled, some effects may be nonfunctional";
  }

  EffectMeta em;

  // load internal effects
  em.path = ":/internalshaders";

  em.type = EFFECT_TYPE_EFFECT;
  em.subtype = EFFECT_TYPE_AUDIO;

  em.name = "Volume";
  em.internal = EFFECT_INTERNAL_VOLUME;
  effects.append(em);

  em.name = "Pan";
  em.internal = EFFECT_INTERNAL_PAN;
  effects.append(em);

  em.name = "VST Plugin 2.x";
  em.internal = EFFECT_INTERNAL_VST;
  effects.append(em);

  em.name = "Tone";
  em.internal = EFFECT_INTERNAL_TONE;
  effects.append(em);

  em.name = "Noise";
  em.internal = EFFECT_INTERNAL_NOISE;
  effects.append(em);

  em.name = "Fill Left/Right";
  em.internal = EFFECT_INTERNAL_FILLLEFTRIGHT;
  effects.append(em);

  em.subtype = EFFECT_TYPE_VIDEO;

  em.name = "Transform";
  em.category = "Distort";
  em.internal = EFFECT_INTERNAL_TRANSFORM;
  effects.append(em);

  em.name = "Corner Pin";
  em.internal = EFFECT_INTERNAL_CORNERPIN;
  effects.append(em);

  /*em.name = "Mask";
  em.internal = EFFECT_INTERNAL_MASK;
  effects.append(em);*/

  em.name = "Shake";
  em.internal = EFFECT_INTERNAL_SHAKE;
  effects.append(em);

  em.name = "Text";
  em.category = "Render";
  em.internal = EFFECT_INTERNAL_TEXT;
  effects.append(em);

  em.name = "Rich Text";
  em.category = "Render";
  em.internal = EFFECT_INTERNAL_RICHTEXT;
  effects.append(em);

  em.name = "Timecode";
  em.internal = EFFECT_INTERNAL_TIMECODE;
  effects.append(em);

  em.name = "Solid";
  em.internal = EFFECT_INTERNAL_SOLID;
  effects.append(em);

  // internal transitions
  em.type = EFFECT_TYPE_TRANSITION;
  em.category = "";

  em.name = "Cross Dissolve";
  em.internal = TRANSITION_INTERNAL_CROSSDISSOLVE;
  effects.append(em);

  em.subtype = EFFECT_TYPE_AUDIO;

  em.name = "Linear Fade";
  em.internal = TRANSITION_INTERNAL_LINEARFADE;
  effects.append(em);

  em.name = "Exponential Fade";
  em.internal = TRANSITION_INTERNAL_EXPONENTIALFADE;
  effects.append(em);

  em.name = "Logarithmic Fade";
  em.internal = TRANSITION_INTERNAL_LOGARITHMICFADE;
  effects.append(em);
}

void load_shader_effects_worker(const QString& effects_path) {
  QDir effects_dir(effects_path);
  if (effects_dir.exists()) {
    QList<QString> entries = effects_dir.entryList(QStringList("*.xml"), QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    for (const auto & entrie : entries) {
      QString entry_path = effects_dir.filePath(entrie);
      if (QFileInfo(entry_path).isDir()) {
        load_shader_effects_worker(entry_path);
      } else {
        QFile file(effects_path + "/" + entrie);
        if (!file.open(QIODevice::ReadOnly)) {
          qCritical() << "Could not open" << entrie;
          continue;
        }

        QXmlStreamReader reader(&file);
        while (!reader.atEnd()) {
          if (reader.name() == QLatin1String("effect")) {
            QString effect_name = "";
            QString effect_cat = "";
            const QXmlStreamAttributes attr = reader.attributes();
            for (const auto & j : attr) {
              if (j.name() == QLatin1String("name")) {
                effect_name = j.value().toString();
              } else if (j.name() == QLatin1String("category")) {
                effect_cat = j.value().toString();
              }
            }
            if (!effect_name.isEmpty()) {
              EffectMeta em;
              em.type = EFFECT_TYPE_EFFECT;
              em.subtype = EFFECT_TYPE_VIDEO;
              em.name = effect_name;
              em.category = effect_cat;
              em.filename = file.fileName();
              em.path = effects_path;
              em.internal = -1;
              effects.append(em);
            } else {
              qCritical() << "Invalid effect found in" << entrie;
            }
            break;
          }
          reader.readNext();
        }

        file.close();
      }
    }
  }
}

void load_shader_effects() {
  QList<QString> effects_paths = get_effects_paths();

  for (const auto & effects_path : effects_paths) {
    load_shader_effects_worker(effects_path);
  }
}

void EffectInit::StartLoading() {
  EffectInit* init_thread = new EffectInit();
  QObject::connect(init_thread, &EffectInit::finished, init_thread, &QObject::deleteLater);
  init_thread->start();
}

#ifndef NOFREI0R
void load_frei0r_effects_worker(const QString& dir, EffectMeta& em, QVector<QString>& loaded_names) {
  QDir search_dir(dir);
  if (search_dir.exists()) {
    QList<QString> entry_list = search_dir.entryList(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    for (const auto & j : entry_list) {
      QString entry_path = search_dir.filePath(j);
      if (QFileInfo(entry_path).isDir()) {
        load_frei0r_effects_worker(entry_path, em, loaded_names);
      } else {

        QString path_without_extension = search_dir.filePath(QFileInfo(j).baseName());

        QLibrary effect;
        effect.setFileName(path_without_extension);
        if (effect.load()) {
          f0rGetPluginInfo get_info_func = reinterpret_cast<f0rGetPluginInfo>(effect.resolve("f0r_get_plugin_info"));
          if (get_info_func != nullptr) {
            f0r_plugin_info_t info;
            get_info_func(&info);

            if (!loaded_names.contains(info.name)
                && info.plugin_type == F0R_PLUGIN_TYPE_FILTER
                && info.color_model == F0R_COLOR_MODEL_RGBA8888) {
              em.name = info.name;
              em.path = dir;
              em.filename = j;
              em.tooltip = QString("%1\n%2\n%3\n%4").arg(em.name, info.author, info.explanation, em.filename);

              loaded_names.append(em.name);

              effects.append(em);
            }
          }
          effect.unload();
        }
      }
    }
  }
}

void load_frei0r_effects() {
  QList<QString> effect_dirs = get_effects_paths();

  // add defined paths for frei0r plugins on unix
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX) || defined(__HAIKU__)
  effect_dirs.prepend("/usr/lib/frei0r-1");
  effect_dirs.prepend("/usr/local/lib/frei0r-1");
  effect_dirs.prepend(QDir::homePath() + "/.frei0r-1/lib");
#endif

  QString env_path(qgetenv("FREI0R_PATH"));
  if (!env_path.isEmpty()) effect_dirs.append(env_path);

  QVector<QString> loaded_names;

  // search for paths
  EffectMeta em;
  em.category = "Frei0r";
  em.type = EFFECT_TYPE_EFFECT;
  em.subtype = EFFECT_TYPE_VIDEO;
  em.internal = EFFECT_INTERNAL_FREI0R;

  for (const auto & effect_dir : effect_dirs) {
    load_frei0r_effects_worker(effect_dir, em, loaded_names);
  }
}
#endif

EffectInit::EffectInit() {
  effects_loaded_mutex.lock();
}

void EffectInit::run() {
  qInfo() << "Initializing effects...";
  load_internal_effects();
  load_shader_effects();
#ifndef NOFREI0R
  load_frei0r_effects();
#endif
  effects_loaded_mutex.unlock();
  qInfo() << "Finished initializing effects";
}
