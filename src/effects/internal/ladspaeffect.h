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

#ifndef LADSPAEFFECT_H
#define LADSPAEFFECT_H

#ifndef NOLADSPA

#include <QLibrary>
#include <QStringList>

#include <memory>
#include <vector>

#include "effects/effect.h"
#include "effects/internal/audiostreamstate.h"
#include "include/ladspa.h"

namespace amber {
namespace ladspa {

// What a control input port's LADSPA_PortRangeHint means for its Effect Controls field
struct PortRange {
  bool toggled = false;
  bool integer = false;
  bool has_min = false;
  bool has_max = false;
  double min = 0;  // valid with has_min, already x rate for LADSPA_HINT_SAMPLE_RATE
  double max = 0;  // valid with has_max, likewise
  double def = 0;  // inside the range; 0 or 1 for a toggle
  int decimals = 2;

  // v clamped into the range, rounded for an integer port, 0 / 1 for a toggle
  double clamp(double v) const;
};

// The header's rules: bounds x sample_rate with LADSPA_HINT_SAMPLE_RATE; DEFAULT_MINIMUM / MAXIMUM the bounds, LOW /
// MIDDLE / HIGH at 1/4, 1/2, 3/4 between them (geometric with LADSPA_HINT_LOGARITHMIC and positive bounds), 0 / 1 /
// 100 / 440 as such. Without a usable default hint: the lower bound, else 0 clamped into the range.
PortRange port_range(const LADSPA_PortRangeHint& hint, int sample_rate);

// $LADSPA_PATH (QDir::listSeparator()-separated) when set, otherwise the platform's conventional folders
QStringList search_paths();

// The plugins the folders hold that Amber can host (1 -> 1 or 2 -> 2 audio ports), one EffectMeta each:
// category "LADSPA", internal EFFECT_INTERNAL_LADSPA, sorted by name. The first folder wins for a UniqueID found
// twice; a later plugin whose name is taken (in UniqueID order) gets " (<UniqueID>)" appended.
std::vector<EffectMeta> scan(const QStringList& dirs);

// scan()'s last step: in UniqueID order, a name already taken gets " (<UniqueID>)"; then sorted by name
void finish_names(std::vector<EffectMeta>& metas);

}  // namespace ladspa
}  // namespace amber

// One LADSPA plugin, found by the startup scan (effectloaders.cpp), as an audio effect: a row per control input port
// (field id = port index), processed per stream with StreamStates
class LadspaEffect : public Effect {
  Q_OBJECT
 public:
  LadspaEffect(Clip* c, const EffectMeta* em);

  void process_audio(double timecode_start, double timecode_end, quint8* samples, int nb_bytes,
                     int channel_count) override;

  // The library, held by the effect and by each running instance so it stays loaded until the last one is gone
  struct Plugin {
    QLibrary library;
    const LADSPA_Descriptor* descriptor = nullptr;
    std::vector<unsigned long> audio_in;   // 1 or 2 port indices, left first
    std::vector<unsigned long> audio_out;  // as many
    ~Plugin();
  };

 private:
  struct Control {
    unsigned long port;
    EffectField* field;
    amber::ladspa::PortRange range;
  };

  // One stream's plugin instances: one handle for a stereo plugin, one per channel for a mono one
  struct Instance {
    std::shared_ptr<Plugin> plugin;
    LADSPA_Handle handles[2] = {nullptr, nullptr};
    int handle_count = 0;
    int rate = 0;
    std::vector<LADSPA_Data> ports[2];  // every port's control value per handle (audio ports unused)
    std::vector<float> in[2];
    std::vector<float> out[2];
    // (Re)creates the handles at rate and connects them; false if the plugin refuses
    Instance() = default;
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    bool instantiate(const std::shared_ptr<Plugin>& p, int sample_rate);
    void restart();  // deactivate + activate, or a fresh instance for a plugin without activate()
    void release();
    ~Instance() { release(); }
  };

  std::shared_ptr<Plugin> plugin_;
  std::vector<Control> controls_;
  amber::StreamStates<Instance> states_;
};

#endif  // NOLADSPA

#endif  // LADSPAEFFECT_H
