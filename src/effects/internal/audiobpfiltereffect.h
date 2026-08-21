#ifndef AUDIOBPFILTEREFFECT_H
#define AUDIOBPFILTEREFFECT_H

#include "effects/effect.h"
#include <vector>

// Much more stable state tracker for Chamberlin/Simper SVF filters
struct SvfState {
    double ic1eq = 0.0; // Internal capacitor 1 state
    double ic2eq = 0.0; // Internal capacitor 2 state
};

// Simple structure to hold individual biquad delay history (x=input, y=output)
struct BiquadState {
    double x1 = 0.0, x2 = 0.0;
    double y1 = 0.0, y2 = 0.0;
};

class AudioBpFilterEffect : public Effect {
public:
    AudioBpFilterEffect(Clip* c, const EffectMeta *em);
    virtual ~AudioBpFilterEffect();

    void process_audio(double timecode_start, double timecode_end, quint8 *samples, int nb_bytes, int) override;

private:
    DoubleField* low_cut_val;   // Low Cut (Hz)
    DoubleField* high_cut_val;  // High Cut (Hz)
    DoubleField* slope_val;     // Dynamic Slope Selector (24, 48, 72 dB)
    DoubleField* amount_val;    // Mix intensity field

    // Vector arrays to hold persistent stage history based on max supported order (up to 3 stages / 72dB)
    std::vector<SvfState> L_hp_states;
    std::vector<SvfState> R_hp_states;
    std::vector<SvfState> L_lp_states;
    std::vector<SvfState> R_lp_states;  
    
};

#endif // AUDIOBPFILTEREFFECT_H                                                                    