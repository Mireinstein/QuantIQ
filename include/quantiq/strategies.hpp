#pragma once

#include "quantiq/ringbuffer.hpp"
#include "quantiq/strategy.hpp"

namespace quantiq {

/// Registers every built-in strategy. Called once at startup; without it the
/// registry is empty and `--strategy` cannot resolve a name.
void register_builtin_strategies();

/// Buys when a short moving average rises through a long one and sells when it
/// falls back through. The classic trend-following rule, and the simplest thing
/// that produces both entries and exits from one signal.
class SmaCrossover : public Strategy {
public:
    explicit SmaCrossover(const StrategyParams& p);
    Target on_bar(const Bar& bar) override;
    std::string name() const override { return "SmaCrossover"; }

private:
    std::size_t fast_;
    std::size_t slow_;
    RingBuffer<double, 256> closes_;
    bool was_above_ = false;
    bool primed_ = false;
    bool holding_ = false;
};

/// Treats price as oscillating around its recent mean and bets on the return.
/// Entry is at `z_entry` standard deviations below the mean, exit once price
/// has recovered to `z_exit`.
class MeanReversion : public Strategy {
public:
    explicit MeanReversion(const StrategyParams& p);
    Target on_bar(const Bar& bar) override;
    std::string name() const override { return "MeanReversion"; }

private:
    std::size_t window_;
    double z_entry_;
    double z_exit_;
    RingBuffer<double, 256> closes_;
    bool holding_ = false;
};

/// The opposite premise to MeanReversion: buys what has already gone up, on the
/// assumption the move continues.
class Momentum : public Strategy {
public:
    explicit Momentum(const StrategyParams& p);
    Target on_bar(const Bar& bar) override;
    std::string name() const override { return "Momentum"; }

private:
    std::size_t lookback_;
    double threshold_;
    RingBuffer<double, 256> closes_;
    bool holding_ = false;
};

/// Buys a close above the highest high of the lookback window, exits on a close
/// below the lowest low.
class Breakout : public Strategy {
public:
    explicit Breakout(const StrategyParams& p);
    Target on_bar(const Bar& bar) override;
    std::string name() const override { return "Breakout"; }

private:
    std::size_t lookback_;
    RingBuffer<double, 256> highs_;
    RingBuffer<double, 256> lows_;
    bool holding_ = false;
};

}  // namespace quantiq
