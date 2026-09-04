#include "quantiq/strategies.hpp"

#include <cmath>
#include <numeric>

#include "quantiq/errors.hpp"

namespace quantiq {

namespace {

double mean_of_last(const RingBuffer<double, 256>& buf, std::size_t n) {
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) sum += buf[i];
    return sum / static_cast<double>(n);
}

double stdev_of_last(const RingBuffer<double, 256>& buf, std::size_t n, double mean) {
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = buf[i] - mean;
        acc += d * d;
    }
    return std::sqrt(acc / static_cast<double>(n));
}

std::size_t window(const StrategyParams& p, const std::string& key, double fallback) {
    const double v = p.get(key, fallback);
    if (v < 2 || v > 250) throw ConfigError(key + " must be between 2 and 250");
    return static_cast<std::size_t>(v);
}

}  // namespace

SmaCrossover::SmaCrossover(const StrategyParams& p)
    : fast_(window(p, "fast", 9)),
      slow_(window(p, "slow", 21)),
      size_(static_cast<Quantity>(p.get("size", 50))) {
    if (fast_ >= slow_) throw ConfigError("fast window must be shorter than slow");
}

std::optional<Signal> SmaCrossover::on_bar(const Bar& bar) {
    closes_.push(bar.close.to_double());
    if (closes_.size() < slow_) return std::nullopt;

    const double fast = mean_of_last(closes_, fast_);
    const double slow = mean_of_last(closes_, slow_);
    const bool above = fast > slow;

    // The first bar with enough history only establishes which side we are on;
    // calling that a crossing would fabricate a trade out of the warm-up.
    if (!primed_) {
        primed_ = true;
        was_above_ = above;
        return std::nullopt;
    }

    const bool crossed = above != was_above_;
    was_above_ = above;
    if (!crossed) return std::nullopt;

    if (above && !holding_) {
        holding_ = true;
        return Signal{Side::Buy, size_, "fast MA crossed above slow"};
    }
    if (!above && holding_) {
        holding_ = false;
        return Signal{Side::Sell, size_, "fast MA crossed below slow"};
    }
    return std::nullopt;
}

MeanReversion::MeanReversion(const StrategyParams& p)
    : window_(window(p, "window", 20)),
      z_entry_(p.get("z_entry", -2.0)),
      z_exit_(p.get("z_exit", -0.5)),
      size_(static_cast<Quantity>(p.get("size", 50))) {}

std::optional<Signal> MeanReversion::on_bar(const Bar& bar) {
    closes_.push(bar.close.to_double());
    if (closes_.size() < window_) return std::nullopt;

    const double mean = mean_of_last(closes_, window_);
    const double sd = stdev_of_last(closes_, window_, mean);
    if (sd == 0.0) return std::nullopt;

    const double z = (closes_[0] - mean) / sd;

    if (!holding_ && z <= z_entry_) {
        holding_ = true;
        return Signal{Side::Buy, size_, "z=" + std::to_string(z) + " below entry"};
    }
    if (holding_ && z >= z_exit_) {
        holding_ = false;
        return Signal{Side::Sell, size_, "z=" + std::to_string(z) + " reverted"};
    }
    return std::nullopt;
}

Momentum::Momentum(const StrategyParams& p)
    : lookback_(window(p, "lookback", 20)),
      threshold_(p.get("threshold", 0.05)),
      size_(static_cast<Quantity>(p.get("size", 50))) {}

std::optional<Signal> Momentum::on_bar(const Bar& bar) {
    closes_.push(bar.close.to_double());
    if (closes_.size() <= lookback_) return std::nullopt;

    const double then = closes_[lookback_];
    if (then == 0.0) return std::nullopt;
    const double change = closes_[0] / then - 1.0;

    if (!holding_ && change > threshold_) {
        holding_ = true;
        return Signal{Side::Buy, size_, "up " + std::to_string(change * 100.0) + "% over lookback"};
    }
    if (holding_ && change < 0.0) {
        holding_ = false;
        return Signal{Side::Sell, size_, "momentum turned negative"};
    }
    return std::nullopt;
}

Breakout::Breakout(const StrategyParams& p)
    : lookback_(window(p, "lookback", 20)),
      size_(static_cast<Quantity>(p.get("size", 50))) {}

std::optional<Signal> Breakout::on_bar(const Bar& bar) {
    const double close = bar.close.to_double();

    // Compared against the window as it stood *before* this bar, since a bar
    // cannot break out above a high it set itself.
    std::optional<double> highest;
    std::optional<double> lowest;
    if (highs_.size() >= lookback_) {
        double hi = highs_[0];
        double lo = lows_[0];
        for (std::size_t i = 1; i < lookback_; ++i) {
            hi = std::max(hi, highs_[i]);
            lo = std::min(lo, lows_[i]);
        }
        highest = hi;
        lowest = lo;
    }

    highs_.push(bar.high.to_double());
    lows_.push(bar.low.to_double());

    if (!highest) return std::nullopt;

    if (!holding_ && close > *highest) {
        holding_ = true;
        return Signal{Side::Buy, size_, "closed above " + std::to_string(lookback_) + "-bar high"};
    }
    if (holding_ && close < *lowest) {
        holding_ = false;
        return Signal{Side::Sell, size_, "closed below " + std::to_string(lookback_) + "-bar low"};
    }
    return std::nullopt;
}

void register_builtin_strategies() {
    auto& reg = StrategyRegistry::instance();
    reg.add("SmaCrossover", [](const StrategyParams& p) { return std::make_unique<SmaCrossover>(p); });
    reg.add("MeanReversion", [](const StrategyParams& p) { return std::make_unique<MeanReversion>(p); });
    reg.add("Momentum", [](const StrategyParams& p) { return std::make_unique<Momentum>(p); });
    reg.add("Breakout", [](const StrategyParams& p) { return std::make_unique<Breakout>(p); });
}

StrategyRegistry& StrategyRegistry::instance() {
    static StrategyRegistry registry;
    return registry;
}

void StrategyRegistry::add(const std::string& name, Factory factory) {
    factories_[name] = std::move(factory);
}

std::unique_ptr<Strategy> StrategyRegistry::create(const std::string& name,
                                                   const StrategyParams& params) const {
    auto it = factories_.find(name);
    if (it == factories_.end()) throw ConfigError("unknown strategy: " + name);
    return it->second(params);
}

std::vector<std::string> StrategyRegistry::names() const {
    std::vector<std::string> out;
    for (const auto& [name, _] : factories_) out.push_back(name);
    return out;
}

}  // namespace quantiq
