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

#include "parametriceqeffect.h"

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

ParametricEQEffect::ParametricEQEffect(Clip* c, const EffectMeta* em) : Effect(c, em) {
  low_freq_val = add_field(this, tr("Low Frequency (Hz)"), "low_freq", 20, 1000, 100);
  low_gain_val = add_field(this, tr("Low Gain (dB)"), "low_gain", -18, 18, 0);
  mid_freq_val = add_field(this, tr("Mid Frequency (Hz)"), "mid_freq", 100, 10000, 1000);
  mid_gain_val = add_field(this, tr("Mid Gain (dB)"), "mid_gain", -18, 18, 0);
  mid_q_val = add_field(this, tr("Mid Q"), "mid_q", 0.1, 10, 1);
  high_freq_val = add_field(this, tr("High Frequency (Hz)"), "high_freq", 1000, 20000, 8000);
  high_gain_val = add_field(this, tr("High Gain (dB)"), "high_gain", -18, 18, 0);
}

void ParametricEQEffect::process_audio(double timecode_start, double, quint8* samples, int nb_bytes, int) {
  const int frames = nb_bytes / 4;
  if (frames <= 0) return;

  // CBA: parameters per chunk; per-sample interpolation if zipper noise shows up
  const double low_gain = low_gain_val->GetDoubleAt(timecode_start);
  const double mid_gain = mid_gain_val->GetDoubleAt(timecode_start);
  const double high_gain = high_gain_val->GetDoubleAt(timecode_start);
  if (low_gain == 0 && mid_gain == 0 && high_gain == 0) return;

  const amber::AudioProcessContext& ctx = amber::current_audio_context();
  bool reset;
  std::shared_ptr<Bands> bands = states_.acquire(ctx, timecode_start, frames, &reset);
  if (reset) {
    bands->low.reset();
    bands->mid.reset();
    bands->high.reset();
  }

  std::vector<float> buf(size_t(frames) * 2);
  amber::audiodsp::s16_to_float(samples, buf.data(), frames);
  // A band switched to 0 dB drops its memory, so switching it back on starts clean
  if (low_gain != 0) {
    bands->low.set(amber::audiodsp::low_shelf(low_freq_val->GetDoubleAt(timecode_start), low_gain, ctx.sample_rate));
    bands->low.process(buf.data(), frames);
  } else {
    bands->low.reset();
  }
  if (mid_gain != 0) {
    bands->mid.set(amber::audiodsp::peaking(mid_freq_val->GetDoubleAt(timecode_start), mid_gain,
                                            mid_q_val->GetDoubleAt(timecode_start), ctx.sample_rate));
    bands->mid.process(buf.data(), frames);
  } else {
    bands->mid.reset();
  }
  if (high_gain != 0) {
    bands->high.set(
        amber::audiodsp::high_shelf(high_freq_val->GetDoubleAt(timecode_start), high_gain, ctx.sample_rate));
    bands->high.process(buf.data(), frames);
  } else {
    bands->high.reset();
  }
  amber::audiodsp::float_to_s16(buf.data(), samples, frames);
}
