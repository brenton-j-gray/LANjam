#include "SynthVoice.h"

#include <cmath>
#include <algorithm>

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

void SynthVoice::set_osc_wave(int index, int w) {
  if (index < 0 || index >= kNumOsc) return;
  oscWave_[index] = std::clamp(w, 0, 2);
}

void SynthVoice::set_osc_octave(int index, int semitones) {
  if (index < 0 || index >= kNumOsc) return;
  oscOctave_[index] = std::clamp(semitones, -24, 24);
}

void SynthVoice::set_osc_detune(int index, float cents) {
  if (index < 0 || index >= kNumOsc) return;
  oscDetune_[index] = std::clamp(cents, -200.0f, 200.0f);
}

void SynthVoice::set_osc_phase(int index, float degrees) {
  if (index < 0 || index >= kNumOsc) return;
  float frac = std::fmod(degrees, 360.0f) / 360.0f;
  if (frac < 0.0f) frac += 1.0f;
  oscPhaseOffset_[index] = frac;
}

void SynthVoice::computeCoefficients(FilterType type, float cutoff, float q, double sr,
                                     float& b0, float& b1, float& b2, float& a1, float& a2) {
  float sr_f = static_cast<float>(sr);
  cutoff = std::clamp(cutoff, 20.0f, sr_f * 0.45f);
  q = std::clamp(q, 0.1f, 10.0f);

  float w0 = 2.0f * kPi * cutoff / sr_f;
  float cosw = std::cos(w0);
  float sinw = std::sin(w0);
  float alpha = sinw / (2.0f * q);

  float a0 = 1.0f + alpha;
  float b0_tmp = 0.0f;
  float b1_tmp = 0.0f;
  float b2_tmp = 0.0f;
  float a1_tmp = -2.0f * cosw;
  float a2_tmp = 1.0f - alpha;

  switch (type) {
    case FilterType::Low:
      b0_tmp = (1.0f - cosw) * 0.5f;
      b1_tmp = 1.0f - cosw;
      b2_tmp = (1.0f - cosw) * 0.5f;
      break;
    case FilterType::Band:
      b0_tmp = alpha;
      b1_tmp = 0.0f;
      b2_tmp = -alpha;
      break;
    case FilterType::High:
    default:
      b0_tmp = (1.0f + cosw) * 0.5f;
      b1_tmp = -(1.0f + cosw);
      b2_tmp = (1.0f + cosw) * 0.5f;
      break;
  }

  float invA0 = 1.0f / a0;
  b0 = b0_tmp * invA0;
  b1 = b1_tmp * invA0;
  b2 = b2_tmp * invA0;
  a1 = a1_tmp * invA0;
  a2 = a2_tmp * invA0;
}

void SynthVoice::updateCoefficients() {
  computeCoefficients(filterType_, cutoff_, resonance_, sr_, b0_, b1_, b2_, a1_, a2_);
  for (auto& stage : stages_) {
    stage = StageState{};
  }
  coeffDirty_ = false;
}

void SynthVoice::note_on() {
  envStage_ = EnvAttack;
  // calculate per-sample increment for attack
  float attackSamples = std::max(1.0f, envAttack_ * static_cast<float>(sr_));
  envInc_ = 1.0f / attackSamples;
}

void SynthVoice::note_off() {
  // transition to release
  envStage_ = EnvRelease;
  float releaseSamples = std::max(1.0f, envRelease_ * static_cast<float>(sr_));
  // envInc_ is negative for release
  envInc_ = - (envLevel_ / releaseSamples);
}

void SynthVoice::render(float* out, unsigned nframes) {
  if (coeffDirty_) updateCoefficients();

  const float baseInc = static_cast<float>(freq_ / sr_);

  for (unsigned i = 0; i < nframes; ++i) {
    float oscSum = 0.0f;
    for (int osc = 0; osc < kNumOsc; ++osc) {
      float inc = baseInc * std::pow(2.0f, oscOctave_[osc] / 12.0f) * std::pow(2.0f, oscDetune_[osc] / 1200.0f);
      oscPhase_[osc] += inc;
      if (oscPhase_[osc] >= 1.0f) oscPhase_[osc] -= std::floor(oscPhase_[osc]);
      float phase = oscPhase_[osc] + oscPhaseOffset_[osc];
      phase -= std::floor(phase);

      float oscValue = 0.0f;
      switch (oscWave_[osc]) {
        case Square: oscValue = (phase < 0.5f) ? 1.0f : -1.0f; break;
        case Sine:   oscValue = std::sinf(2.0f * kPi * phase); break;
        case Saw:
        default:     oscValue = 2.0f * phase - 1.0f; break;
      }
      oscSum += oscValue;
    }
    float sample = oscSum / static_cast<float>(kNumOsc);

    float stageInput = sample;
    for (int s = 0; s < filterStages_; ++s) {
      auto& st = stages_[s];
      float y = b0_ * stageInput + b1_ * st.x1 + b2_ * st.x2 - a1_ * st.y1 - a2_ * st.y2;
      st.x2 = st.x1;
      st.x1 = stageInput;
      st.y2 = st.y1;
      st.y1 = y;
      stageInput = y;
    }

    // Envelope processing (per-sample linear ramps)
    switch (envStage_) {
      case EnvIdle:
        // keep envLevel_ at 0
        break;
      case EnvAttack: {
        envLevel_ += envInc_;
        if (envLevel_ >= 1.0f) {
          envLevel_ = 1.0f;
          envStage_ = EnvDecay;
          float decaySamples = std::max(1.0f, envDecay_ * static_cast<float>(sr_));
          // amount to go from 1.0 -> sustain
          envInc_ = -(1.0f - envSustain_) / decaySamples;
        }
      } break;
      case EnvDecay: {
        envLevel_ += envInc_;
        if (envLevel_ <= envSustain_) {
          envLevel_ = envSustain_;
          envStage_ = EnvSustain;
          envInc_ = 0.0f;
        }
      } break;
      case EnvSustain:
        // hold at sustain
        break;
      case EnvRelease: {
        envLevel_ += envInc_;
        if (envLevel_ <= 0.0f) {
          envLevel_ = 0.0f;
          envStage_ = EnvIdle;
          envInc_ = 0.0f;
        }
      } break;
    }

    out[i] += 0.15f * envLevel_ * stageInput;
  }
}

bool SynthVoice::is_active() const {
  return envStage_ != EnvIdle || envLevel_ > 1e-6f;
}
