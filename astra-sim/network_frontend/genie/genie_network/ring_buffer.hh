#ifndef GENIE_NETWORK_RING_BUFFER_HH
#define GENIE_NETWORK_RING_BUFFER_HH
#include <stdexcept>

class RingBuffer {
 public:
    RingBuffer(int size, int buffer_idx) : size_(size), buffer_idx_(buffer_idx), head_(0), tail_(0), count_(0) {
        buffer_ = new void*[size_];
    }
 
    bool enqueue(void* item) {
        int next_head = (head_ + 1) % size_;
        if (next_head == tail_) {
            throw std::runtime_error("RingBuffer overflow");
        }
        buffer_[head_] = item;
        head_ = next_head;
        count_ += 1;
        return true;
    }

    void* dequeue() {
        if (head_ == tail_) {
            throw std::runtime_error("Buffer " + std::to_string(buffer_idx_) + " is empty. Head: " + std::to_string(head_) + " Tail: " + std::to_string(tail_) + " Count: " + std::to_string(count_) + ")");
        }
        void* item = buffer_[tail_];
        tail_ = (tail_ + 1) % size_;
        count_ -= 1;
        return item;
    }

private:
    int size_;
    void** buffer_;
    int head_;
    int tail_;
    int count_;
    int buffer_idx_; // == qp_idx
};

#endif // GENIE_NETWORK_RING_BUFFER_HH