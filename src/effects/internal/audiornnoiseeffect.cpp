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
#include <rnnoise.h>
#include <QMutexLocker>
#include <QtMath>
#include <QtNumeric>
#include <cmath>
#include "rendering/audio.h"

namespace {
// RNNoise's model is fixed at 480-sample (10 ms at 48 kHz) frames.
constexpr int kFrameSize = 480;
}  // namespace

AudioRNNoiseEffect::AudioRNNoiseEffect(Clip* c, const EffectMeta* em) : Effect(c, em) {
  EffectRow* amount_row = new EffectRow(this, tr("Percentage"));
  rnn_val = new DoubleField(amount_row, "rnn_val");
  rnn_val->SetMinimum(0);
  rnn_val->SetDefault(70);
  rnn_val->SetMaximum(100);

  // Instantiates one instance of the RNN model per channel and primes the buffers
  reset_state();
}

AudioRNNoiseEffect::~AudioRNNoiseEffect() {
  if (rnnoise_left_) rnnoise_destroy(rnnoise_left_);
  if (rnnoise_right_) rnnoise_destroy(rnnoise_right_);
}

void AudioRNNoiseEffect::reset_state() {
  if (rnnoise_left_) rnnoise_destroy(rnnoise_left_);
  if (rnnoise_right_) rnnoise_destroy(rnnoise_right_);
  rnnoise_left_ = rnnoise_create(nullptr);
  rnnoise_right_ = rnnoise_create(nullptr);

  left_accumulator_.clear();
  right_accumulator_.clear();

  // Prime the output queues with one block of silence so the FIFO always has
  // a whole block in hand. Without it the queue runs dry on any call that
  // asks for a sample count that isn't a multiple of 480, punching a short
  // gap of silence into the output on nearly every call.
  ready_left_queue_.assign(kFrameSize, 0.0f);
  ready_right_queue_.assign(kFrameSize, 0.0f);

  // rnnoise_process_frame() returns the denoised version of the *previous*
  // block (its analysis window spans the previous and current frames), so the
  // wet signal trails the input by one more block on top of the priming
  // above. The dry queue carries both delays, which puts it sample-for-sample
  // in step with the wet queue - mixing the two without that alignment would
  // comb-filter the result.
  dry_left_queue_.assign(kFrameSize * 2, 0.0f);
  dry_right_queue_.assign(kFrameSize * 2, 0.0f);

  expected_next_timecode_ = -1.0;
}

void AudioRNNoiseEffect::process_audio(double timecode_start, double timecode_end, quint8* samples, int nb_bytes, int) {
  QMutexLocker locker(&mutex_);

  // RNNoise's model is trained specifically for 48kHz audio; running it at
  // another rate would process the wrong frequency content entirely. Pass
  // audio through untouched rather than denoise at the wrong rate.
  if (current_audio_freq() != 48000) {
    return;
  }

  // A non-matching timecode means playback jumped (seek/scrub) since the
  // last call: the model's recurrent state and the accumulator/queue
  // buffers no longer correspond to what's about to be fed in.
  const double kEpsilon = 1e-9;
  if (expected_next_timecode_ >= 0.0 && std::abs(timecode_start - expected_next_timecode_) > kEpsilon) {
    reset_state();
  }
  expected_next_timecode_ = timecode_end;

  int total_frames = nb_bytes / 4;
  if (!rnnoise_left_ || !rnnoise_right_) return;

  // 1. Unpack incoming frames into the class-level accumulators
  for (int i = 0; i < total_frames; ++i) {
    int byte_idx = i * 4;
    qint16 left_short = static_cast<qint16>(((samples[byte_idx + 1] & 0xFF) << 8) | (samples[byte_idx] & 0xFF));
    qint16 right_short = static_cast<qint16>(((samples[byte_idx + 3] & 0xFF) << 8) | (samples[byte_idx + 2] & 0xFF));

    left_accumulator_.push_back(static_cast<float>(left_short));
    right_accumulator_.push_back(static_cast<float>(right_short));
  }

  // 2. Run the model on all complete 480-sample blocks, queuing output so
  // playback keeps up with the cacher's larger per-call request size.
  while (left_accumulator_.size() >= kFrameSize) {
    std::vector<float> denoised_left(kFrameSize);
    std::vector<float> denoised_right(kFrameSize);

    rnnoise_process_frame(rnnoise_left_, denoised_left.data(), left_accumulator_.data());
    rnnoise_process_frame(rnnoise_right_, denoised_right.data(), right_accumulator_.data());

    ready_left_queue_.insert(ready_left_queue_.end(), denoised_left.begin(), denoised_left.end());
    ready_right_queue_.insert(ready_right_queue_.end(), denoised_right.begin(), denoised_right.end());

    // Queue the same block untouched for the dry half of the mix.
    dry_left_queue_.insert(dry_left_queue_.end(), left_accumulator_.begin(), left_accumulator_.begin() + kFrameSize);
    dry_right_queue_.insert(dry_right_queue_.end(), right_accumulator_.begin(),
                            right_accumulator_.begin() + kFrameSize);

    left_accumulator_.erase(left_accumulator_.begin(), left_accumulator_.begin() + kFrameSize);
    right_accumulator_.erase(right_accumulator_.begin(), right_accumulator_.begin() + kFrameSize);
  }

  // 3. Write out exactly total_frames from the front of the FIFO queues,
  // blending the denoised signal against its time-aligned dry counterpart.
  // 0% leaves the audio as it came in (just delayed), 100% is fully denoised.
  const double mix = qBound(0.0, rnn_val->GetDoubleAt(timecode_start) * 0.01, 1.0);
  const float wet_gain = static_cast<float>(mix);
  const float dry_gain = static_cast<float>(1.0 - mix);

  int consumed = 0;
  for (int i = 0; i < total_frames; ++i) {
    int byte_idx = i * 4;

    if (consumed >= static_cast<int>(ready_left_queue_.size())) {
      // Unreachable: the priming in reset_state() keeps a block in reserve,
      // so the queue holds more than total_frames by the time we get here.
      // Leave the remaining samples as they came in rather than emit a gap.
      break;
    }

    float out_l = ready_left_queue_[consumed] * wet_gain + dry_left_queue_[consumed] * dry_gain;
    float out_r = ready_right_queue_[consumed] * wet_gain + dry_right_queue_[consumed] * dry_gain;
    ++consumed;

    out_l = std::max(-32768.0f, std::min(32767.0f, out_l));
    out_r = std::max(-32768.0f, std::min(32767.0f, out_r));

    qint16 final_left = static_cast<qint16>(out_l);
    qint16 final_right = static_cast<qint16>(out_r);

    samples[byte_idx + 3] = static_cast<quint8>(final_right >> 8);
    samples[byte_idx + 2] = static_cast<quint8>(final_right);
    samples[byte_idx + 1] = static_cast<quint8>(final_left >> 8);
    samples[byte_idx] = static_cast<quint8>(final_left);
  }

  // Drop the consumed samples in one go - erasing from the front per sample
  // would be quadratic in the queue length on every call.
  ready_left_queue_.erase(ready_left_queue_.begin(), ready_left_queue_.begin() + consumed);
  ready_right_queue_.erase(ready_right_queue_.begin(), ready_right_queue_.begin() + consumed);
  dry_left_queue_.erase(dry_left_queue_.begin(), dry_left_queue_.begin() + consumed);
  dry_right_queue_.erase(dry_right_queue_.begin(), dry_right_queue_.begin() + consumed);
}
