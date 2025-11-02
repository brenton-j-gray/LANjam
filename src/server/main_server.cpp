#include <asio.hpp>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include "common/UdpSocket.h"

int main(int argc, char** argv) {
  uint16_t port = 50000;
  if (argc > 1) port = static_cast<uint16_t>(std::stoi(argv[1]));
  asio::io_context io;
  UdpSocket sock(io);
  sock.bind_any(port);

  printf("Server listening on UDP %u\n", port);

  std::vector<uint8_t> buf(1500);
  std::unordered_map<std::string, asio::ip::udp::endpoint> peers;

  for (;;) {
    asio::ip::udp::endpoint from;
    size_t n = sock.recv(buf.data(), buf.size(), from);
    if (!n) continue;
    auto key = from.address().to_string() + ":" + std::to_string(from.port());
    peers[key] = from;

    // fan-out to others
    for (auto& [k, ep] : peers) {
      if (k == key) continue;
      // send through the same socket but to stored endpoint
      // quick send using a temporary UdpSocket clone would be messy
      // so create a transient socket-less send
      // Simpler approach: set remote and send
      // This socket tracks only one remote, so we make a raw send here:
      // For now, skip if same address. Minimal server. We can improve later.
    }

    // Minimal approach: forward by constructing a new socket each time
    // which is not efficient but fine for v0
    for (auto& [k, ep] : peers) {
      if (k == key) continue;
      asio::ip::udp::socket tmp(io, asio::ip::udp::v4());
      tmp.send_to(asio::buffer(buf.data(), n), ep);
    }
  }
}
