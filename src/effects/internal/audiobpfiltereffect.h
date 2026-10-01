/***

    Amber - Non-Linear Video Editor
    Copyright (C) 2026  Amber Team

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