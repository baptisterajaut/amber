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

#include "choruseffect.h"

#include <vector>

ChorusEffect::ChorusEffect(Clip* c, const EffectMeta* em) : Effect(c, em) {
  rate_val = new DoubleField(new EffectRow(this, tr("Rate (Hz)")), "rate");
  rate_val->SetMinimum(0.1);
  rate_val->SetMaximum(5);
  rate_val->SetDefault(1);

  depth_val = new DoubleField(new EffectRow(this, tr("Depth (ms)")), "depth");
  depth_val->SetMinimum(0);
  depth_val->SetMaximum(amber::audiodsp::Chorus::kMaxDepthMs);
  depth_val->SetDefault(3);

  mix_val = new DoubleField(new EffectRow(this, tr("Mix (%)")), "mix");
  mix_val->SetMinimum(0);
  mix_val->SetMaximum(100);
  mix_val->SetDefault(50);
}

void ChorusEffect::process_audio(double timecode_start, double, quint8* samples, int nb_bytes, int) {
  const int frames = nb_bytes / 4;
  if (frames <= 0) return;

  // CBA: parameters per chunk; per-sample interpolation if zipper noise shows up
  const double mix = mix_val->GetDoubleAt(timecode_start) * 0.01;
  if (mix <= 0) return;  // bit-exact passthrough

  const amber::AudioProcessContext& ctx = amber::current_audio_context();
  bool reset;
  std::shared_ptr<amber::audiodsp::Chorus> chorus = states_.acquire(ctx, timecode_start, frames, &reset);
  // A fresh stream (or a seek) starts with an empty line and the LFO at 0
  if (reset) chorus->reset(ctx.sample_rate);
  chorus->set(rate_val->GetDoubleAt(timecode_start), depth_val->GetDoubleAt(timecode_start), mix, ctx.sample_rate);

  std::vector<float> buf(size_t(frames) * 2);
  amber::audiodsp::s16_to_float(samples, buf.data(), frames);
  chorus->process(buf.data(), frames);
  amber::audiodsp::float_to_s16(buf.data(), samples, frames);
}
