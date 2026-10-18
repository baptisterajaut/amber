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

#ifndef REVERBEFFECT_H
#define REVERBEFFECT_H

#include "core/audiodsp.h"
#include "effects/effect.h"
#include "effects/internal/audiostreamstate.h"

class ReverbEffect : public Effect {
  Q_OBJECT
 public:
  ReverbEffect(Clip* c, const EffectMeta* em);
  void process_audio(double timecode_start, double timecode_end, quint8* samples, int nb_bytes,
                     int channel_count) override;

 private:
  DoubleField* room_val;
  DoubleField* damping_val;
  DoubleField* width_val;
  DoubleField* mix_val;

  amber::StreamStates<amber::audiodsp::Reverb> states_;
};

#endif  // REVERBEFFECT_H
