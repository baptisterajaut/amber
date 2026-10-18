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

#include "compressoreffect.h"

#include <vector>

namespace {

DoubleField* add_field(Effect* e, const QString& label, const QString& id, double min, double max, double def) {
  DoubleField* f = new DoubleField(new EffectRow(e, label), id);
  f->SetMinimum(min);
  f->SetMaximum(max);
  f->SetDefault(def);
  return f;
}

}  // namespace

CompressorEffect::CompressorEffect(Clip* c, const EffectMeta* em) : Effect(c, em) {
  threshold_val = add_field(this, tr("Threshold (dB)"), "threshold", -60, 0, -20);
  ratio_val = add_field(this, tr("Ratio"), "ratio", 1, 20, 4);
  attack_val = add_field(this, tr("Attack (ms)"), "attack", 0.1, 100, 10);
  release_val = add_field(this, tr("Release (ms)"), "release", 10, 1000, 100);
  makeup_val = add_field(this, tr("Makeup (dB)"), "makeup", 0, 24, 0);
}

void CompressorEffect::process_audio(double timecode_start, double, quint8* samples, int nb_bytes, int) {
  const int frames = nb_bytes / 4;
  if (frames <= 0) return;

  const amber::AudioProcessContext& ctx = amber::current_audio_context();
  bool reset;
  std::shared_ptr<amber::audiodsp::Compressor> comp = states_.acquire(ctx, timecode_start, frames, &reset);
  // CBA: parameters per chunk; per-sample interpolation if zipper noise shows up
  comp->set(threshold_val->GetDoubleAt(timecode_start), ratio_val->GetDoubleAt(timecode_start),
            attack_val->GetDoubleAt(timecode_start), release_val->GetDoubleAt(timecode_start),
            makeup_val->GetDoubleAt(timecode_start), ctx.sample_rate);

  std::vector<float> buf(size_t(frames) * 2);
  amber::audiodsp::s16_to_float(samples, buf.data(), frames);
  comp->process(buf.data(), frames);
  amber::audiodsp::float_to_s16(buf.data(), samples, frames);
}
