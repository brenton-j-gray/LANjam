#pragma once

#include "audio/IAudioRenderer.h"
#include "audio/SynthVoice.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class CpuPolyRenderer : public IAudioRenderer {
public:
  CpuPolyRenderer(size_t voiceCount, double sampleRate);

  void set_sample_rate(double sampleRate) override;
  void set_polyphony(size_t voiceCount) override;
  size_t polyphony() const override;

  void apply_gui_params(const GuiState& gui) override;
  void note_on(int note, int octave) override;
  void note_off(int note) override;
  void render_mixed(float* out, unsigned nframes) override;
  RuntimeStats runtime_stats() const override;

private:
  struct Voice {
    SynthVoice synth;
    int note = -1;
    bool released = false;
    uint64_t lastUsed = 0;
  };

  double sampleRate_ = 48000.0;
  std::vector<Voice> voices_;
  uint64_t tick_ = 0;
  uint32_t lastActiveVoices_ = 0;
};
