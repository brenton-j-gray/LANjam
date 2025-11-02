#include "ServerGuiApp.h"
#include "common/Discovery.h"

#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

void push_log(ServerState& state, const std::string& line) {
  std::lock_guard<std::mutex> lock(state.logMutex);
  state.log.push_back(line);
  while (state.log.size() > 200) state.log.pop_front();
}

void update_peer(ServerState& state, const std::string& endpoint, uint64_t addPackets, std::chrono::steady_clock::time_point now) {
  std::lock_guard<std::mutex> lock(state.peersMutex);
  auto it = std::find_if(state.peers.begin(), state.peers.end(), [&](const ServerPeerInfo& p){ return p.endpoint == endpoint; });
  if (it == state.peers.end()) {
    ServerPeerInfo info;
    info.endpoint = endpoint;
    info.lastSeen = now;
    info.packetsForwarded = addPackets;
    state.peers.push_back(info);
  } else {
    it->lastSeen = now;
    it->packetsForwarded += addPackets;
  }
}

} // namespace

int main() {
  ServerState state;
  std::atomic<bool> serverLoopActive{false};

  std::thread netThread([&]{
    while (!state.quitRequested.load()) {
      if (state.startRequested.exchange(false)) {
        if (state.running.load()) continue;

        uint16_t listenPort = state.port.load();
        push_log(state, "Starting server on port " + std::to_string(listenPort));

        try {
          asio::io_context io;
          asio::ip::udp::socket sock(io, asio::ip::udp::endpoint(asio::ip::udp::v4(), listenPort));
          sock.non_blocking(true);

          std::unique_ptr<asio::ip::udp::socket> discoverySock;
          if (listenPort != kDiscoveryPort) {
            asio::ip::udp::endpoint discoverEp(asio::ip::udp::v4(), kDiscoveryPort);
            discoverySock = std::make_unique<asio::ip::udp::socket>(io, discoverEp);
            discoverySock->set_option(asio::socket_base::reuse_address(true));
            discoverySock->non_blocking(true);
          }

          state.running.store(true);
          serverLoopActive.store(true);
          push_log(state, "Listening on UDP port " + std::to_string(listenPort));

          std::vector<uint8_t> buffer(1500);
          std::unordered_map<std::string, asio::ip::udp::endpoint> peers;

          auto handle_discovery = [&](asio::ip::udp::socket& ds) {
            asio::ip::udp::endpoint from;
            asio::error_code ec;
            size_t n = ds.receive_from(asio::buffer(buffer), from, 0, ec);
            if (ec == asio::error::would_block || ec == asio::error::try_again) return;
            if (ec || n == 0) return;
            std::string_view payload(reinterpret_cast<const char*>(buffer.data()), n);
            if (payload.rfind(kDiscoveryMsg, 0) == 0) {
              std::string reply = std::string(kDiscoveryReplyPrefix) + ":" + std::to_string(listenPort);
              ds.send_to(asio::buffer(reply), from);
              state.discoveryCount.fetch_add(1);
              push_log(state, "Discovery from " + from.address().to_string() + ":" + std::to_string(from.port()));
            }
          };
          while (!state.quitRequested.load() && !state.stopRequested.load()) {
            if (discoverySock) handle_discovery(*discoverySock);

            asio::ip::udp::endpoint from;
            asio::error_code ec;
            size_t n = sock.receive_from(asio::buffer(buffer), from, 0, ec);
            if (ec == asio::error::would_block || ec == asio::error::try_again) {
              std::this_thread::sleep_for(std::chrono::milliseconds(2));
              continue;
            }
            if (ec) {
              push_log(state, std::string("Receive error: ") + ec.message());
              std::this_thread::sleep_for(std::chrono::milliseconds(10));
              continue;
            }
            if (!n) continue;

            auto now = std::chrono::steady_clock::now();
            std::string_view payload(reinterpret_cast<const char*>(buffer.data()), n);
            if (payload.rfind(kDiscoveryMsg, 0) == 0) {
              std::string reply = std::string(kDiscoveryReplyPrefix) + ":" + std::to_string(listenPort);
              sock.send_to(asio::buffer(reply), from);
              state.discoveryCount.fetch_add(1);
              push_log(state, "Discovery from " + from.address().to_string() + ":" + std::to_string(from.port()));
              continue;
            }
            if (payload.rfind(kHelloMsg, 0) == 0) {
              sock.send_to(asio::buffer(kWelcomeMsg, std::strlen(kWelcomeMsg)), from);
              state.handshakeCount.fetch_add(1);
              std::string key = from.address().to_string() + ":" + std::to_string(from.port());
              peers[key] = from;
              update_peer(state, key, 0, now);
              push_log(state, "Handshake hello from " + key + " -> welcome sent");
              continue;
            }

            std::string key = from.address().to_string() + ":" + std::to_string(from.port());
            auto it = peers.find(key);
            if (it == peers.end()) {
              peers[key] = from;
              update_peer(state, key, 0, now);
              push_log(state, "Peer joined " + key + " (total peers: " + std::to_string(peers.size()) + ")");
            } else {
              it->second = from;
            }

            for (auto& [peerKey, ep] : peers) {
              if (peerKey == key) continue;
              asio::error_code sendEc;
              sock.send_to(asio::buffer(buffer.data(), n), ep, 0, sendEc);
              if (sendEc) {
                push_log(state, "Send error to " + peerKey + ": " + sendEc.message());
                continue;
              }
              state.packetsForwarded.fetch_add(1);
              update_peer(state, peerKey, 1, now);
            }
          }

          sock.close();
        } catch (const std::exception& e) {
          push_log(state, std::string("Server error: ") + e.what());
        }

        {
          std::lock_guard<std::mutex> lock(state.peersMutex);
          state.peers.clear();
        }

        state.stopRequested.store(false);
        state.running.store(false);
        serverLoopActive.store(false);
        push_log(state, "Server stopped.");
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });

  int guiResult = run_server_gui(state);

  state.quitRequested.store(true);
  state.stopRequested.store(true);
  // wait until net thread acknowledges stop
  const auto quitDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (serverLoopActive.load() && std::chrono::steady_clock::now() < quitDeadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (netThread.joinable()) netThread.join();
  return guiResult;
}
