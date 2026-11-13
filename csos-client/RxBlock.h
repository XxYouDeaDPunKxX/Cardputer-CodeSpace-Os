#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>

// Owned exclusively by the WSS RX task. A WS callback deposits one bounded
// block; CXL consumes its bytes before the next network poll is permitted.
class RxBlock {
  uint8_t data_[4096];
  size_t position_=0, length_=0;
public:
  uint8_t* storage() { return data_; }
  size_t capacity() const { return sizeof(data_); }
  size_t available() const { return length_-position_; }
  bool put(const uint8_t* data,size_t size) {
    if (available() || size>sizeof(data_)) return false;
    if (size && data!=data_) std::memcpy(data_,data,size);
    position_=0; length_=size;
    return true;
  }
  size_t read(uint8_t* destination,size_t requested) {
    const size_t size=std::min(requested,available());
    if (size) std::memcpy(destination,data_+position_,size);
    position_+=size;
    return size;
  }
  void reset() { position_=length_=0; }
};
