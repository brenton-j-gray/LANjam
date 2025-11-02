#include "UdpSocket.h"

UdpSocket::UdpSocket(asio::io_context& io, uint16_t) : io_(io), sock_(io) {}
void UdpSocket::bind_any(uint16_t port) {
  asio::ip::udp::endpoint ep(asio::ip::udp::v4(), port);
  sock_.open(ep.protocol());
  sock_.bind(ep);
}
void UdpSocket::set_remote(const std::string& host, uint16_t port) {
  remote_ = asio::ip::udp::endpoint(asio::ip::make_address(host), port);
}
bool UdpSocket::send(const uint8_t* data, size_t len) {
  return sock_.send_to(asio::buffer(data, len), remote_) == len;
}
size_t UdpSocket::recv(uint8_t* buf, size_t maxlen, asio::ip::udp::endpoint& from) {
  return sock_.receive_from(asio::buffer(buf, maxlen), from);
}
