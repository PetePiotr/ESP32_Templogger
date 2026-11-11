// =============================================================================
//  reading_queue.cpp  –  Implementierung von ReadingQueue
// =============================================================================
#include "reading_queue.h"

ReadingQueue::~ReadingQueue() { free(buf_); }

size_t ReadingQueue::begin(size_t capacity) {
  free(buf_);
  buf_ = nullptr;
  head_ = size_ = 0;
  while (capacity > 0 && buf_ == nullptr) {
    buf_ = static_cast<Reading *>(malloc(capacity * sizeof(Reading)));
    if (buf_ == nullptr) capacity /= 2;
  }
  capacity_ = capacity;
  return capacity_;
}

void ReadingQueue::push(const Reading &r) {
  if (capacity_ == 0) {
    dropped_++;
    return;
  }
  if (size_ == capacity_) {       // voll: ältesten Wert verwerfen
    head_ = (head_ + 1) % capacity_;
    size_--;
    dropped_++;
  }
  buf_[(head_ + size_) % capacity_] = r;
  size_++;
}

const Reading &ReadingQueue::at(size_t index) const {
  return buf_[(head_ + index) % capacity_];
}

void ReadingQueue::popFront(size_t count) {
  if (count > size_) count = size_;
  if (capacity_ > 0) head_ = (head_ + count) % capacity_;
  size_ -= count;
}
