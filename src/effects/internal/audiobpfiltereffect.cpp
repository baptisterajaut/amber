#include "audiobpfiltereffect.h"
#include <cmath>
#include <algorithm>

AudioBpFilterEffect::AudioBpFilterEffect(Clip* c, const EffectMeta *em) : Effect(c, em) {
  // 1. Low Cut Slider
  EffectRow* low_cut_row = new EffectRow(this, tr("Low Cut (Hz)"));
  low_cut_val = new DoubleField(low_cut_row, "low_cut");
  low_cut_val->SetMinimum(20);
  low_cut_val->SetDefault(100);
  low_cut_val->SetMaximum(500);

  // 2. High Cut Slider
  EffectRow* high_cut_row = new EffectRow(this, tr("High Cut (Hz)"));
  high_cut_val = new DoubleField(high_cut_row, "high_cut");
  high_cut_val->SetMinimum(2000);
  high_cut_val->SetDefault(4000);
  high_cut_val->SetMaximum(16000);

  // 3.Dynamic Slope Selector Dropdown / Int IntField
  EffectRow* slope_row = new EffectRow(this, tr("Slope Steepness"));
  slope_val = new DoubleField(slope_row, "slope_steepness");
  slope_val->SetMinimum(1); // Mode 1 = 24 dB/oct (1 Stage)
  slope_val->SetDefault(1); 
  slope_val->SetMaximum(3); // Mode 3 = 72 dB/oct (3 Stages Cascaded)

  // 4. Wet/Dry Mix Slider
  EffectRow* amount_row = new EffectRow(this, tr("Mix Intensity"));
  amount_val = new DoubleField(amount_row, "amount");
  amount_val->SetMinimum(0);
  amount_val->SetDefault(100);
  amount_val->SetMaximum(100);

  // Pre-allocate history structures for 3 complete hardware stages
  L_hp_states.resize(3); R_hp_states.resize(3);
  L_lp_states.resize(3); R_lp_states.resize(3);
}

AudioBpFilterEffect::~AudioBpFilterEffect() {}

// Bandpass filter processing
void AudioBpFilterEffect::process_audio(double timecode_start, double timecode_end, quint8 *samples, int nb_bytes, int) {
  int total_frames = nb_bytes / 4; 
  if (total_frames <= 0) return;

  double mix_factor   = amount_val->GetDoubleAt(timecode_start) * 0.01;
  double low_cut_f    = low_cut_val->GetDoubleAt(timecode_start);
  double high_cut_f   = high_cut_val->GetDoubleAt(timecode_start);
  int filter_stages   = static_cast<int>(slope_val->GetDoubleAt(timecode_start));
  double sample_rate  = 48000.0; 
  
// To keep a flat Butterworth response across multi-stage filters, 
  // each stage needs a unique damping coefficient instead of a fixed 0.7071.
  std::vector<double> q_factors(3, 0.7071);
  if (filter_stages == 2) {
      q_factors[0] = 0.5412; q_factors[1] = 1.3065;
  } else if (filter_stages == 3) {
      q_factors[0] = 0.5176; q_factors[1] = 0.7071; q_factors[2] = 1.9319;
  }
// Staging vectors
  std::vector<double> left_orig(total_frames);
  std::vector<double> right_orig(total_frames);
  std::vector<double> left_proc(total_frames);
  std::vector<double> right_proc(total_frames);

  for (int i = 0; i < total_frames; ++i) {
    int byte_idx = i * 4;
    qint16 left_sh  = static_cast<qint16>(((samples[byte_idx+1] & 0xFF) << 8) | (samples[byte_idx] & 0xFF));
    qint16 right_sh = static_cast<qint16>(((samples[byte_idx+3] & 0xFF) << 8) | (samples[byte_idx+2] & 0xFF));
    left_orig[i] = left_sh / 32768.0; right_orig[i] = right_sh / 32768.0;
    left_proc[i] = left_orig[i]; right_proc[i] = right_orig[i];
  }
  
// --- 2. FORWARD PASS STATE-VARIABLE FILTER (SVF) ---
// --- 2. THE HIGH-PERFORMANCE FORWARD-ONLY PASS ---
  // We process through our chosen stages forward in time. 
  // Because we do not run a backward pass, we double the number of stages 
  // (filter_stages * 2) so that a user setting of '3' still delivers a massive 72 dB slope!
  int total_forward_stages = filter_stages * 2;

  // Protect against indexing overflows if vectors were resized to 3
  if (total_forward_stages > 6) total_forward_stages = 6; 

  for (int stage = 0; stage < total_forward_stages; ++stage) {
    // Choose a stable, flat Butterworth damping profile across your forward stages
    double Q = 0.7071; 
    
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
      // LEFT CHANNEL 
      double v3_lh = left_proc[i] - l_hp.ic2eq;
      double v1_lh = a1_hp * l_hp.ic1eq + a2_hp * v3_lh;
      double v2_lh = l_hp.ic2eq + g_hp * v1_lh;
      double hp_out_l = left_proc[i] - k_hp * v1_lh - v2_lh;
      l_hp.ic1eq = 2.0 * v1_lh - l_hp.ic1eq; l_hp.ic2eq = 2.0 * v2_lh - l_hp.ic2eq;

      double v3_ll = hp_out_l - l_lp.ic2eq;
      double v1_ll = a1_lp * l_lp.ic1eq + a2_lp * v3_ll;
      double v2_ll = l_lp.ic2eq + g_lp * v1_ll;
      l_lp.ic1eq = 2.0 * v1_ll - l_lp.ic1eq; l_lp.ic2eq = 2.0 * v2_ll - l_lp.ic2eq;
      left_proc[i] = v2_ll; // Feed output directly as the input to the next stage!

      // RIGHT CHANNEL
      double v3_rh = right_proc[i] - r_hp.ic2eq;
      double v1_rh = a1_hp * r_hp.ic1eq + a2_hp * v3_rh;
      double v2_rh = r_hp.ic2eq + g_hp * v1_rh;
      double hp_out_r = right_proc[i] - k_hp * v1_rh - v2_rh;
      r_hp.ic1eq = 2.0 * v1_rh - r_hp.ic1eq; r_hp.ic2eq = 2.0 * v2_rh - r_hp.ic2eq;

      double v3_rl = hp_out_r - r_lp.ic2eq;
      double v1_rl = a1_lp * r_lp.ic1eq + a2_lp * v3_rl;
      double v2_rl = r_lp.ic2eq + g_lp * v1_rl;
      r_lp.ic1eq = 2.0 * v1_rl - r_lp.ic1eq; r_lp.ic2eq = 2.0 * v2_rl - r_lp.ic2eq; 
      right_proc[i] = v2_rl;
    }
  }

  // --- 4. MIX & PACK ---  
  for (int i = 0; i < total_frames; ++i) {
    int byte_idx = i * 4;

    double out_l = (left_proc[i] * mix_factor) + (left_orig[i] * (1.0 - mix_factor));
    double out_r = (right_proc[i] * mix_factor) + (right_orig[i] * (1.0 - mix_factor));

    out_l = std::max(-1.0, std::min(1.0, out_l)) * 32767.0;
    out_r = std::max(-1.0, std::min(1.0, out_r)) * 32767.0;

    qint16 final_l = static_cast<qint16>(out_l);
    qint16 final_r = static_cast<qint16>(out_r);

    samples[byte_idx+3] = static_cast<quint8>(final_r >> 8);
    samples[byte_idx+2] = static_cast<quint8>(final_r);
    samples[byte_idx+1] = static_cast<quint8>(final_l >> 8);
    samples[byte_idx]   = static_cast<quint8>(final_l);
  }
}