#pragma once
#include <cstdint>
#include <vector>

struct PacketHeader {
  uint32_t room_id;
  uint32_t sender_id;
  uint32_t seq;
  uint64_t timestamp_ns;
  uint8_t  flags;
};

struct AudioPacket {
  PacketHeader hdr{};
  std::vector<uint8_t> payload; // raw PCM for v0; Opus later
};
