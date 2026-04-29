#ifndef GENIE_NETWORK_RING_BUFFER_HH
#define GENIE_NETWORK_RING_BUFFER_HH
#include <stdexcept>

// WARNING: RingBuffer is NOT thread safe!
// We assume sequentiality in Genie. Hence writes are 'atomic' and there will be no overwrite of a dequeued value before it is consumed.
template <typename T>
class RingBuffer {
 public:
    RingBuffer(int size, int buffer_idx) : size_(size), buffer_idx_(buffer_idx), head_(0), tail_(0), count_(0) {
        buffer_ = new T[size_];
    }

    ~RingBuffer() {
        delete[] buffer_;
    }
 
    // Copy-based enqueue (allocates/copies the item)
    bool enqueue(const T& item) {
        int next_head = (head_ + 1) % size_;
        if (next_head == tail_) {
            throw std::runtime_error("RingBuffer overflow");
        }
        buffer_[head_] = item;
        head_ = next_head;
        count_ += 1;
        return true;
    }

    // Zero-copy: Get reference to next write slot without advancing head
    T& get_next_write_slot() {
        int next_head = (head_ + 1) % size_;
        if (next_head == tail_) {
            throw std::runtime_error("RingBuffer overflow");
        }
        return buffer_[head_];
    }

    // Zero-copy: Commit the write and advance head
    void commit_write() {
        int next_head = (head_ + 1) % size_;
        if (next_head == tail_) {
            throw std::runtime_error("RingBuffer overflow during commit");
        }
        head_ = next_head;
        count_ += 1;
    }

    // Zero-copy dequeue (returns a reference)
    T& dequeue() {
        if (head_ == tail_) {
            throw std::runtime_error("Buffer " + std::to_string(buffer_idx_) + " is empty. Head: " + std::to_string(head_) + " Tail: " + std::to_string(tail_) + " Count: " + std::to_string(count_) + ")");
        }
        T& item = buffer_[tail_];
        tail_ = (tail_ + 1) % size_;
        count_ -= 1;
        return item;
    }

    bool is_empty() const {
        return head_ == tail_;
    }

    bool is_full() const {
        return (head_ + 1) % size_ == tail_;
    }

    int size() const {
        return count_;
    }

private:
    int size_;
    T* buffer_;
    int head_;
    int tail_;
    int count_;
    int buffer_idx_; // == qp_idx
};

#endif // GENIE_NETWORK_RING_BUFFER_HH