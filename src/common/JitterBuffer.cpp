#include "JitterBuffer.h"
#include <algorithm>
#include <cstring>
#include <utility>

void JitterBuffer::push(const std::vector<float>& block) {
  std::scoped_lock<std::mutex> lk(m_);
  q_.push_back(block);
  // optional cap
  if (q_.size() > 64) q_.pop_front();
}

void JitterBuffer::push(std::vector<float>&& block) {
  std::scoped_lock<std::mutex> lk(m_);
  q_.push_back(std::move(block));
  // optional cap
  if (q_.size() > 64) q_.pop_front();
}
size_t JitterBuffer::pop(float* out, size_t nframes) {
  std::scoped_lock<std::mutex> lk(m_);
  if (q_.size() <= target_) return 0;
  auto blk = std::move(q_.front());
  q_.pop_front();
  size_t n = std::min(nframes, blk.size());
  std::copy_n(blk.data(), n, out);
  return n;
}

void JitterBuffer::set_target_blocks(size_t blocks) { 
    std::scoped_lock<std::mutex> lk(m_);
    target_ = blocks; 
}

size_t JitterBuffer::size() const { 
    std::scoped_lock<std::mutex> lk(m_); 
    return q_.size(); 
}
