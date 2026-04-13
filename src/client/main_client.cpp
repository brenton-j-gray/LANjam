#include <asio.hpp>
#include <thread>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>
#include "common/UdpSocket.h"
#include "common/JitterBuffer.h"
#include "audio/AudioIO.h"
#include "audio/SynthVoice.h"

struct ClientCtx {
  std::atomic<bool> running{true};
  JitterBuffer jitter;
  std::mutex tx_m;
  std::vector<float> lastBlock; // for TX
};

int main(int argc, char** argv) {
  if (argc < 3) {
    printf("Usage: lan_jam_client <server_ip> <server_port>\n");
    return 1;
  }
  std::string host = argv[1];
  uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));

  asio::io_context io;
  UdpSocket udp(io);
  udp.bind_any(0);
  udp.set_remote(host, port);

  ClientCtx ctx;
  ctx.jitter.set_target_blocks(2); // ~2 audio buffers of delay

  // RX thread
  std::thread rx([&]{
    std::vector<uint8_t> buf(1500);
    asio::ip::udp::endpoint from;
    while (ctx.running.load()) {
      size_t n = udp.recv(buf.data(), buf.size(), from);
      if (!n) continue;
      // Interpret payload as float32 mono frames
      if (n % sizeof(float) != 0) continue;
      size_t frames = n / sizeof(float);
      std::vector<float> block(frames);
      std::memcpy(block.data(), buf.data(), n);
      ctx.jitter.push(std::move(block));
    }
  });

  // Audio
  AudioIO audio;
  SynthVoice synth;
  synth.set_sample_rate(48000.0);
  audio.set_callback([&](float* out, unsigned nframes){
    static thread_local std::vector<float> mix;
    static thread_local std::vector<uint8_t> bytes;

    // 1) Local synth
    synth.render(out, nframes);

    // 2) Mix in remote
    if (mix.size() < nframes) mix.resize(nframes);
    size_t got = ctx.jitter.pop(mix.data(), nframes);
    if (got) {
      for (size_t i = 0; i < got; ++i) out[i] += 0.5f * mix[i];
    }

    // 3) Ship current block as PCM
    // Copy out to tx buffer to avoid racing
    const size_t byteCount = static_cast<size_t>(nframes) * sizeof(float);
    if (bytes.size() < byteCount) bytes.resize(byteCount);
    std::memcpy(bytes.data(), out, byteCount);
    udp.send(bytes.data(), byteCount);
  });
  if (!audio.open(48000, 128)) {
    printf("Failed to open audio\n");
    ctx.running = false;
  }

  printf("Client running. Press Enter to quit.\n");
  getchar();

  ctx.running = false;
  audio.close();
  rx.join();
  return 0;
}
