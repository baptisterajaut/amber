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

#include "keyframe.h"

#include <QVector>

#include "core/appcontext.h"
#include "core/graphmath.h"
#include "effect.h"
#include "effectfields.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"

void delete_keyframes(QVector<EffectField*>& selected_key_fields, QVector<int>& selected_keys) {
  QVector<EffectField*> fields;
  QVector<int> key_indices;

  for (int i = 0; i < selected_keys.size(); i++) {
    bool added = false;
    for (int j = 0; j < key_indices.size(); j++) {
      if (key_indices.at(j) < selected_keys.at(i)) {
        key_indices.insert(j, selected_keys.at(i));
        fields.insert(j, selected_key_fields.at(i));
        added = true;
        break;
      }
    }
    if (!added) {
      key_indices.append(selected_keys.at(i));
      fields.append(selected_key_fields.at(i));
    }
  }

  if (fields.size() > 0) {
    ComboAction* ca = new ComboAction(QObject::tr("Delete Keyframe(s)"));
    for (int i = 0; i < key_indices.size(); i++) {
      ca->append(new KeyframeDelete(fields.at(i), key_indices.at(i)));
    }
    amber::UndoStack.push(ca);
    selected_keys.clear();
    selected_key_fields.clear();
    amber::app_ctx->updateUi(false);
  }
}

bool keyframe_shows_handles(int type) { return type == EFFECT_KEYFRAME_BEZIER; }

ComboAction* keyframe_value_entry(const QVector<EffectField*>& fields, const QVector<int>& keys,
                                  const QVector<QVariant>& old_values, double value) {
  ComboAction* ca = nullptr;
  const int n = qMin(fields.size(), qMin(keys.size(), old_values.size()));
  for (int i = 0; i < n; i++) {
    EffectField* f = fields.at(i);
    if (f->type() != EffectField::EFFECT_FIELD_DOUBLE) continue;
    DoubleField* df = static_cast<DoubleField*>(f);
    const double v = amber::graph::ClampToRange(value, df->GetMinimum(), df->GetMaximum());
    if (old_values.at(i).toDouble() == v) continue;
    if (ca == nullptr) ca = new ComboAction(QObject::tr("Change Keyframe Value"));
    ca->append(new KeyframeMemberChange(f, keys.at(i), KeyframeMemberChange::kData, old_values.at(i), v));
  }
  return ca;
}

ComboAction* keyframe_easy_ease(const QVector<EffectField*>& fields, const QVector<int>& keys,
                                amber::graph::EaseSide side) {
  ComboAction* ca = nullptr;
  auto change = [&ca](EffectField* f, int k, KeyframeMemberChange::Member m, const QVariant& from, const QVariant& to) {
    if (from == to) return;
    if (ca == nullptr) ca = new ComboAction(QObject::tr("Easy Ease"));
    ca->append(new KeyframeMemberChange(f, k, m, from, to));
  };

  const int n = qMin(fields.size(), keys.size());
  for (int i = 0; i < n; i++) {
    EffectField* f = fields.at(i);
    const int k = keys.at(i);
    const EffectKeyframe& key = f->keyframes.at(k);

    std::optional<long> prev, next;
    for (const EffectKeyframe& other : f->keyframes) {
      if (other.time < key.time && (!prev || other.time > *prev)) prev = other.time;
      if (other.time > key.time && (!next || other.time < *next)) next = other.time;
    }
    const amber::graph::Handles h = amber::graph::EasyEase(
        {key.pre_handle_x, key.pre_handle_y, key.post_handle_x, key.post_handle_y}, key.time, prev, next, side);

    change(f, k, KeyframeMemberChange::kType, key.type, int(EFFECT_KEYFRAME_BEZIER));
    change(f, k, KeyframeMemberChange::kPreHandleX, key.pre_handle_x, h.pre_x);
    change(f, k, KeyframeMemberChange::kPreHandleY, key.pre_handle_y, h.pre_y);
    change(f, k, KeyframeMemberChange::kPostHandleX, key.post_handle_x, h.post_x);
    change(f, k, KeyframeMemberChange::kPostHandleY, key.post_handle_y, h.post_y);
  }
  return ca;
}
