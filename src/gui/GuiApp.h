#pragma once
#include <atomic>
#include <string>
#include <mutex>
#include <array>

struct OscParams {
  std::atomic<int>   wave{0};     // 0=saw,1=square,2=sine
  std::atomic<int>   octave{0};   // semitone offset /12 (steps of octaves)
  std::atomic<float> detune{0.0f}; // cents
  std::atomic<float> phase{0.0f};  // degrees 0-360
};

struct SynthParams {
  std::atomic<int>   octave{3};   // 3 -> A3 ~ 220 Hz
  std::atomic<int>   note{9};     // 0=C, 9=A
  std::atomic<float> cutoff{1200.0f};
  std::atomic<float> resonance{0.7f}; // filter Q
  std::atomic<int>   filterType{0};   // 0=low,1=band,2=high
  std::atomic<int>   filterSlope{1};  // stages 1-4
  std::array<OscParams, 3> osc{};
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
  std::atomic<bool> audioRunning{false};
  std::atomic<bool> audioStartRequested{false};
  std::atomic<bool> audioStopRequested{false};
  std::atomic<bool> discoverRequested{false};
  std::atomic<bool> discovering{false};
  std::atomic<int> discoveryStatus{0}; // 0 idle, 1 success, -1 failure
  std::atomic<bool> hostDirty{false};
  mutable std::mutex discoveryMutex;
  std::string discoveredHost;
  std::string discoveryMessage;
};

int run_gui(GuiState& shared);
