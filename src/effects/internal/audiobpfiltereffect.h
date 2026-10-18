#ifndef AUDIOBPFILTEREFFECT_H
#define AUDIOBPFILTEREFFECT_H

#include <vector>
#include "effects/effect.h"

// State tracker for Chamberlin/Simper SVF filters
struct SvfState {
  double ic1eq = 0.0;  // Internal capacitor 1 state
  double ic2eq = 0.0;  // Internal capacitor 2 state
};

class AudioBpFilterEffect : public Effect {
 public:
  AudioBpFilterEffect(Clip* c, const EffectMeta* em);
  virtual ~AudioBpFilterEffect();

  void process_audio(double timecode_start, double timecode_end, quint8* samples, int nb_bytes, int) override;

 private:
  void reset_state();

  DoubleField* low_cut_val;   // Low Cut (Hz)
  DoubleField* high_cut_val;  // High Cut (Hz)
  DoubleField* slope_val;     // Slope (1 = 24dB, 2 = 48dB, 3 = 72dB)
  DoubleField* amount_val;    // Wet/dry mix

  // Persistent per-stage filter history, sized for the maximum supported
  // cascade (3 logical stages, doubled to 6 forward passes since this is a
  // forward-only, minimum-phase design rather than a zero-phase one).
  std::vector<SvfState> L_hp_states;
  std::vector<SvfState> R_hp_states;
  std::vector<SvfState> L_lp_states;
  std::vector<SvfState> R_lp_states;

  // Timecode (seconds) this instance expects the next process_audio() call
  // to start at. A mismatch means playback jumped (seek/scrub) since the
  // last call, so the filter history is reset rather than carrying audio
  // content from a different part of the clip into the new position.
  double expected_next_timecode_ = -1.0;
};

#endif  // AUDIOBPFILTEREFFECT_H
