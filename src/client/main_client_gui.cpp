#include <asio.hpp>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>

#include "common/UdpSocket.h"
#include "common/Discovery.h"
#include "common/JitterBuffer.h"
#include "audio/AudioIO.h"
#include "audio/IAudioRenderer.h"
#include "audio/CpuPolyRenderer.h"
#include "audio/CudaPolyRenderer.h"
#include "gui/GuiApp.h"

struct ClientCtx {
  std::atomic<bool> running{true};
  JitterBuffer jitter;
  std::atomic<float> remoteGain{0.5f};
  std::atomic<uint32_t> xruns{0};
};

namespace {

enum class AudioBackend {
  Cpu,
  Cuda
};

AudioBackend select_backend_from_env() {
  const char* env = std::getenv("LANJAM_AUDIO_BACKEND");
  if (!env) return AudioBackend::Cpu;

  std::string value(env);
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (value == "cuda") return AudioBackend::Cuda;
  return AudioBackend::Cpu;
}

}  // namespace

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
      ctx.jitter.push(std::move(block));
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

  // Audio: create renderer backend and wire to GUI gate/note
  AudioIO audio;
  const size_t kVoiceCount = 8;
  const AudioBackend selectedBackend = select_backend_from_env();
  std::unique_ptr<IAudioRenderer> renderer;
  if (selectedBackend == AudioBackend::Cuda) {
    renderer = std::make_unique<CudaPolyRenderer>(kVoiceCount, 48000.0);
    std::printf("Audio backend: CUDA (stub/fallback path for now)\n");
  } else {
    renderer = std::make_unique<CpuPolyRenderer>(kVoiceCount, 48000.0);
    std::printf("Audio backend: CPU\n");
  }
#if defined(LANJAM_ENABLE_CUDA)
#else
  if (selectedBackend == AudioBackend::Cuda) {
    std::printf("Note: binary built without LANJAM_ENABLE_CUDA; using CPU-equivalent renderer path.\n");
  }
#endif
  if (!renderer) {
    // Safety fallback.
  renderer = std::make_unique<CpuPolyRenderer>(kVoiceCount, 48000.0);
  }

  audio.set_callback([&](float* out, unsigned nframes) {
    static thread_local std::vector<float> mix;
    static thread_local std::vector<uint8_t> bytes;

    // zero output buffer
    std::memset(out, 0, static_cast<size_t>(nframes) * sizeof(float));

  // Sample-accurate sequencer handling (runs in audio thread)
    static const int kSeqRows = 12;
    static const int kSeqSteps = 16;
    static const double sampleRate = 48000.0;
    static double sampleAcc = 0.0; // leftover samples toward next step
    static uint64_t globalSamplePos = 0; // increasing sample counter
    static int currentStep = 0;
    static uint64_t seqReleaseSample = 0;
    int baseOct = std::clamp(gui.params.octave.load(), 0, 8);
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
            renderer->note_on(r, baseOct);
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
      for (int n = 0; n < 12; ++n) if (onReq & (1u << n)) renderer->note_on(n, baseOct);
    }
    uint16_t offReq = gui.noteOffRequests.exchange(0);
    if (offReq) {
      for (int n = 0; n < 12; ++n) if (offReq & (1u << n)) renderer->note_off(n);
    }

    // allow dynamic polyphony change requested by GUI
    int desired = std::clamp(gui.polyphony.load(), 1, 256);
    if (static_cast<int>(renderer->polyphony()) != desired) {
      renderer->set_polyphony(static_cast<size_t>(desired));
    }
    // update voice params from GUI (cheap to do each callback)
    renderer->apply_gui_params(gui);
    // (old gate path removed - GUI now communicates note on/off via request bitmasks)

    // render voices into out
    renderer->render_mixed(out, nframes);
    const auto renderStats = renderer->runtime_stats();
    gui.stats.audioBackend.store(renderStats.backend);
    gui.stats.activeVoices.store(renderStats.activeVoices);
    gui.stats.cudaFallbacks.store(renderStats.fallbackCount);
    gui.stats.cudaOverBudget.store(renderStats.overBudgetCount);
    gui.stats.cudaLastRenderMs.store(renderStats.lastRenderMs);
    gui.stats.cudaBudgetMs.store(renderStats.lastBudgetMs);
    gui.stats.cudaFallbackLastBlock.store(renderStats.usedFallbackLastBlock);

    // mix remote audio
    if (mix.size() < nframes) mix.resize(nframes);
    size_t got = ctx.jitter.pop(mix.data(), nframes);
    if (got) {
      float rg = ctx.remoteGain.load();
      for (size_t i = 0; i < got; ++i) out[i] += rg * mix[i];
    }

    // send audio
    const size_t byteCount = static_cast<size_t>(nframes) * sizeof(float);
    if (bytes.size() < byteCount) bytes.resize(byteCount);
    std::memcpy(bytes.data(), out, byteCount);
    udp.send(bytes.data(), byteCount);

    // advance sample position and handle sequencer note release timing
    globalSamplePos += nframes;
    if (seqReleaseMask != 0 && globalSamplePos >= seqReleaseSample) {
      // release all scheduled notes for that step
      for (int n = 0; n < 12; ++n) if (seqReleaseMask & (1u << n)) renderer->note_off(n);
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
