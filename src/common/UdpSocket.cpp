#include "UdpSocket.h"
#include <system_error>

UdpSocket::UdpSocket(asio::io_context& io, uint16_t) : io_(io), sock_(io) {}
void UdpSocket::bind_any(uint16_t port) {
  asio::ip::udp::endpoint ep(asio::ip::udp::v4(), port);
  sock_.open(ep.protocol());
  sock_.bind(ep);
}
void UdpSocket::set_remote(const std::string& host, uint16_t port) {
  remote_ = asio::ip::udp::endpoint(asio::ip::make_address(host), port);
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
  return sock_.receive_from(asio::buffer(buf, maxlen), from);
}
