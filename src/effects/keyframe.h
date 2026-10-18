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

#ifndef KEYFRAME_H
#define KEYFRAME_H

#include "core/keyframe.h"

#include <QVariant>
#include <QVector>

class ComboAction;
class EffectField;

void delete_keyframes(QVector<EffectField *> &selected_key_fields, QVector<int> &selected_keys);

// Whether the Graph Editor draws a key's bezier handles, and so whether they can be grabbed: both rely on this one
// test so a hidden handle (the default +-40 of a Linear or Hold key) is never hit.
bool keyframe_shows_handles(int type);

// Graph Editor Value entry: one undo step setting the same value, clamped to each field's range, on several keys.
// old_values are the keys' values from before a live slider drag (the keys may already hold the new value).
// nullptr when no key changes.
ComboAction *keyframe_value_entry(const QVector<EffectField *> &fields, const QVector<int> &keys,
                                  const QVector<QVariant> &old_values, double value);

#endif // KEYFRAME_H
