#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "quantiq/types.hpp"

namespace quantiq {

struct StrategyParams {
    std::map<std::string, double> values;

    double get(const std::string& key, double fallback) const {
        auto it = values.find(key);
        return it == values.end() ? fallback : it->second;
    }
};

/// What a strategy wants, rather than what it wants done. `weight` is exposure
/// to this symbol: 1.0 is a full position, 0.0 is flat.
///
/// Returning a target instead of a buy/sell instruction is what makes the bot
/// restartable and safe to run more than one strategy against: the engine
/// compares the target to what is actually held and trades only the gap, so a
/// repeated signal places nothing, a restart mid-session recovers by reading
/// the broker, and a rejected order simply gets re-attempted on the next bar
/// instead of leaving the position silently wrong.
struct Target {
    double weight = 0.0;
    std::string reason;
};

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual Target on_bar(const Bar& bar) = 0;
    virtual std::string name() const = 0;
};

/// Names to constructors, so a strategy can be chosen from a config file or a
/// command line flag without the engine knowing which types exist.
class StrategyRegistry {
public:
    using Factory = std::function<std::unique_ptr<Strategy>(const StrategyParams&)>;

    static StrategyRegistry& instance();

    void add(const std::string& name, Factory factory);
    std::unique_ptr<Strategy> create(const std::string& name, const StrategyParams& params) const;
    std::vector<std::string> names() const;

private:
    std::map<std::string, Factory> factories_;
};

}  // namespace quantiq
