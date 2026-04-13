#pragma once

#include "audio/IAudioRenderer.h"
#include "audio/CpuPolyRenderer.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

struct GuiState;
struct LanJamCudaVoiceEngine;

class CudaPolyRenderer : public IAudioRenderer {
public:
  CudaPolyRenderer(size_t voiceCount, double sampleRate);
  ~CudaPolyRenderer() override;

  void set_sample_rate(double sampleRate) override;
  void set_polyphony(size_t voiceCount) override;
  size_t polyphony() const override;

  void apply_gui_params(const GuiState& gui) override;
  void note_on(int note, int octave) override;
  void note_off(int note) override;
  void render_mixed(float* out, unsigned nframes) override;
  RuntimeStats runtime_stats() const override;

private:
  struct VoiceState {
    bool active = false;
    int note = -1;
    float freq = 0.0f;
    std::array<float, 3> phase{{0.0f, 0.0f, 0.0f}};
    int envStage = 0;
    float envLevel = 0.0f;
    float envInc = 0.0f;
    float filterState = 0.0f;
    uint64_t lastUsed = 0;
  };

  void rebuild_cuda_engine(int maxFrames);

  double sampleRate_ = 48000.0;
  uint64_t tick_ = 0;
  std::vector<VoiceState> voices_;
  std::vector<float> activeFreqs_;
  std::vector<float> activePhasesX3_;
  std::vector<float> activeEnvLevels_;
  std::vector<int> activeEnvStages_;
  std::vector<float> activeEnvIncs_;
  std::vector<float> activeFilterStates_;
  std::vector<size_t> activeVoiceMap_;
  std::vector<float> gpuOut_;
  std::array<int, 3> oscWaveType_{{0, 0, 0}};
  std::array<int, 3> oscOctave_{{0, 0, 0}};
  std::array<float, 3> oscDetune_{{0.0f, 0.0f, 0.0f}};
  std::array<float, 3> oscPhaseOffset_{{0.0f, 0.0f, 0.0f}};
  float envAttackSec_ = 0.01f;
  float envDecaySec_ = 0.10f;
  float envSustain_ = 0.8f;
  float envReleaseSec_ = 0.2f;
  float filterCutoffHz_ = 1200.0f;
  int filterType_ = 0;
  float outputGain_ = 0.15f;
  int maxFrames_ = 128;
  LanJamCudaVoiceEngine* engine_ = nullptr;
  bool warnedOverBudget_ = false;
  uint32_t fallbackCount_ = 0;
  uint32_t overBudgetCount_ = 0;
  uint32_t lastActiveVoices_ = 0;
  float lastRenderMs_ = 0.0f;
  float lastBudgetMs_ = 0.0f;
  bool usedFallbackLastBlock_ = false;

  CpuPolyRenderer fallbackCpu_;
};
