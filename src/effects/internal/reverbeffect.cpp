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

#include "reverbeffect.h"

#include <vector>

ReverbEffect::ReverbEffect(Clip* c, const EffectMeta* em) : Effect(c, em) {
  room_val = new DoubleField(new EffectRow(this, tr("Room Size (%)")), "room");
  room_val->SetMinimum(0);
  room_val->SetMaximum(100);
  room_val->SetDefault(50);

  damping_val = new DoubleField(new EffectRow(this, tr("Damping (%)")), "damping");
  damping_val->SetMinimum(0);
  damping_val->SetMaximum(100);
  damping_val->SetDefault(50);

  width_val = new DoubleField(new EffectRow(this, tr("Width (%)")), "width");
  width_val->SetMinimum(0);
  width_val->SetMaximum(100);
  width_val->SetDefault(100);

  mix_val = new DoubleField(new EffectRow(this, tr("Mix (%)")), "mix");
  mix_val->SetMinimum(0);
  mix_val->SetMaximum(100);
  mix_val->SetDefault(25);
}

void ReverbEffect::process_audio(double timecode_start, double, quint8* samples, int nb_bytes, int) {
  const int frames = nb_bytes / 4;
  if (frames <= 0) return;

  // CBA: parameters per chunk; per-sample interpolation if zipper noise shows up
  const double mix = mix_val->GetDoubleAt(timecode_start) * 0.01;
  // Bit-exact passthrough. The tank doesn't run meanwhile, so raising the mix again starts from silence.
  if (mix <= 0) return;

  const amber::AudioProcessContext& ctx = amber::current_audio_context();
  bool reset;
  std::shared_ptr<amber::audiodsp::Reverb> reverb = states_.acquire(ctx, timecode_start, frames, &reset);
  // A fresh stream (or a seek) starts with an empty tank: no tail of what played before the jump. The tail is cut at
  // the clip's end, like Delay's echoes (effects run on the clip's own samples).
  if (reset) reverb->reset(ctx.sample_rate);
  reverb->set(room_val->GetDoubleAt(timecode_start) * 0.01, damping_val->GetDoubleAt(timecode_start) * 0.01,
              width_val->GetDoubleAt(timecode_start) * 0.01, mix);

  std::vector<float> buf(size_t(frames) * 2);
  amber::audiodsp::s16_to_float(samples, buf.data(), frames);
  reverb->process(buf.data(), frames);
  amber::audiodsp::float_to_s16(buf.data(), samples, frames);
}
