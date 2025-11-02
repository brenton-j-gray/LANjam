#include <asio.hpp>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <string>
#include <string_view>
#include <cstring>
#include <thread>
#include <chrono>
#include <memory>

#include "common/Discovery.h"

int main(int argc, char** argv) {
  uint16_t port = 50000;
  if (argc > 1) port = static_cast<uint16_t>(std::stoi(argv[1]));

  try {
    asio::io_context io;
    asio::ip::udp::socket sock(io, asio::ip::udp::endpoint(asio::ip::udp::v4(), port));
    sock.non_blocking(true);

    std::unique_ptr<asio::ip::udp::socket> discoverySock;
    if (port != kDiscoveryPort) {
      asio::ip::udp::endpoint discoverEp(asio::ip::udp::v4(), kDiscoveryPort);
      discoverySock = std::make_unique<asio::ip::udp::socket>(io, discoverEp);
      discoverySock->set_option(asio::socket_base::reuse_address(true));
      discoverySock->non_blocking(true);
    }

    std::printf("Server listening on UDP %u\n", port);

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
        std::string reply = std::string(kDiscoveryReplyPrefix) + ":" + std::to_string(port);
        ds.send_to(asio::buffer(reply), from);
        std::printf("Discovery request from %s:%u\n", from.address().to_string().c_str(), from.port());
      }
    };

    while (true) {
      if (discoverySock) handle_discovery(*discoverySock);

      asio::ip::udp::endpoint from;
      asio::error_code ec;
      size_t n = sock.receive_from(asio::buffer(buffer), from, 0, ec);
      if (ec == asio::error::would_block || ec == asio::error::try_again) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }
      if (ec || n == 0) continue;

      std::string_view payload(reinterpret_cast<const char*>(buffer.data()), n);
      if (payload.rfind(kDiscoveryMsg, 0) == 0) {
        std::string reply = std::string(kDiscoveryReplyPrefix) + ":" + std::to_string(port);
        sock.send_to(asio::buffer(reply), from);
        std::printf("Discovery request from %s:%u\n", from.address().to_string().c_str(), from.port());
        continue;
      }
      if (payload.rfind(kHelloMsg, 0) == 0) {
        sock.send_to(asio::buffer(kWelcomeMsg, std::strlen(kWelcomeMsg)), from);
        std::printf("Handshake hello from %s:%u -> welcome sent\n", from.address().to_string().c_str(), from.port());
        continue;
      }

      auto key = from.address().to_string() + ":" + std::to_string(from.port());
      auto [it, inserted] = peers.emplace(key, from);
      if (!inserted) {
        it->second = from;
      } else {
        std::printf("Peer joined %s (total peers: %zu)\n", key.c_str(), peers.size());
      }

      for (auto& [k, ep] : peers) {
        if (k == key) continue;
        sock.send_to(asio::buffer(buffer.data(), n), ep);
      }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Server error: %s\n", e.what());
    return 1;
  }
  return 0;
}
