#pragma once
#include <atomic>
#include <string>

struct SynthParams {
  std::atomic<float> freq{220.0f};
  std::atomic<float> cutoff{1200.0f};
  std::atomic<float> resonance{0.3f};
  std::atomic<int>   waveform{0}; // 0=saw, 1=square, 2=sin
  std::atomic<float> remoteGain{0.5f};
};

struct NetStats {
  std::atomic<uint32_t> rxPackets{0};
  std::atomic<uint32_t> xruns{0};
  std::atomic<size_t>   jitterDepth{0};
};

struct GuiState {
  SynthParams params;
  NetStats    stats;
  std::string serverHost = "127.0.0.1";
  uint16_t    serverPort  = 50000;
  std::atomic<bool> connectRequested{false};
  std::atomic<bool> quitRequested{false};
};

// runs the Win32 + D3D11 + ImGui loop. Returns when window closes.
int run_gui(GuiState& shared);
