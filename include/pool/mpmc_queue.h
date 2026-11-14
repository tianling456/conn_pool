//
// Created by aobo on 2026/11/8.
//

#ifndef CONN_POOL_MPMC_QUEUE_H
#define CONN_POOL_MPMC_QUEUE_H

#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>

namespace cp {

template <typename T>
class BoundedMpmcQueue {
public:
    explicit BoundedMpmcQueue(size_t capacity) {
        size_t cap = 1;
        while (cap < capacity) cap <<= 1;
        capacity_ = cap;
        mask_     = cap - 1;
        cells_ = std::make_unique<Cell[]>(cap);
        for (size_t i = 0; i < cap; ++i)
            cells_[i].seq.store(i, std::memory_order_relaxed);
    }

    bool enqueue(T& item) {
        for (;;) {
            size_t pos = enqueuePos_.load(std::memory_order_relaxed);
            Cell& cell = cells_[pos & mask_];
            size_t seq = cell.seq.load(std::memory_order_acquire);
            if (seq == pos) {
                if (enqueuePos_.compare_exchange_weak(pos, pos + 1)) {
                    cell.data = std::move(item);
                    cell.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (static_cast<std::ptrdiff_t>(seq)
                     - static_cast<std::ptrdiff_t>(pos) < 0) {
                return false;
            }
        }
    }

    bool dequeue(T& out) {
        for (;;) {
            size_t pos = dequeuePos_.load(std::memory_order_relaxed);
            Cell& cell = cells_[pos & mask_];
            size_t seq = cell.seq.load(std::memory_order_acquire);
            if (seq == pos + 1) {
                if (dequeuePos_.compare_exchange_weak(pos, pos + 1)) {
                    out = std::move(cell.data);
                    cell.seq.store(pos + capacity_, std::memory_order_release);
                    return true;
                }
            } else if (static_cast<std::ptrdiff_t>(seq)
                     - static_cast<std::ptrdiff_t>(pos + 1) < 0) {
                return false;
            }
        }
    }

    size_t approxSize() const {
        size_t e = enqueuePos_.load(std::memory_order_relaxed);
        size_t d = dequeuePos_.load(std::memory_order_relaxed);
        return e >= d ? e - d : 0;
    }

private:
    struct alignas(64) Cell {
        std::atomic<size_t> seq;
        T data;
    };
    std::unique_ptr<Cell[]> cells_;
    size_t capacity_, mask_;
    alignas(64) std::atomic<size_t> enqueuePos_{0};
    alignas(64) std::atomic<size_t> dequeuePos_{0};
};

} // namespace cp

#endif //CONN_POOL_MPMC_QUEUE_H
