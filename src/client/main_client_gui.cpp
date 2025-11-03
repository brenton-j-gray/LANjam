#include <asio.hpp>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <string>
#include <cmath>
#include <algorithm>

#include "common/UdpSocket.h"
#include "common/Discovery.h"
#include "common/JitterBuffer.h"
#include "audio/AudioIO.h"
#include "audio/SynthVoice.h"
#include "gui/GuiApp.h"

// Simple polyphonic voice pool used from the audio thread only
struct Voice {
  SynthVoice synth;
  int note = -1; // 0..11, -1 idle
  bool released = false; // note_off called
  uint64_t lastUsed = 0; // for voice stealing
};

struct VoicePool {
  std::vector<Voice> voices;
  uint64_t tick = 0;

  VoicePool(size_t n, double sr) { voices.resize(n); for (auto &v : voices) v.synth.set_sample_rate(sr); }
  void resize(size_t n, double sr) {
    voices.clear();
    voices.resize(n);
    for (auto &v : voices) v.synth.set_sample_rate(sr);
    tick = 0;
  }

  void set_global_params_from_gui(const GuiState& gui) {
    for (size_t i = 0; i < voices.size(); ++i) {
      auto &v = voices[i];
      for (int osc = 0; osc < 3; ++osc) {
        v.synth.set_osc_wave(osc, gui.params.osc[osc].wave.load());
        v.synth.set_osc_octave(osc, gui.params.osc[osc].octave.load());
        v.synth.set_osc_detune(osc, gui.params.osc[osc].detune.load());
        v.synth.set_osc_phase(osc, gui.params.osc[osc].phase.load());
      }
      v.synth.set_cutoff(gui.params.cutoff.load());
      v.synth.set_resonance(gui.params.resonance.load());
      v.synth.set_filter_type(gui.params.filterType.load());
      v.synth.set_filter_slope(gui.params.filterSlope.load());
      v.synth.set_env_attack(gui.params.envAttack.load());
      v.synth.set_env_decay(gui.params.envDecay.load());
      v.synth.set_env_sustain(gui.params.envSustain.load());
      v.synth.set_env_release(gui.params.envRelease.load());
    }
  }

  void note_on(int note, int octave) {
    // choose free voice or steal oldest
    Voice* choose = nullptr;
    for (auto &v : voices) {
      if (!v.synth.is_active() && v.note == -1) { choose = &v; break; }
    }
    if (!choose) {
      // steal least recently used
      uint64_t oldest = UINT64_MAX; size_t idx = 0;
      for (size_t i = 0; i < voices.size(); ++i) {
        if (voices[i].lastUsed < oldest) { oldest = voices[i].lastUsed; idx = i; }
      }
      choose = &voices[idx];
    }
    // init voice
    choose->note = note;
    choose->released = false;
    choose->lastUsed = ++tick;
    int midi = (octave + 1) * 12 + note;
    float freq = 440.0f * std::pow(2.0f, (static_cast<float>(midi) - 69.0f) / 12.0f);
    choose->synth.set_freq(freq);
    choose->synth.note_on();
  }

  void note_off(int note) {
    for (auto &v : voices) {
      if (v.note == note && v.synth.is_active()) {
        v.synth.note_off();
        v.released = true;
        // keep v.note until envelope finishes (we'll clear below)
      }
    }
  }

  void render_mixed(float* out, unsigned nframes) {
    // mix all voices into out (synth.render adds into out)
    for (auto &v : voices) {
      if (v.synth.is_active()) {
        v.synth.render(out, nframes);
      } else {
        // voice idle, clear note if it was released previously
        if (v.note != -1 && v.released) {
          v.note = -1;
          v.released = false;
        }
      }
    }
  }
};

struct ClientCtx {
  std::atomic<bool> running{true};
  JitterBuffer jitter;
  std::atomic<float> remoteGain{0.5f};
  std::atomic<uint32_t> xruns{0};
};

int main() {
  GuiState gui;
  gui.serverHost = "127.0.0.1";
  gui.serverPort = 50000;
  for (int osc = 0; osc < 3; ++osc) {
    gui.params.osc[osc].wave.store(0);
    gui.params.osc[osc].octave.store(0);
    gui.params.osc[osc].detune.store(0.0f);
    gui.params.osc[osc].phase.store(osc == 0 ? 0.0f : osc * 120.0f);
  }

  // Launch GUI in its own thread
  std::thread guiThread([&] { run_gui(gui); });

  asio::io_context io;
  UdpSocket udp(io);
  udp.bind_any(0);

  ClientCtx ctx;
  ctx.jitter.set_target_blocks(2);

  // Simple RX loop
  std::thread rx([&] {
    std::vector<uint8_t> buf(1500);
    asio::ip::udp::endpoint from;
    while (!gui.quitRequested.load()) {
      size_t n = udp.recv(buf.data(), buf.size(), from);
      if (!n) continue;
      if (n % sizeof(float) != 0) continue;
      size_t frames = n / sizeof(float);
      std::vector<float> block(frames);
      std::memcpy(block.data(), buf.data(), n);
      ctx.jitter.push(block);
      gui.stats.rxPackets.fetch_add(1);
      gui.stats.jitterDepth.store(ctx.jitter.size()); // optional helper
    }
  });

  // Connect when requested
  std::thread netCtl([&] {
    for (;;) {
      if (gui.quitRequested.load()) break;
      if (gui.connectRequested.exchange(false)) {
        std::printf("Connect requested -> setting remote to %s:%u\n", gui.serverHost.c_str(), gui.serverPort);
        udp.set_remote(gui.serverHost, gui.serverPort);
        std::printf("Set remote %s:%u\n", gui.serverHost.c_str(), gui.serverPort);
      }

      if (gui.discoverRequested.exchange(false)) {
        // perform a simple UDP broadcast discovery on kDiscoveryPort
        gui.discovering.store(true);
        {
          std::lock_guard<std::mutex> lock(gui.discoveryMutex);
          gui.discoveryMessage.clear();
          gui.discoveredHost.clear();
        }
        try {
          asio::ip::udp::socket dsock( io );
          dsock.open(asio::ip::udp::v4());
          dsock.set_option(asio::socket_base::broadcast(true));
          asio::ip::udp::endpoint bcast(asio::ip::address_v4::broadcast(), kDiscoveryPort);
          dsock.send_to(asio::buffer(std::string(kDiscoveryMsg)), bcast);

          // wait briefly for replies
          dsock.non_blocking(true);
          std::vector<uint8_t> buf(1500);
          asio::ip::udp::endpoint from;
          auto start = std::chrono::steady_clock::now();
          bool found = false;
          while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(600)) {
            asio::error_code ec;
            size_t n = dsock.receive_from(asio::buffer(buf), from, 0, ec);
            if (ec == asio::error::would_block || ec == asio::error::try_again) {
              std::this_thread::sleep_for(std::chrono::milliseconds(20));
              continue;
            }
            if (ec || n == 0) continue;
            std::string reply(reinterpret_cast<const char*>(buf.data()), n);
            if (reply.rfind(kDiscoveryReplyPrefix, 0) == 0) {
              // reply format: PREFIX:port
              auto sep = reply.find(':');
              uint16_t serverPort = 50000;
              if (sep != std::string::npos) {
                serverPort = static_cast<uint16_t>(std::stoi(reply.substr(sep+1)));
              }
              {
                std::lock_guard<std::mutex> lock(gui.discoveryMutex);
                gui.discoveredHost = from.address().to_string();
                gui.discoveryMessage = std::string("Found server at ") + gui.discoveredHost + ":" + std::to_string(serverPort);
                gui.serverHost = gui.discoveredHost;
                gui.serverPort = serverPort;
              }
              gui.discoveryStatus.store(1);
              found = true;
              break;
            }
          }
          if (!found) {
            std::lock_guard<std::mutex> lock(gui.discoveryMutex);
            gui.discoveryMessage = "No server found";
            gui.discoveryStatus.store(-1);
          }
        } catch (const std::exception& e) {
          std::lock_guard<std::mutex> lock(gui.discoveryMutex);
          gui.discoveryMessage = std::string("Discovery error: ") + e.what();
          gui.discoveryStatus.store(-1);
        }
        gui.discovering.store(false);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  });

  // Audio: create voice pool and wire to GUI gate/note
  AudioIO audio;
  const size_t kVoiceCount = 8;
  VoicePool vpool(kVoiceCount, 48000.0);

  audio.set_callback([&](float* out, unsigned nframes) {
    // zero output buffer
    for (unsigned i = 0; i < nframes; ++i) out[i] = 0.0f;

  // Sample-accurate sequencer handling (runs in audio thread)
    static const int kSeqRows = 12;
    static const int kSeqSteps = 16;
    static const double sampleRate = 48000.0;
    static double sampleAcc = 0.0; // leftover samples toward next step
    static uint64_t globalSamplePos = 0; // increasing sample counter
    static int currentStep = 0;
    static bool seqOwnsGate = false;
    static uint64_t seqReleaseSample = 0;
  int baseOct = std::clamp(gui.params.octave.load(), 0, 8);
  int note = std::clamp(gui.params.note.load(), 0, 11);
  bool playing = gui.sequencer.playing.load();
  int bpm = gui.sequencer.bpm.load();
    if (bpm <= 0) bpm = 120;
    double samplesPerStep = (sampleRate * 60.0) / static_cast<double>(bpm) / 4.0; // 16th notes
  static uint16_t seqReleaseMask = 0;
  if (playing) {
      // accumulate and advance steps as needed
      sampleAcc += static_cast<double>(nframes);
      while (sampleAcc >= samplesPerStep) {
        sampleAcc -= samplesPerStep;
        currentStep = (currentStep + 1) % kSeqSteps;
  gui.sequencer.step.store(currentStep);

        // trigger all rows set at this step (polyphonic step)
        for (int r = kSeqRows - 1; r >= 0; --r) {
          if (gui.sequencer.grid[r][currentStep].load()) {
            vpool.note_on(r, baseOct);
            seqReleaseMask |= static_cast<uint16_t>(1u << r);
          }
        }
        if (seqReleaseMask != 0) {
          seqReleaseSample = globalSamplePos + static_cast<uint64_t>(samplesPerStep * 0.8);
        }
      }
    } else {
      // if paused, reset accumulator so we restart cleanly when started
      sampleAcc = 0.0;
    }

    // process GUI note on/off requests (bitmasks) - consume and clear atomically
    uint16_t onReq = gui.noteOnRequests.exchange(0);
    if (onReq) {
      for (int n = 0; n < 12; ++n) if (onReq & (1u << n)) vpool.note_on(n, baseOct);
    }
    uint16_t offReq = gui.noteOffRequests.exchange(0);
    if (offReq) {
      for (int n = 0; n < 12; ++n) if (offReq & (1u << n)) vpool.note_off(n);
    }

    // allow dynamic polyphony change requested by GUI
    int desired = std::clamp(gui.polyphony.load(), 1, 256);
    if (static_cast<int>(vpool.voices.size()) != desired) {
      vpool.resize(static_cast<size_t>(desired), 48000.0);
    }
    // update voice params from GUI (cheap to do each callback)
    vpool.set_global_params_from_gui(gui);
    // (old gate path removed - GUI now communicates note on/off via request bitmasks)

    // render voices into out
    vpool.render_mixed(out, nframes);

    // mix remote audio
    std::vector<float> mix(nframes, 0.0f);
    size_t got = ctx.jitter.pop(mix.data(), nframes);
    if (got) {
      float rg = ctx.remoteGain.load();
      for (size_t i = 0; i < got; ++i) out[i] += rg * mix[i];
    }

    // send audio
    std::vector<uint8_t> bytes(nframes * sizeof(float));
    std::memcpy(bytes.data(), out, bytes.size());
    udp.send(bytes.data(), bytes.size());

    // advance sample position and handle sequencer note release timing
    globalSamplePos += nframes;
    if (seqReleaseMask != 0 && globalSamplePos >= seqReleaseSample) {
      // release all scheduled notes for that step
      for (int n = 0; n < 12; ++n) if (seqReleaseMask & (1u << n)) vpool.note_off(n);
      seqReleaseMask = 0;
    }
  });

  if (!audio.open(48000, 128)) {
    std::printf("Audio open failed\n");
  }

  // Wait for GUI to exit
  guiThread.join();
  // Ensure other threads see quit and unblock any blocking socket calls
  gui.quitRequested.store(true);
  udp.close();
  audio.close();
  rx.join();
  netCtl.join();
  return 0;
}
