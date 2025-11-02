#pragma once
#include <vector>
#include <cmath>

class SynthVoice {
public:
    void set_sample_rate(double sr) { sr_ = sr; }
    void set_freq(float hz) { freq_ = hz; }
    void render(float* out, unsigned nframes);
    
    enum Wave { Saw=0, Square=1, Sine=2 };
    void set_wave(int w) { wave_ = w; }
    void set_cutoff(float hz) { cutoff_ = hz; }   // placeholder for later filter
    void set_resonance(float r) { resonance_ = r; } // placeholder

private:
    double sr_ = 48000.0;
    float phase_ = 0.0f;
    float freq_ = 220.0f;

    int wave_ = 0;
    float cutoff_ = 1200.0f;
    float resonance_ = 0.3f;

};

