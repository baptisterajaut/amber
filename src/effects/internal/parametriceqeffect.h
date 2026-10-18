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

#ifndef PARAMETRICEQEFFECT_H
#define PARAMETRICEQEFFECT_H

#include "core/audiodsp.h"
#include "effects/effect.h"
#include "effects/internal/audiostreamstate.h"

// Three-band EQ: low shelf, peaking mid, high shelf. A band at exactly 0 dB is skipped, so the defaults leave the
// samples untouched.
class ParametricEQEffect : public Effect {
  Q_OBJECT
 public:
  ParametricEQEffect(Clip* c, const EffectMeta* em);
  void process_audio(double timecode_start, double timecode_end, quint8* samples, int nb_bytes,
                     int channel_count) override;

 private:
  DoubleField* low_freq_val;
  DoubleField* low_gain_val;
  DoubleField* mid_freq_val;
  DoubleField* mid_gain_val;
  DoubleField* mid_q_val;
  DoubleField* high_freq_val;
  DoubleField* high_gain_val;

  struct Bands {
    amber::audiodsp::Biquad low, mid, high;
  };
  amber::StreamStates<Bands> states_;
};

#endif  // PARAMETRICEQEFFECT_H
