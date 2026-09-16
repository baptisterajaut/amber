/***

    Amber - Non-Linear Video Editor
    Copyright (C) 2026  Amber Team

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

#ifndef AUDIORNNOISEEFFECT_H
#define AUDIORNNOISEEFFECT_H

#include <rnnoise.h> // Include the official RNNoise API header
#include <vector>
#include "effects/effect.h"

class AudioRNNoiseEffect : public Effect {
  Q_OBJECT
  
public:
  AudioRNNoiseEffect(Clip* c, const EffectMeta* em);
  virtual ~AudioRNNoiseEffect(); // Crucial for preventing memory leaks  

  void process_audio(double timecode_start, double timecode_end, quint8* samples, int nb_bytes, int channel_count) override;

  DoubleField* rnn_val;
private:
    // Pointers to individual RNNoise neural network model states
    DenoiseState* rnnoise_left_ = nullptr;
    DenoiseState* rnnoise_right_ = nullptr;

    // Buffers to accumulate exactly 480 float samples for each channel
    std::vector<float> left_accumulator_;
    std::vector<float> right_accumulator_;
    std::vector<float> ready_left_queue_;
    std::vector<float> ready_right_queue_;    
};

#endif // AUDIONOISEEFFECT_H
