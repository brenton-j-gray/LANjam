#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

class SynthVoice {
public:
    void set_sample_rate(double sr) { sr_ = sr; coeffDirty_ = true; }
    void set_freq(float hz) { freq_ = hz; }
    void render(float* out, unsigned nframes);
    
    enum Wave { Saw=0, Square=1, Sine=2 };
    enum FilterType { Low=0, Band=1, High=2 };

    void set_osc_wave(int index, int w);
    void set_osc_octave(int index, int semitones);
    void set_osc_detune(int index, float cents);
    void set_osc_phase(int index, float degrees);

    void set_cutoff(float hz) { cutoff_ = hz; coeffDirty_ = true; }
    void set_resonance(float r) { resonance_ = r; coeffDirty_ = true; }
    void set_filter_type(int type) { filterType_ = static_cast<FilterType>(std::clamp(type, 0, 2)); coeffDirty_ = true; }
    void set_filter_slope(int stages) { filterStages_ = std::clamp(stages, 1, 4); coeffDirty_ = true; }

    static void computeCoefficients(FilterType type, float cutoff, float q, double sr,
                                    float& b0, float& b1, float& b2, float& a1, float& a2);

private:
    struct StageState {
        float x1 = 0.0f;
        float x2 = 0.0f;
        float y1 = 0.0f;
        float y2 = 0.0f;
    };

    void updateCoefficients();

    static constexpr int kNumOsc = 3;

    double sr_ = 48000.0;
    float freq_ = 220.0f;

    std::array<float, kNumOsc> oscPhase_{};
    std::array<int,   kNumOsc> oscWave_{{0,0,0}};
    std::array<int,   kNumOsc> oscOctave_{{0,0,0}}; // semitone offsets
    std::array<float, kNumOsc> oscDetune_{{0.0f,0.0f,0.0f}}; // cents
    std::array<float, kNumOsc> oscPhaseOffset_{{0.0f,0.0f,0.0f}}; // 0..1

    float cutoff_ = 1200.0f;
    float resonance_ = 0.7f;
    FilterType filterType_ = FilterType::Low;
    int filterStages_ = 1;

    // ADSR envelope
    float envAttack_ = 0.01f;   // seconds
    float envDecay_ = 0.1f;     // seconds
    float envSustain_ = 0.8f;   // level 0..1
    float envRelease_ = 0.2f;   // seconds
    enum EnvStage { EnvIdle=0, EnvAttack=1, EnvDecay=2, EnvSustain=3, EnvRelease=4 };
    EnvStage envStage_ = EnvIdle;
    float envLevel_ = 0.0f;     // current envelope level 0..1
    // per-sample increment used during attack/decay/release
    float envInc_ = 0.0f;

public:
    // Envelope control
    void set_env_attack(float s) { envAttack_ = std::max(0.0f, s); }
    void set_env_decay(float s) { envDecay_ = std::max(0.0f, s); }
    void set_env_sustain(float s) { envSustain_ = std::clamp(s, 0.0f, 1.0f); }
    void set_env_release(float s) { envRelease_ = std::max(0.0f, s); }
    void note_on();
    void note_off();
    // Returns true if the voice is currently producing sound (envelope not idle)
    bool is_active() const;

    bool coeffDirty_ = true;
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;
    std::array<StageState, 4> stages_{};
};
