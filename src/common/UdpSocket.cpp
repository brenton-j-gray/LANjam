#include "UdpSocket.h"
#include <system_error>

UdpSocket::UdpSocket(asio::io_context& io, uint16_t) : io_(io), sock_(io) {}
void UdpSocket::bind_any(uint16_t port) {
  asio::ip::udp::endpoint ep(asio::ip::udp::v4(), port);
  sock_.open(ep.protocol());
  sock_.bind(ep);
}
void UdpSocket::set_remote(const std::string& host, uint16_t port) {
  try {
    remote_ = asio::ip::udp::endpoint(asio::ip::make_address(host), port);
  } catch (const std::exception&) {
    // fallback: try DNS resolution for hostnames
    try {
      asio::ip::udp::resolver resolver(io_);
      asio::ip::udp::resolver::results_type results = resolver.resolve(host, std::to_string(port));
      if (results.begin() != results.end()) {
        remote_ = *results.begin();
      }
    } catch (const std::exception& e) {
      // leave remote_ unchanged on failure
      std::fprintf(stderr, "UdpSocket::set_remote: failed to resolve %s:%u -> %s\n", host.c_str(), port, e.what());
    }
  }
}
void UdpSocket::close() {
  if (sock_.is_open()) {
    std::error_code ec;
    sock_.close(ec);
  }
}
bool UdpSocket::send(const uint8_t* data, size_t len) {
  std::error_code ec;
  auto sent = sock_.send_to(asio::buffer(data, len), remote_, 0, ec);
  if (ec) return false;
  return sent == static_cast<std::size_t>(len);
}
bool UdpSocket::send_to(const uint8_t* data, size_t len, const asio::ip::udp::endpoint& to) {
  std::error_code ec;
  auto sent = sock_.send_to(asio::buffer(data, len), to, 0, ec);
  if (ec) return false;
  return sent == static_cast<std::size_t>(len);
}
size_t UdpSocket::recv(uint8_t* buf, size_t maxlen, asio::ip::udp::endpoint& from) {
  std::error_code ec;
  size_t n = sock_.receive_from(asio::buffer(buf, maxlen), from, 0, ec);
  if (ec) return 0;
  return n;
}
