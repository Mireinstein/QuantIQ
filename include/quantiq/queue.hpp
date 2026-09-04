#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace quantiq {

/// Hand-off between the thread receiving market data and the thread deciding
/// what to do about it.
///
/// Bounded on purpose: if the market produces faster than the strategies can
/// consume, an unbounded queue hides that by growing until the process dies.
/// Blocking the producer instead makes the backlog visible while it is still
/// small.
///
/// `close()` is what lets a consumer exit cleanly. Without it a blocked `pop`
/// waits for data that will never arrive and the thread cannot be joined.
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

    /// Blocks while full. False once closed.
    bool push(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] { return closed_ || queue_.size() < capacity_; });
        if (closed_) return false;

        queue_.push_back(std::move(value));
        lock.unlock();
        not_empty_.notify_one();
        return true;
    }

    /// Blocks until an item arrives. nullopt once closed and drained, which is
    /// the consumer's signal to stop.
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) return std::nullopt;

        T value = std::move(queue_.front());
        queue_.pop_front();
        lock.unlock();
        not_full_.notify_one();
        return value;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<T> queue_;
    std::size_t capacity_;
    bool closed_ = false;
};

}  // namespace quantiq
