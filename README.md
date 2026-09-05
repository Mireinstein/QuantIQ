# QuantIQ

A trading bot in C++20. It runs a strategy over market data and places the
resulting orders — against years of saved history in a couple of seconds, or
live against an Alpaca paper account.

Both modes run identical code. The strategy, portfolio, risk layer and journal
do not know where the bars came from; only the `Venue` implementation differs.

## Getting started

    cmake -B build && cmake --build build -j && ctest --test-dir build
    ./build/trader --init          # writes .env, then checks your keys work
    ./build/fetch-bars AAPL 5y
    ./build/trader --replay bars/AAPL-5y.csv --strategy all

Needs CMake 3.20+, a C++20 compiler, and network on the first build — nlohmann/json
and Catch2 are fetched by CMake. libcurl comes with the system.

Paper keys are free from
[Alpaca](https://app.alpaca.markets/paper/dashboard/overview). `AlpacaVenue`
refuses to construct unless `ALPACA_BASE_URL` is the paper endpoint, so a typo
cannot reach a live account. The test suite runs entirely against a mock and
needs no keys at all.

## Replay

    ./build/trader --replay bars/AAPL-5y.csv --strategy SmaCrossover --set fast=9 --set slow=21

`--strategy all` runs every strategy over the same bars and reports them
together. On five years of AAPL:

| Strategy | Trades | Return | Held | Sharpe | Max DD | Exposure |
|---|---|---|---|---|---|---|
| Breakout | 16 | 4.4% | 104.2% | 0.47 | -2.4% | 57% |
| MeanReversion | 22 | 3.6% | 104.2% | 0.50 | -1.7% | 16% |
| Momentum | 24 | 4.7% | 104.2% | 0.56 | -2.5% | 46% |
| SmaCrossover | 29 | 3.5% | 104.2% | 0.36 | -3.0% | 57% |

Every one lost to holding the stock. Most of that is structural rather than a
verdict on the rules: a position is 10% of equity and there is one symbol, so
90% of the capital never leaves cash while the benchmark is fully invested
throughout.

Replay fills at the close of the bar that produced the signal. Real fills happen
at the next available price, so these numbers are optimistic by roughly one
bar's move.

## Live paper trading

    ./build/trader --check                    # account, clock, positions
    ./build/trader --once --dry-run           # decide, print, send nothing
    ./build/trader --once                     # one pass, then exit
    ./build/trader --live                     # runs until stopped

`--once` is a single pass: it warms each strategy on 250 bars of history, acts
only on a completed bar, and skips any symbol that already has an order working.
`--live` runs until stopped, asking Alpaca when the market next opens rather than
hardcoding a calendar, so nights, weekends and holidays are handled by sleeping.
Ctrl-C stops the feed thread, joins it, and flushes the journal.

At each session start it reads the account's real positions and adopts them, so
a restart does not mistake a held position for a flat one.

Three threads: one polls for bars, one turns them into orders, and a signal
handler can stop both. Feed messages are a `variant<Bar, SessionOpen,
SessionClose, FeedError>` so the consumer handles each kind explicitly rather
than checking a nullable field.

Each symbol belongs to exactly one strategy — the config is rejected otherwise,
since two strategies setting one position would undo each other and the report
could no longer say which earned what.

## Dashboard

    ./build/trader --dashboard --journal journal/trades.jsonl -o dashboard.html

One self-contained HTML file: a strategy table, each strategy's equity against
buying the instrument and holding it, an underwater plot, and every closed trade
newest first. Nothing is fetched at runtime, so it opens offline — the charts
are inline SVG rather than a library.

The journal is the only source of truth. Regenerate the page whenever; throwing
it away costs nothing.

## Adding a strategy

A strategy answers what it wants to **hold**, not what to do. Returning `0.0`
does not mean "no opinion this bar" — it means "be flat", and if a position is
open it will be sold. You must answer every bar.

```cpp
#include "quantiq/strategy.hpp"
using namespace quantiq;

class MyStrategy : public Strategy {
public:
    explicit MyStrategy(const StrategyParams& p)      // params come from config.json
        : threshold_(p.get("threshold", 0.02)) {}

    Target on_bar(const Bar& bar) override {
        closes_.push(bar.close.to_double());
        if (closes_.size() < 20) return Target{0.0, "warming up"};

        return going_up() ? Target{1.0, "why I want to be long"}
                          : Target{0.0, "why I want to be flat"};
    }

    std::string name() const override { return "MyStrategy"; }

private:
    double threshold_;
    RingBuffer<double, 256> closes_;
};
```

Register it, name it in `config.json`, add the `.cpp` to `CMakeLists.txt`, and
rebuild:

```cpp
// in register_builtin_strategies()
reg.add("MyStrategy", [](const StrategyParams& p) { return std::make_unique<MyStrategy>(p); });
```

```json
{ "name": "MyStrategy", "symbols": ["TSLA"], "params": { "threshold": 0.03 } }
```

Sizing, the rebalance band, risk limits, order placement, the journal and the
dashboard all come for free. The `reason` string ends up in the journal, so the
report can say why a trade happened.

## Strategies

| Name | Rule | Parameters |
|---|---|---|
| SmaCrossover | Buy when the fast mean crosses above the slow one, sell on the reverse | fast, slow |
| MeanReversion | Buy at `z_entry` standard deviations below the mean, exit at `z_exit` | window, z_entry, z_exit |
| Momentum | Buy when the lookback return clears `threshold`, exit when it turns negative | lookback, threshold |
| Breakout | Buy a close above the lookback high, exit below the lookback low | lookback |

## Sizing

Positions are sized as a fraction of equity, not as a share count. Fifty shares
of a $880 stock is a 44% position on a $100k account and fifty shares of a $12
stock is under 1%, so a report comparing strategies by share count is mostly
comparing which of them traded expensive names.

A rebalance band (default 20%) stops the bot chasing its own target: as a price
rises, a fixed weight implies fewer shares, and without the band it would sell a
handful every bar to correct drift it was never asked to correct.

## Risk

Orders pass through `Risk::allow` before reaching the venue. It refuses sells
with no position behind them, refuses buys the cash cannot cover, and caps how
many names can be open at once. A drawdown past the limit trips a halt, and once
halted nothing further is sent for the rest of the session. Ten consecutive feed
errors trip it too, since trading on stale prices is worse than not trading.

## Money

Prices and cash are fixed-point integers with four decimal places, not doubles.
A cost basis that averages in binary floating point drifts, and the drift shows
up in realised profit.

## Performance

Replaying 1,255 daily bars through all four strategies, best of three:

| Build | Time |
|---|---|
| `-O0` | 241.5 ms |
| `-O3` | 55.6 ms |

4.3x, for 5,020 bar-evaluations and about as many journal lines. Worth keeping
in proportion: the bot makes one decision per symbol per day, and an Alpaca
round trip is 20–200 ms.

## Layout

    include/quantiq/    headers
    src/                implementations, trader and fetch-bars entry points
    tests/              Catch2, 66 cases, all offline

    doxygen Doxyfile && open docs/html/index.html
