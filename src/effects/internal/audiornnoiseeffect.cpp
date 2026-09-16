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
#include "audiornnoiseeffect.h"
#include <iostream> // Add at the top for std::cout debugging
#include <QDateTime>
#include <QtMath>
#include <rnnoise.h>

AudioRNNoiseEffect::AudioRNNoiseEffect(Clip* c, const EffectMeta *em) : Effect(c, em) {
  EffectRow* amount_row = new EffectRow(this, tr("Percentage"));
  rnn_val = new DoubleField(amount_row, "rnn_val");
  rnn_val->SetMinimum(0);
  rnn_val->SetDefault(70);
  rnn_val->SetMaximum(100);
  
      // Instantiate separate instances of the RNN model for each channel
    rnnoise_left_ = rnnoise_create(nullptr);
    rnnoise_right_ = rnnoise_create(nullptr);

}
// Ensure you free the model memory when the effect is deleted from the timeline
AudioRNNoiseEffect::~AudioRNNoiseEffect() {
    if (rnnoise_left_)  rnnoise_destroy(rnnoise_left_);
    if (rnnoise_right_) rnnoise_destroy(rnnoise_right_);
}


void AudioRNNoiseEffect::process_audio(double timecode_start, double timecode_end, quint8 *samples, int nb_bytes, int) {

	int total_frames = nb_bytes / 4; 
  if (!rnnoise_left_ || !rnnoise_right_) return; 

  double mix_factor = rnn_val->GetDoubleAt(timecode_start) * 0.01; 

  // 1. Unpack incoming frames straight into the class-level storage buckets
  for (int i = 0; i < total_frames; ++i) {
    int byte_idx = i * 4;
    qint16 left_short  = static_cast<qint16>(((samples[byte_idx+1] & 0xFF) << 8) | (samples[byte_idx] & 0xFF));
    qint16 right_short = static_cast<qint16>(((samples[byte_idx+3] & 0xFF) << 8) | (samples[byte_idx+2] & 0xFF));

    left_accumulator_.push_back(static_cast<float>(left_short));
    right_accumulator_.push_back(static_cast<float>(right_short));
  }

  // 2. Run the AI on ALL available complete 480-sample blocks.
  // Instead of clearing 'ready_left' each time, push outputs to persistent class arrays.
  // Make sure to add 'std::vector<float> ready_left_queue_;' and 'ready_right_queue_;' 
  // into your protected/private variables inside 'audioseismicfiltereffect.h'!
  while (left_accumulator_.size() >= 480) {
      std::vector<float> denoised_left(480);
      std::vector<float> denoised_right(480);

      rnnoise_process_frame(rnnoise_left_,  denoised_left.data(),  left_accumulator_.data());
      rnnoise_process_frame(rnnoise_right_, denoised_right.data(), right_accumulator_.data());

      // Push into the long-term class queues
      ready_left_queue_.insert(ready_left_queue_.end(), denoised_left.begin(), denoised_left.end());
      ready_right_queue_.insert(ready_right_queue_.end(), denoised_right.begin(), denoised_right.end());

      left_accumulator_.erase(left_accumulator_.begin(), left_accumulator_.begin() + 480);
      right_accumulator_.erase(right_accumulator_.begin(), right_accumulator_.begin() + 480);
  }

  // Write out EXACTLY total_frames to Amber, pulling continuously from the FIFO queue
  for (int i = 0; i < total_frames; ++i) {
    int byte_idx = i * 4;

    // Read original audio as fallback
    qint16 left_short  = static_cast<qint16>(((samples[byte_idx+1] & 0xFF) << 8) | (samples[byte_idx] & 0xFF));
    qint16 right_short = static_cast<qint16>(((samples[byte_idx+3] & 0xFF) << 8) | (samples[byte_idx+2] & 0xFF));

    float out_l = left_short;
    float out_r = right_short;

    // If the long-term AI queue has samples ready, pull from the front
    if (!ready_left_queue_.empty()) {
        float ai_l = ready_left_queue_.front();
        float ai_r = ready_right_queue_.front();

        // Blend smooth wet/dry mix
        out_l = (ai_l * mix_factor) + (left_short * (1.0 - mix_factor));
        out_r = (ai_r * mix_factor) + (right_short * (1.0 - mix_factor));

        // Pop the sample out of the queue so the next frame reads the next value
        ready_left_queue_.erase(ready_left_queue_.begin());
        ready_right_queue_.erase(ready_right_queue_.begin());
    }

    out_l = std::max(-32768.0f, std::min(32767.0f, out_l));
    out_r = std::max(-32768.0f, std::min(32767.0f, out_r));

    qint16 final_left  = static_cast<qint16>(out_l);
    qint16 final_right = static_cast<qint16>(out_r);

    samples[byte_idx+3] = static_cast<quint8>(final_right >> 8);
    samples[byte_idx+2] = static_cast<quint8>(final_right);
    samples[byte_idx+1] = static_cast<quint8>(final_left >> 8);
    samples[byte_idx]   = static_cast<quint8>(final_left);
  }
}

