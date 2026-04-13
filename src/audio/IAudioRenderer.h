#pragma once

#include <cstddef>
#include <cstdint>

struct GuiState;

class IAudioRenderer {
public:
  struct RuntimeStats {
    int backend = 0; // 0=CPU, 1=CUDA
    uint32_t activeVoices = 0;
    uint32_t fallbackCount = 0;
    uint32_t overBudgetCount = 0;
    float lastRenderMs = 0.0f;
    float lastBudgetMs = 0.0f;
    bool usedFallbackLastBlock = false;
  };

  virtual ~IAudioRenderer() = default;

  virtual void set_sample_rate(double sampleRate) = 0;
  virtual void set_polyphony(size_t voiceCount) = 0;
  virtual size_t polyphony() const = 0;

  virtual void apply_gui_params(const GuiState& gui) = 0;
  virtual void note_on(int note, int octave) = 0;
  virtual void note_off(int note) = 0;
  virtual void render_mixed(float* out, unsigned nframes) = 0;
  virtual RuntimeStats runtime_stats() const = 0;
};
