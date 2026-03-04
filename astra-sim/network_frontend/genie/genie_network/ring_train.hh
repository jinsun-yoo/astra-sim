#ifndef GENIE_NETWORK_RING_TRAIN_HH
#define GENIE_NETWORK_RING_TRAIN_HH
#include <stdexcept>

// WARNING: RingBuffer is NOT thread safe!
// We assume sequentiality in Genie. Hence writes are 'atomic' and there will be no overwrite of a dequeued value before it is consumed.
template <typename T>
class RingTrain {
 public:
    RingTrain(int size, int buffer_idx) : size_(size), buffer_idx_(buffer_idx), head_(0), tail_(0), count_(0) {
        available_data_ptr = new T*[size_]();
        data_ = new T[size_]();
        for (int i = 0; i < size_; ++i) {
            available_data_ptr[i] = &data_[i];
        }
    }

    ~RingTrain() {
        delete[] available_data_ptr;
        delete[] data_;
    }

    T*& get_slot_to_write() {
        T*& slot_ptr = available_data_ptr[head_];
        head_ = (head_ + 1) % size_;
        if (head_ == tail_) {
            throw std::runtime_error("RingTrain overflow");
        }
        count_ += 1;
        return slot_ptr;
    }
 
    void return_finished_slot(T*& slot_ptr) {
        if (head_ == tail_) {
            throw std::runtime_error("RingTrain underflow during return_finished_slot");
        }
        available_data_ptr[tail_] = slot_ptr;
        tail_ = (tail_ + 1) % size_;
        count_ -= 1;
    }

    bool is_empty() const {
        return count_ == 0;
    }

    bool is_full() const {
        return count_ == size_ - 1;
    }

    int size() const {
        return count_;
    }

private:
    int size_;
    T** available_data_ptr;
    T*  data_;
    int head_;
    int tail_;
    int count_;
    int buffer_idx_; // == qp_idx
};

#endif // GENIE_NETWORK_RING_TRAIN_HH