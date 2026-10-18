#ifndef EFFECTPRESETS_H
#define EFFECTPRESETS_H

#include <QString>
#include <QVector>

#include "effects/effect.h"

// Named effect presets: one XML file per preset in EffectPresetDir(), holding one or more effects exactly as
// Effect::save writes them. Kept free of widgets so it can be tested on its own; the menus live in EffectControls.
// CBA: menu-only presets; an effects browser panel with drag-to-clip if asked

struct EffectPresetInfo {
  QString name;  // display name (the root's name attribute, or the file's base name without it)
  QString path;
  int subtype;  // EFFECT_TYPE_VIDEO or EFFECT_TYPE_AUDIO
};

// <data dir>/effectpresets: AppDataLocation, or next to the executable with a `portable` marker (get_data_path)
QString EffectPresetDir();

// The new file for a preset of this display name: up to 64 characters of it with those outside [A-Za-z0-9 _-] as
// '_', then '-' and 8 hex digits of the name's SHA-1, so names that sanitise alike ("Glow!", "Glow?") get their own
// file. Stable: the same name always maps to the same file.
QString EffectPresetPathForName(const QString& name);

// Where saving a preset named `name` writes: the file of an existing preset showing this exact name (whatever scheme
// named the file, older presets were saved without the hash), else EffectPresetPathForName(name). *replaced gets
// the name of the preset that file holds, empty if the save creates a new file.
QString EffectPresetSavePath(const QString& name, QString* replaced);

// Presets of `subtype` in EffectPresetDir(), sorted by name case-insensitively. Only the root element of each file
// is read; files that don't parse as a preset are skipped.
QVector<EffectPresetInfo> ListEffectPresets(int subtype);

// Writes `effects` as a preset to `path`, creating its directory if needed. False if the file can't be written.
bool SaveEffectPreset(const QString& path, const QString& name, int subtype, const QVector<Effect*>& effects);

// New effects for `parent` from the preset at `path`, in file order. An effect this system doesn't know (a missing
// Frei0r plugin or ShaderToy import) is skipped and counted in *missing. An unreadable file returns nothing.
QVector<EffectPtr> LoadEffectPreset(const QString& path, Clip* parent, int* missing);

#endif  // EFFECTPRESETS_H
