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

#ifndef EFFECTLOADERS_H
#define EFFECTLOADERS_H

#include <QList>
#include <QMutex>
#include <QString>
#include <QThread>

/**
 * @brief The EffectInit class
 *
 * A separate thread for loading effects in the background while the rest of the program's initiation takes place.
 * The program can even run before the effects have finished loading, but any point the software needs to access
 * effects, it will have to wait for this thread to finish before it can. Fortunately this thread is usually very
 * quick and is over before the MainWindow shows.
 */
class EffectInit : public QThread {
public:
  EffectInit();

  /**
   * @brief A static convenience function to set up the EffectInit thread, start it, and free itself when complete.
   */
  static void StartLoading();
protected:
  /**
   * @brief Function that runs in the other thread.
   */
  void run() override;
};

/// Mutex used to synchronize effect loading with consumers.
/// EffectInit locks it at construction and unlocks it when loading finishes.
/// Code that needs effects to be available should lock/unlock this mutex.
extern QMutex effects_loaded_mutex;

/// True for an effect name as a project saves it ("category/name", Effect::save) that belongs to a Frei0r plugin.
bool is_frei0r_effect_name(const QString& saved_name);

/// The rule for listing Frei0r effects in the add-effect menus: the preference is on, or the open project uses some.
bool frei0r_effects_visible(bool show_preference, bool project_uses_frei0r);

/// Frei0r effects (CPU plugins, slower) are hidden from the add-effect menus unless Config::show_frei0r_effects is on
/// or the open project uses some. Hidden effects still load, render, copy and paste.
bool frei0r_effects_visible();

/// Session flag, never saved: set when a loaded or imported project contains Frei0r effects, cleared when the
/// project is cleared (new project, before each open).
void set_project_uses_frei0r(bool uses);

#endif // EFFECTLOADERS_H
