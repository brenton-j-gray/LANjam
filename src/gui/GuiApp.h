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
  // ADSR envelope parameters (seconds for times, 0..1 for sustain)
  std::atomic<float> envAttack{0.01f};
  std::atomic<float> envDecay{0.1f};
  std::atomic<float> envSustain{0.8f};
  std::atomic<float> envRelease{0.2f};
};

struct NetStats {
  std::atomic<uint32_t> rxPackets{0};
  std::atomic<uint32_t> xruns{0};
  std::atomic<size_t>   jitterDepth{0};
};

struct GuiState {
  SynthParams params;
  NetStats    stats;
  // Lock-free sequencer state shared between GUI and audio thread.
  struct SequencerState {
    std::atomic<int> bpm{120};
    std::atomic<bool> playing{false};
    std::atomic<int> step{0};
    // grid[row][step] -> 0/1
    std::array<std::array<std::atomic<uint8_t>, 16>, 12> grid{};
    SequencerState() {
      for (int r = 0; r < 12; ++r) for (int s = 0; s < 16; ++s) grid[r][s].store(0);
    }
  } sequencer;
  std::string serverHost = "127.0.0.1";
  uint16_t    serverPort  = 50000;
  // Gate for note on/off (true while a key is held)
  std::atomic<bool> noteGate{false};
  std::atomic<bool> connectRequested{false};
  std::atomic<bool> quitRequested{false};
  std::atomic<bool> audioRunning{false};
  std::atomic<bool> audioStartRequested{false};
  std::atomic<bool> audioStopRequested{false};
  std::atomic<bool> discoverRequested{false};
  std::atomic<bool> discovering{false};
  std::atomic<int> discoveryStatus{0}; // 0 idle, 1 success, -1 failure
  // Desired polyphony requested by the GUI (audio thread will resize the pool)
  std::atomic<int> polyphony{8};
  // Lightweight note request flags for GUI -> audio thread communication.
  // Each bit represents one of 12 notes (0=C .. 11=B). GUI sets bits when a key is pressed or released;
  // audio thread consumes and clears them via atomic exchange.
  std::atomic<uint16_t> noteOnRequests{0};
  std::atomic<uint16_t> noteOffRequests{0};
  std::atomic<bool> hostDirty{false};
  mutable std::mutex discoveryMutex;
  std::string discoveredHost;
  std::string discoveryMessage;
};

int run_gui(GuiState& shared);
