#pragma once
#include <deque>
#include <vector>
#include <mutex>

class JitterBuffer {
public:
  // store vectors of float samples
  void push(const std::vector<float>& block);
  size_t pop(float* out, size_t nframes); // returns frames written
  void set_target_blocks(size_t blocks);  // fixed delay in blocks
  size_t size() const;

private:
  std::deque<std::vector<float>> q_;
  mutable std::mutex m_;
  size_t target_ = 2;
};
