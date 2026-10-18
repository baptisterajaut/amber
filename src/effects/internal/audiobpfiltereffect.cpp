#include "audiobpfiltereffect.h"
#include <algorithm>
#include <cmath>
#include "rendering/audio.h"

AudioBpFilterEffect::AudioBpFilterEffect(Clip* c, const EffectMeta* em) : Effect(c, em) {
  EffectRow* low_cut_row = new EffectRow(this, tr("Low Cut (Hz)"));
  low_cut_val = new DoubleField(low_cut_row, "low_cut");
  low_cut_val->SetMinimum(20);
  low_cut_val->SetDefault(100);
  low_cut_val->SetMaximum(500);

  EffectRow* high_cut_row = new EffectRow(this, tr("High Cut (Hz)"));
  high_cut_val = new DoubleField(high_cut_row, "high_cut");
  high_cut_val->SetMinimum(2000);
  high_cut_val->SetDefault(4000);
  high_cut_val->SetMaximum(16000);

  EffectRow* slope_row = new EffectRow(this, tr("Slope"));
  slope_val = new DoubleField(slope_row, "slope_steepness");
  slope_val->SetMinimum(1);  // 1 = 24 dB/oct (1 stage)
  slope_val->SetDefault(1);
  slope_val->SetMaximum(3);  // 3 = 72 dB/oct (3 stages cascaded)

  EffectRow* amount_row = new EffectRow(this, tr("Mix"));
  amount_val = new DoubleField(amount_row, "amount");
  amount_val->SetMinimum(0);
  amount_val->SetDefault(100);
  amount_val->SetMaximum(100);

  L_hp_states.resize(6);
  R_hp_states.resize(6);
  L_lp_states.resize(6);
  R_lp_states.resize(6);
}

AudioBpFilterEffect::~AudioBpFilterEffect() {}

void AudioBpFilterEffect::reset_state() {
  std::fill(L_hp_states.begin(), L_hp_states.end(), SvfState{});
  std::fill(R_hp_states.begin(), R_hp_states.end(), SvfState{});
  std::fill(L_lp_states.begin(), L_lp_states.end(), SvfState{});
  std::fill(R_lp_states.begin(), R_lp_states.end(), SvfState{});
}

void AudioBpFilterEffect::process_audio(double timecode_start, double timecode_end, quint8* samples, int nb_bytes,
                                        int) {
  int total_frames = nb_bytes / 4;
  if (total_frames <= 0) return;

  // A non-matching timecode means playback jumped (seek/scrub) since the
  // last call: the filter's history no longer corresponds to what's about
  // to be fed in.
  const double kEpsilon = 1e-9;
  if (expected_next_timecode_ >= 0.0 && std::abs(timecode_start - expected_next_timecode_) > kEpsilon) {
    reset_state();
  }
  expected_next_timecode_ = timecode_end;

  double mix_factor = amount_val->GetDoubleAt(timecode_start) * 0.01;
  double low_cut_f = low_cut_val->GetDoubleAt(timecode_start);
  double high_cut_f = high_cut_val->GetDoubleAt(timecode_start);
  int filter_stages = static_cast<int>(slope_val->GetDoubleAt(timecode_start));
  double sample_rate = current_audio_freq();
  if (sample_rate <= 0.0) sample_rate = 48000.0;  // fallback if unavailable

  // To keep a flat Butterworth response across multi-stage filters, each
  // logical stage needs a unique damping coefficient instead of a fixed
  // 0.7071.
  std::vector<double> q_factors(3, 0.7071);
  if (filter_stages == 2) {
    q_factors[0] = 0.5412;
    q_factors[1] = 1.3065;
  } else if (filter_stages == 3) {
    q_factors[0] = 0.5176;
    q_factors[1] = 0.7071;
    q_factors[2] = 1.9319;
  }

  std::vector<double> left_orig(total_frames);
  std::vector<double> right_orig(total_frames);
  std::vector<double> left_proc(total_frames);
  std::vector<double> right_proc(total_frames);

  for (int i = 0; i < total_frames; ++i) {
    int byte_idx = i * 4;
    qint16 left_sh = static_cast<qint16>(((samples[byte_idx + 1] & 0xFF) << 8) | (samples[byte_idx] & 0xFF));
    qint16 right_sh = static_cast<qint16>(((samples[byte_idx + 3] & 0xFF) << 8) | (samples[byte_idx + 2] & 0xFF));
    left_orig[i] = left_sh / 32768.0;
    right_orig[i] = right_sh / 32768.0;
    left_proc[i] = left_orig[i];
    right_proc[i] = right_orig[i];
  }

  // Forward-only (minimum-phase) cascade: since there's no backward pass,
  // each logical stage runs twice (filter_stages * 2) to approximate the
  // slope a true zero-phase cascade would give.
  int total_forward_stages = filter_stages * 2;
  if (total_forward_stages > 6) total_forward_stages = 6;

  for (int stage = 0; stage < total_forward_stages; ++stage) {
    double Q = q_factors[stage / 2];

    double g_hp = std::tan(M_PI * low_cut_f / sample_rate);
    double k_hp = 1.0 / Q;
    double a1_hp = 1.0 / (1.0 + g_hp * (g_hp + k_hp));
    double a2_hp = g_hp * a1_hp;

    double g_lp = std::tan(M_PI * high_cut_f / sample_rate);
    double k_lp = 1.0 / Q;
    double a1_lp = 1.0 / (1.0 + g_lp * (g_lp + k_lp));
    double a2_lp = g_lp * a1_lp;

    SvfState& l_hp = L_hp_states[stage];
    SvfState& r_hp = R_hp_states[stage];
    SvfState& l_lp = L_lp_states[stage];
    SvfState& r_lp = R_lp_states[stage];

    for (int i = 0; i < total_frames; ++i) {
      double v3_lh = left_proc[i] - l_hp.ic2eq;
      double v1_lh = a1_hp * l_hp.ic1eq + a2_hp * v3_lh;
      double v2_lh = l_hp.ic2eq + g_hp * v1_lh;
      double hp_out_l = left_proc[i] - k_hp * v1_lh - v2_lh;
      l_hp.ic1eq = 2.0 * v1_lh - l_hp.ic1eq;
      l_hp.ic2eq = 2.0 * v2_lh - l_hp.ic2eq;

      double v3_ll = hp_out_l - l_lp.ic2eq;
      double v1_ll = a1_lp * l_lp.ic1eq + a2_lp * v3_ll;
      double v2_ll = l_lp.ic2eq + g_lp * v1_ll;
      l_lp.ic1eq = 2.0 * v1_ll - l_lp.ic1eq;
      l_lp.ic2eq = 2.0 * v2_ll - l_lp.ic2eq;
      left_proc[i] = v2_ll;

      double v3_rh = right_proc[i] - r_hp.ic2eq;
      double v1_rh = a1_hp * r_hp.ic1eq + a2_hp * v3_rh;
      double v2_rh = r_hp.ic2eq + g_hp * v1_rh;
      double hp_out_r = right_proc[i] - k_hp * v1_rh - v2_rh;
      r_hp.ic1eq = 2.0 * v1_rh - r_hp.ic1eq;
      r_hp.ic2eq = 2.0 * v2_rh - r_hp.ic2eq;

      double v3_rl = hp_out_r - r_lp.ic2eq;
      double v1_rl = a1_lp * r_lp.ic1eq + a2_lp * v3_rl;
      double v2_rl = r_lp.ic2eq + g_lp * v1_rl;
      r_lp.ic1eq = 2.0 * v1_rl - r_lp.ic1eq;
      r_lp.ic2eq = 2.0 * v2_rl - r_lp.ic2eq;
      right_proc[i] = v2_rl;
    }
  }

  for (int i = 0; i < total_frames; ++i) {
    int byte_idx = i * 4;

    double out_l = (left_proc[i] * mix_factor) + (left_orig[i] * (1.0 - mix_factor));
    double out_r = (right_proc[i] * mix_factor) + (right_orig[i] * (1.0 - mix_factor));

    out_l = std::max(-1.0, std::min(1.0, out_l)) * 32767.0;
    out_r = std::max(-1.0, std::min(1.0, out_r)) * 32767.0;

    qint16 final_l = static_cast<qint16>(out_l);
    qint16 final_r = static_cast<qint16>(out_r);

    samples[byte_idx + 3] = static_cast<quint8>(final_r >> 8);
    samples[byte_idx + 2] = static_cast<quint8>(final_r);
    samples[byte_idx + 1] = static_cast<quint8>(final_l >> 8);
    samples[byte_idx] = static_cast<quint8>(final_l);
  }
}
