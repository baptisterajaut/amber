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

#ifndef FREI0REFFECT_H
#define FREI0REFFECT_H

#ifndef NOFREI0R

#include <QLibrary>
#include <vector>
#include "include/frei0r.h"

#include "effects/effect.h"

using f0rGetParamInfo = void (*)(f0r_param_info_t * info,
                int param_index );

class Frei0rEffect : public Effect {
  Q_OBJECT
public:
    Frei0rEffect(Clip* c, const EffectMeta* em);
  ~Frei0rEffect() override;

  void process_image(double timecode, uint8_t* input, uint8_t* output, int width, int height, int stride) override;

  void refresh() override;

  void load(QXmlStreamReader& stream) override;
  void custom_load(QXmlStreamReader& stream) override;
  void save(QXmlStreamWriter& stream) override;

 private:
  // set by custom_load() when the loaded data says its positions are in the fields' 0-100 unit
  bool positions_in_percent_{false};
  void migrate_old_positions();
  QLibrary handle;
  f0r_instance_t instance{nullptr};
  int param_count{0};  // stays 0 (no rows) when the plugin fails to load
  f0rGetParamInfo get_param_info{nullptr};
  void destruct_module();
  void construct_module(int width, int height);
  bool open{false};
  int instance_width{0};
  int instance_height{0};
  // Tightly packed copies of a frame whose rows are padded (Frei0r frames have no stride)
  std::vector<uint32_t> packed_in_;
  std::vector<uint32_t> packed_out_;
};

#endif

#endif // FREI0REFFECT_H
