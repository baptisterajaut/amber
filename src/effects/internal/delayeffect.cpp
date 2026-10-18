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

#include "delayeffect.h"

#include <vector>

DelayEffect::DelayEffect(Clip* c, const EffectMeta* em) : Effect(c, em) {
  time_val = new DoubleField(new EffectRow(this, tr("Time (ms)")), "time");
  time_val->SetMinimum(1);
  time_val->SetMaximum(2000);
  time_val->SetDefault(250);

  feedback_val = new DoubleField(new EffectRow(this, tr("Feedback (%)")), "feedback");
  feedback_val->SetMinimum(0);
  feedback_val->SetMaximum(90);
  feedback_val->SetDefault(30);

  mix_val = new DoubleField(new EffectRow(this, tr("Mix (%)")), "mix");
  mix_val->SetMinimum(0);
  mix_val->SetMaximum(100);
  mix_val->SetDefault(30);
}

void DelayEffect::process_audio(double timecode_start, double, quint8* samples, int nb_bytes, int) {
  const int frames = nb_bytes / 4;
  if (frames <= 0) return;

  const amber::AudioProcessContext& ctx = amber::current_audio_context();
  bool reset;
  std::shared_ptr<amber::audiodsp::DelayLine> line = states_.acquire(ctx, timecode_start, frames, &reset);
  // A fresh stream (or a seek) starts with an empty buffer: no echo of what played before the jump
  if (reset) line->reset(ctx.sample_rate);
  // CBA: parameters per chunk; per-sample interpolation if zipper noise shows up
  line->set(time_val->GetDoubleAt(timecode_start), feedback_val->GetDoubleAt(timecode_start) * 0.01,
            mix_val->GetDoubleAt(timecode_start) * 0.01, ctx.sample_rate);

  std::vector<float> buf(size_t(frames) * 2);
  amber::audiodsp::s16_to_float(samples, buf.data(), frames);
  line->process(buf.data(), frames);
  amber::audiodsp::float_to_s16(buf.data(), samples, frames);
}
