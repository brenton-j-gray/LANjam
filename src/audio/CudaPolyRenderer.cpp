#include "audio/CudaPolyRenderer.h"

#include "audio/CudaVoiceKernel.h"

#include "gui/GuiApp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

CudaPolyRenderer::CudaPolyRenderer(size_t voiceCount, double sampleRate)
    : sampleRate_(sampleRate), fallbackCpu_(voiceCount, sampleRate) {
  set_polyphony(voiceCount);
  rebuild_cuda_engine(maxFrames_);
}

CudaPolyRenderer::~CudaPolyRenderer() {
  lanjam_cuda_destroy_engine(engine_);
  engine_ = nullptr;
}

void CudaPolyRenderer::rebuild_cuda_engine(int maxFrames) {
  if (maxFrames <= 0) return;
  maxFrames_ = maxFrames;
  lanjam_cuda_destroy_engine(engine_);
  engine_ = lanjam_cuda_create_engine(
      static_cast<int>(std::max<size_t>(1, voices_.size())), maxFrames_, static_cast<float>(sampleRate_));
}

void CudaPolyRenderer::set_sample_rate(double sampleRate) {
  sampleRate_ = sampleRate;
  fallbackCpu_.set_sample_rate(sampleRate);
  rebuild_cuda_engine(maxFrames_);
}

void CudaPolyRenderer::set_polyphony(size_t voiceCount) {
  voices_.assign(voiceCount, VoiceState{});
  tick_ = 0;
  fallbackCpu_.set_polyphony(voiceCount);
  rebuild_cuda_engine(maxFrames_);
}

size_t CudaPolyRenderer::polyphony() const {
  return voices_.size();
}

void CudaPolyRenderer::apply_gui_params(const GuiState& gui) {
  for (int osc = 0; osc < 3; ++osc) {
    oscWaveType_[osc] = std::clamp(gui.params.osc[osc].wave.load(), 0, 2);
    oscOctave_[osc] = std::clamp(gui.params.osc[osc].octave.load(), -24, 24);
    oscDetune_[osc] = std::clamp(gui.params.osc[osc].detune.load(), -200.0f, 200.0f);
    float frac = std::fmod(gui.params.osc[osc].phase.load(), 360.0f) / 360.0f;
    if (frac < 0.0f) frac += 1.0f;
    oscPhaseOffset_[osc] = frac;
  }
  envAttackSec_ = std::max(0.0f, gui.params.envAttack.load());
  envDecaySec_ = std::max(0.0f, gui.params.envDecay.load());
  envSustain_ = std::clamp(gui.params.envSustain.load(), 0.0f, 1.0f);
  envReleaseSec_ = std::max(0.0f, gui.params.envRelease.load());
  filterCutoffHz_ = std::max(20.0f, gui.params.cutoff.load());
  filterType_ = std::clamp(gui.params.filterType.load(), 0, 2);
  fallbackCpu_.apply_gui_params(gui);
}

void CudaPolyRenderer::note_on(int note, int octave) {
  VoiceState* selected = nullptr;
  for (auto& voice : voices_) {
    if (!voice.active && voice.note == -1) {
      selected = &voice;
      break;
    }
  }
  if (!selected && !voices_.empty()) {
    uint64_t oldest = UINT64_MAX;
    size_t index = 0;
    for (size_t i = 0; i < voices_.size(); ++i) {
      if (voices_[i].lastUsed < oldest) {
        oldest = voices_[i].lastUsed;
        index = i;
      }
    }
    selected = &voices_[index];
  }
  if (selected) {
    const int midi = (octave + 1) * 12 + note;
    selected->note = note;
    selected->active = true;
    selected->envStage = 1;
    selected->envLevel = 0.0f;
    const float attackSamples = std::max(1.0f, envAttackSec_ * static_cast<float>(sampleRate_));
    selected->envInc = 1.0f / attackSamples;
    selected->filterState = 0.0f;
    selected->lastUsed = ++tick_;
    selected->freq = 440.0f * std::pow(2.0f, (static_cast<float>(midi) - 69.0f) / 12.0f);
  }
  fallbackCpu_.note_on(note, octave);
}

void CudaPolyRenderer::note_off(int note) {
  for (auto& voice : voices_) {
    if (voice.note == note && voice.active) {
      voice.envStage = 4;
      const float releaseSamples = std::max(1.0f, envReleaseSec_ * static_cast<float>(sampleRate_));
      voice.envInc = -(voice.envLevel / releaseSamples);
    }
  }
  fallbackCpu_.note_off(note);
}

void CudaPolyRenderer::render_mixed(float* out, unsigned nframes) {
  if (nframes == 0) return;
  if (static_cast<int>(nframes) > maxFrames_) {
    rebuild_cuda_engine(static_cast<int>(nframes));
  }

  activeFreqs_.clear();
  activePhasesX3_.clear();
  activeEnvLevels_.clear();
  activeEnvStages_.clear();
  activeEnvIncs_.clear();
  activeFilterStates_.clear();
  activeVoiceMap_.clear();
  for (size_t i = 0; i < voices_.size(); ++i) {
    if (!voices_[i].active && voices_[i].envStage == 0) continue;
    activeVoiceMap_.push_back(i);
    activeFreqs_.push_back(voices_[i].freq);
    activePhasesX3_.push_back(voices_[i].phase[0]);
    activePhasesX3_.push_back(voices_[i].phase[1]);
    activePhasesX3_.push_back(voices_[i].phase[2]);
    activeEnvLevels_.push_back(voices_[i].envLevel);
    activeEnvStages_.push_back(voices_[i].envStage);
    activeEnvIncs_.push_back(voices_[i].envInc);
    activeFilterStates_.push_back(voices_[i].filterState);
  }

  if (!engine_ || activeFreqs_.empty()) {
    usedFallbackLastBlock_ = true;
    ++fallbackCount_;
    lastActiveVoices_ = static_cast<uint32_t>(activeFreqs_.size());
    lastRenderMs_ = 0.0f;
    lastBudgetMs_ = static_cast<float>(1000.0 * (static_cast<double>(nframes) / sampleRate_));
    fallbackCpu_.render_mixed(out, nframes);
    return;
  }

  if (gpuOut_.size() < nframes) gpuOut_.resize(nframes);
  const float budgetMs = static_cast<float>(1000.0 * (static_cast<double>(nframes) / sampleRate_));
  lastBudgetMs_ = budgetMs;
  lastActiveVoices_ = static_cast<uint32_t>(activeFreqs_.size());
  float elapsedMs = 0.0f;
  const bool ok = lanjam_cuda_render_saw_block(
      engine_,
      activeFreqs_.data(),
      activePhasesX3_.data(),
      oscWaveType_.data(),
      oscOctave_.data(),
      oscDetune_.data(),
      oscPhaseOffset_.data(),
      activeEnvLevels_.data(),
      activeEnvStages_.data(),
      activeEnvIncs_.data(),
      activeFilterStates_.data(),
      static_cast<int>(activeFreqs_.size()),
      static_cast<int>(nframes),
      envAttackSec_,
      envDecaySec_,
      envSustain_,
      envReleaseSec_,
      filterCutoffHz_,
      filterType_,
      outputGain_,
      gpuOut_.data(),
      &elapsedMs);
  lastRenderMs_ = elapsedMs;

  if (!ok || elapsedMs > budgetMs * 0.9f) {
    usedFallbackLastBlock_ = true;
    ++fallbackCount_;
    if (ok && elapsedMs > budgetMs * 0.9f) {
      ++overBudgetCount_;
    }
    fallbackCpu_.render_mixed(out, nframes);
    if (ok && !warnedOverBudget_ && elapsedMs > budgetMs * 0.9f) {
      std::printf("CUDA render exceeded budget (%.3f ms > %.3f ms); using CPU fallback.\n", elapsedMs, budgetMs);
      warnedOverBudget_ = true;
    }
    return;
  }
  usedFallbackLastBlock_ = false;

  for (size_t i = 0; i < activeVoiceMap_.size(); ++i) {
    auto& voice = voices_[activeVoiceMap_[i]];
    voice.phase[0] = activePhasesX3_[i * 3 + 0];
    voice.phase[1] = activePhasesX3_[i * 3 + 1];
    voice.phase[2] = activePhasesX3_[i * 3 + 2];
    voice.envLevel = activeEnvLevels_[i];
    voice.envStage = activeEnvStages_[i];
    voice.envInc = activeEnvIncs_[i];
    voice.filterState = activeFilterStates_[i];
    if (voice.envStage == 0) {
      voice.active = false;
      voice.note = -1;
      voice.envLevel = 0.0f;
      voice.envInc = 0.0f;
      voice.filterState = 0.0f;
    } else {
      voice.active = true;
    }
  }
  for (unsigned i = 0; i < nframes; ++i) {
    out[i] += gpuOut_[i];
  }
}

IAudioRenderer::RuntimeStats CudaPolyRenderer::runtime_stats() const {
  RuntimeStats stats;
  stats.backend = 1;
  stats.activeVoices = lastActiveVoices_;
  stats.fallbackCount = fallbackCount_;
  stats.overBudgetCount = overBudgetCount_;
  stats.lastRenderMs = lastRenderMs_;
  stats.lastBudgetMs = lastBudgetMs_;
  stats.usedFallbackLastBlock = usedFallbackLastBlock_;
  return stats;
}
