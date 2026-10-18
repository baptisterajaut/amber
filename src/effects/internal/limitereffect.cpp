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

#include "limitereffect.h"

#include <vector>

LimiterEffect::LimiterEffect(Clip* c, const EffectMeta* em) : Effect(c, em) {
  ceiling_val = new DoubleField(new EffectRow(this, tr("Ceiling (dB)")), "ceiling");
  ceiling_val->SetMinimum(-24);
  ceiling_val->SetMaximum(0);
  ceiling_val->SetDefault(-1);

  release_val = new DoubleField(new EffectRow(this, tr("Release (ms)")), "release");
  release_val->SetMinimum(1);
  release_val->SetMaximum(1000);
  release_val->SetDefault(50);
}

void LimiterEffect::process_audio(double timecode_start, double, quint8* samples, int nb_bytes, int) {
  const int frames = nb_bytes / 4;
  if (frames <= 0) return;

  const amber::AudioProcessContext& ctx = amber::current_audio_context();
  bool reset;
  std::shared_ptr<amber::audiodsp::Limiter> limiter = states_.acquire(ctx, timecode_start, frames, &reset);
  if (reset) limiter->reset();
  // CBA: parameters per chunk; per-sample interpolation if zipper noise shows up
  limiter->set(ceiling_val->GetDoubleAt(timecode_start), release_val->GetDoubleAt(timecode_start), ctx.sample_rate);

  std::vector<float> buf(size_t(frames) * 2);
  amber::audiodsp::s16_to_float(samples, buf.data(), frames);
  limiter->process(buf.data(), frames);
  amber::audiodsp::float_to_s16(buf.data(), samples, frames);
}
