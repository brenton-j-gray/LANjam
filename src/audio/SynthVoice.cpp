#include "SynthVoice.h"
void SynthVoice::render(float* out, unsigned nframes) {
  float inc = static_cast<float>(freq_ / sr_);
  for (unsigned i = 0; i < nframes; ++i) {
    phase_ += inc; if (phase_ >= 1.0f) phase_ -= 1.0f;
    float x = 0.0f;
    switch (wave_) {
      case Saw:    x = 2.0f * phase_ - 1.0f; break;
      case Square: x = (phase_ < 0.5f) ? 1.0f : -1.0f; break;
      case Sine:   x = std::sinf(2.0f * 3.14159265f * phase_); break;
    }
    out[i] += 0.15f * x;
  }
}