#include "audio/CpuPolyRenderer.h"

#include "gui/GuiApp.h"

#include <algorithm>
#include <cmath>

CpuPolyRenderer::CpuPolyRenderer(size_t voiceCount, double sampleRate)
    : sampleRate_(sampleRate) {
  set_polyphony(voiceCount);
}

void CpuPolyRenderer::set_sample_rate(double sampleRate) {
  sampleRate_ = sampleRate;
  for (auto& voice : voices_) {
    voice.synth.set_sample_rate(sampleRate_);
  }
}

void CpuPolyRenderer::set_polyphony(size_t voiceCount) {
  voices_.clear();
  voices_.resize(voiceCount);
  for (auto& voice : voices_) {
    voice.synth.set_sample_rate(sampleRate_);
  }
  tick_ = 0;
}

size_t CpuPolyRenderer::polyphony() const {
  return voices_.size();
}

void CpuPolyRenderer::apply_gui_params(const GuiState& gui) {
  for (auto& voice : voices_) {
    for (int osc = 0; osc < 3; ++osc) {
      voice.synth.set_osc_wave(osc, gui.params.osc[osc].wave.load());
      voice.synth.set_osc_octave(osc, gui.params.osc[osc].octave.load());
      voice.synth.set_osc_detune(osc, gui.params.osc[osc].detune.load());
      voice.synth.set_osc_phase(osc, gui.params.osc[osc].phase.load());
    }
    voice.synth.set_cutoff(gui.params.cutoff.load());
    voice.synth.set_resonance(gui.params.resonance.load());
    voice.synth.set_filter_type(gui.params.filterType.load());
    voice.synth.set_filter_slope(gui.params.filterSlope.load());
    voice.synth.set_env_attack(gui.params.envAttack.load());
    voice.synth.set_env_decay(gui.params.envDecay.load());
    voice.synth.set_env_sustain(gui.params.envSustain.load());
    voice.synth.set_env_release(gui.params.envRelease.load());
  }
}

void CpuPolyRenderer::note_on(int note, int octave) {
  Voice* selected = nullptr;
  for (auto& voice : voices_) {
    if (!voice.synth.is_active() && voice.note == -1) {
      selected = &voice;
      break;
    }
  }
  if (!selected) {
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

  selected->note = note;
  selected->released = false;
  selected->lastUsed = ++tick_;

  const int midi = (octave + 1) * 12 + note;
  const float freq = 440.0f * std::pow(2.0f, (static_cast<float>(midi) - 69.0f) / 12.0f);
  selected->synth.set_freq(freq);
  selected->synth.note_on();
}

void CpuPolyRenderer::note_off(int note) {
  for (auto& voice : voices_) {
    if (voice.note == note && voice.synth.is_active()) {
      voice.synth.note_off();
      voice.released = true;
    }
  }
}

void CpuPolyRenderer::render_mixed(float* out, unsigned nframes) {
  uint32_t activeCount = 0;
  for (auto& voice : voices_) {
    if (voice.synth.is_active()) {
      ++activeCount;
      voice.synth.render(out, nframes);
    } else if (voice.note != -1 && voice.released) {
      voice.note = -1;
      voice.released = false;
    }
  }
  lastActiveVoices_ = activeCount;
}

IAudioRenderer::RuntimeStats CpuPolyRenderer::runtime_stats() const {
  RuntimeStats stats;
  stats.backend = 0;
  stats.activeVoices = lastActiveVoices_;
  return stats;
}
