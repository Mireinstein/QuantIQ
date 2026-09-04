#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "quantiq/journal.hpp"
#include "quantiq/portfolio.hpp"
#include "quantiq/risk.hpp"
#include "quantiq/strategy.hpp"
#include "quantiq/venue.hpp"

namespace quantiq {

struct EngineStats {
    std::size_t bars = 0;
    std::size_t signals = 0;
    std::size_t fills = 0;
    std::size_t rejected = 0;
};

/// The loop: pull a bar, show it to the strategy, put any resulting order past
/// the risk layer, send what survives to the venue, book the fill.
///
/// Nothing here knows whether the bars came from a CSV or a websocket. That is
/// the whole reason the Venue interface exists.
class Engine {
public:
    Engine(Venue& venue, Strategy& strategy, Journal& journal, Risk& risk);

    /// Runs until the venue stops producing bars. Returns what happened.
    EngineStats run(bool verbose = false);

    const Portfolio& portfolio() const noexcept { return portfolio_; }
    const std::unordered_map<Symbol, Price>& marks() const noexcept { return marks_; }

private:
    void handle(const Bar& bar, const Signal& signal, bool verbose);

    Venue& venue_;
    Strategy& strategy_;
    Journal& journal_;
    Risk& risk_;
    Portfolio portfolio_;
    std::unordered_map<Symbol, Price> marks_;
    EngineStats stats_;
};

}  // namespace quantiq
