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

#ifndef COMPRESSOREFFECT_H
#define COMPRESSOREFFECT_H

#include "core/audiodsp.h"
#include "effects/effect.h"
#include "effects/internal/audiostreamstate.h"

class CompressorEffect : public Effect {
  Q_OBJECT
 public:
  CompressorEffect(Clip* c, const EffectMeta* em);
  void process_audio(double timecode_start, double timecode_end, quint8* samples, int nb_bytes,
                     int channel_count) override;

 private:
  DoubleField* threshold_val;
  DoubleField* ratio_val;
  DoubleField* attack_val;
  DoubleField* release_val;
  DoubleField* makeup_val;

  amber::StreamStates<amber::audiodsp::Compressor> states_;
};

#endif  // COMPRESSOREFFECT_H
