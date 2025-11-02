#pragma once
#include <asio.hpp>
#include <vector>

class UdpSocket {
public:
  UdpSocket(asio::io_context& io, uint16_t local_port = 0);
  void bind_any(uint16_t port);
  void set_remote(const std::string& host, uint16_t port);
  void close();

  bool send(const uint8_t* data, size_t len);
  bool send_to(const uint8_t* data, size_t len, const asio::ip::udp::endpoint& to);
  size_t recv(uint8_t* buf, size_t maxlen, asio::ip::udp::endpoint& from);

  asio::ip::udp::endpoint remote_endpoint() const { return remote_; }

private:
  asio::io_context& io_;
  asio::ip::udp::socket sock_;
  asio::ip::udp::endpoint remote_;
};
