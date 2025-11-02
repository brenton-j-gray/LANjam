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
#include "common/JitterBuffer.h"
#include "audio/AudioIO.h"
#include "audio/SynthVoice.h"
#include "gui/GuiApp.h"

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
        udp.set_remote(gui.serverHost, gui.serverPort);
        std::printf("Set remote %s:%u\n", gui.serverHost.c_str(), gui.serverPort);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  });

  // Audio
  AudioIO audio;
  SynthVoice synth;
  synth.set_sample_rate(48000.0);

  audio.set_callback([&](float* out, unsigned nframes) {
    int baseOct = std::clamp(gui.params.octave.load(), 0, 8);
    int note = std::clamp(gui.params.note.load(), 0, 11);
    int midi = (baseOct + 1) * 12 + note;
    float freq = 440.0f * std::pow(2.0f, (static_cast<float>(midi) - 69.0f) / 12.0f);
    synth.set_freq(freq);
    for (int osc = 0; osc < 3; ++osc) {
      synth.set_osc_wave(osc, gui.params.osc[osc].wave.load());
      synth.set_osc_octave(osc, gui.params.osc[osc].octave.load());
      synth.set_osc_detune(osc, gui.params.osc[osc].detune.load());
      synth.set_osc_phase(osc, gui.params.osc[osc].phase.load());
    }
    synth.set_cutoff(gui.params.cutoff.load());
    synth.set_resonance(gui.params.resonance.load());
    synth.set_filter_type(gui.params.filterType.load());
    synth.set_filter_slope(gui.params.filterSlope.load());
    ctx.remoteGain.store(gui.params.remoteGain.load());

    synth.render(out, nframes);

    std::vector<float> mix(nframes, 0.0f);
    size_t got = ctx.jitter.pop(mix.data(), nframes);
    if (got) {
      for (size_t i = 0; i < got; ++i) out[i] += ctx.remoteGain.load() * mix[i];
    }

    std::vector<uint8_t> bytes(nframes * sizeof(float));
    std::memcpy(bytes.data(), out, bytes.size());
    udp.send(bytes.data(), bytes.size());
  });

  if (!audio.open(48000, 128)) {
    std::printf("Audio open failed\n");
  }

  // Wait for GUI to exit
  guiThread.join();
  gui.quitRequested.store(true);
  audio.close();
  rx.join();
  netCtl.join();
  return 0;
}
