#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>

namespace quantiq {

/// Fixed-capacity rolling window. Every indicator in this project is some
/// arithmetic over the last N bars, so they all share this one container
/// rather than each keeping its own deque and trimming it by hand.
///
/// Indexing is newest-first: `[0]` is the most recent value pushed, `[1]` the
/// one before it. That ordering is chosen because indicator code is almost
/// always written in terms of "the current bar versus N bars ago".
template <typename T, std::size_t N>
class RingBuffer {
public:
    static_assert(N > 0, "a rolling window of zero has no meaning");

    void push(const T& value) {
        head_ = (head_ + 1) % N;
        data_[head_] = value;
        if (size_ < N) ++size_;
    }

    const T& operator[](std::size_t age) const {
        if (age >= size_) throw std::out_of_range("RingBuffer index past end");
        return data_[(head_ + N - age) % N];
    }

    std::size_t size() const noexcept { return size_; }
    bool full() const noexcept { return size_ == N; }
    bool empty() const noexcept { return size_ == 0; }
    static constexpr std::size_t capacity() noexcept { return N; }

    void clear() noexcept { size_ = 0; head_ = 0; }

    /// Iterates newest to oldest, so range-based algorithms see the same order
    /// as operator[].
    class iterator {
    public:
        iterator(const RingBuffer* b, std::size_t i) : buf_(b), i_(i) {}
        const T& operator*() const { return (*buf_)[i_]; }
        iterator& operator++() { ++i_; return *this; }
        bool operator!=(const iterator& o) const { return i_ != o.i_; }
    private:
        const RingBuffer* buf_;
        std::size_t i_;
    };

    iterator begin() const { return iterator(this, 0); }
    iterator end() const { return iterator(this, size_); }

private:
    std::array<T, N> data_{};
    std::size_t head_ = N - 1;
    std::size_t size_ = 0;
};

}  // namespace quantiq
