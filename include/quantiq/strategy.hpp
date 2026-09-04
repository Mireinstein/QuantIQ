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

/// A strategy sees one bar at a time and answers with a signal or with nothing.
/// Returning nullopt on most bars is the normal case, which is why the return
/// type is optional rather than a Signal carrying a "do nothing" side.
class Strategy {
public:
    virtual ~Strategy() = default;
    virtual std::optional<Signal> on_bar(const Bar& bar) = 0;
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
