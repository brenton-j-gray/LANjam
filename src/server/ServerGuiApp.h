#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct ServerPeerInfo {
  std::string endpoint;
  uint64_t packetsForwarded = 0;
  std::chrono::steady_clock::time_point lastSeen;
};

struct ServerState {
  std::atomic<uint16_t> port{50000};
  std::atomic<bool> startRequested{false};
  std::atomic<bool> stopRequested{false};
  std::atomic<bool> running{false};
  std::atomic<bool> quitRequested{false};

  std::atomic<uint64_t> discoveryCount{0};
  std::atomic<uint64_t> handshakeCount{0};
  std::atomic<uint64_t> packetsForwarded{0};

  std::mutex peersMutex;
  std::vector<ServerPeerInfo> peers;

  std::mutex logMutex;
  std::deque<std::string> log;
};

int run_server_gui(ServerState& state);
