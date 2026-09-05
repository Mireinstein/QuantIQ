# QuantIQ

A trading bot in C++20. It runs a strategy over market data and places the
resulting orders — against years of saved history in a couple of seconds, or
live against an Alpaca paper account.

Both modes run identical code. The strategy, portfolio, risk layer and journal
do not know where the bars came from; only the `Venue` implementation differs.

## Build

    cmake -B build && cmake --build build -j8 && ./build/tests

Dependencies are fetched by CMake (nlohmann/json, Catch2). libcurl comes with
the system.

## Use

Download bars, then replay a strategy over them:

    ./build/fetch-bars AAPL 5y
    ./build/trader --replay bars/AAPL-5y.csv --strategy SmaCrossover --set fast=9 --set slow=21

`--strategy all` runs every strategy over the same bars and reports them
together:

    ./build/trader --replay bars/AAPL-5y.csv --strategy all

    strategy          trades    win%         net      max dd
    --------------------------------------------------------
    Breakout              14    57.1     3080.00    -1520.50
    MeanReversion         22    72.7     3259.00     -449.50
    Momentum              24    33.3     3539.50    -1837.00
    SmaCrossover          28    46.4     2931.00    -2401.50
    --------------------------------------------------------
    total                 88    51.1    12809.50    -2401.50

AAPL, 1255 daily bars, 2021-09 to 2026-09, 50 shares a trade. Replay fills at
the close of the bar that produced the signal, so these numbers are optimistic
by roughly one bar's move.

## Live paper trading

    ./build/trader --check                  # credentials, account, market clock, positions
    ./build/trader --test-order AAPL 1      # places one real paper order
    ./build/trader --live --config config.json

`--live` runs until stopped. It asks Alpaca when the market next opens rather
than hardcoding a calendar, so nights, weekends and holidays are handled by
sleeping. At each session start it reads the account's real positions and adopts
them, so a restart does not mistake a held position for a flat one. Ctrl-C stops
the feed thread, joins it, and flushes the journal.

Three threads: one polls for bars, one turns them into orders, and a signal
handler can stop both. Feed messages are a `variant<Bar, SessionOpen,
SessionClose, FeedError>` so the consumer has to handle each kind rather than
checking a nullable field.

Each symbol belongs to exactly one strategy — the config is rejected otherwise,
since two strategies setting one position would undo each other and the report
could no longer say which earned what.

`AlpacaVenue` refuses to construct unless `ALPACA_BASE_URL` points at
`paper-api.alpaca.markets`, so a typo cannot reach a live account. Credentials
come from a gitignored `.env`; anything already exported wins, so a deployment
overrides the file without editing it.

## Dashboard

    ./build/trader --dashboard --journal journal/trades.jsonl -o dashboard.html

Writes one self-contained HTML file: a strategy table, each strategy's equity
against buying the instrument and holding it, an underwater plot, and every
closed trade newest first. No server and nothing fetched at runtime, so it opens
offline — the charts are inline SVG rather than a library, and a page that pulls
a script from a CDN stops working exactly when you want to read it.

Every strategy is drawn against buy-and-hold on a shared scale, because a rising
curve looks like success even when doing nothing would have risen faster. On
five years of AAPL, all four look like this:

| Strategy | Trades | Return | Held | Sharpe | Max DD | Exposure |
|---|---|---|---|---|---|---|
| Breakout | 16 | 4.4% | 104.2% | 0.47 | -2.4% | 57% |
| MeanReversion | 22 | 3.6% | 104.2% | 0.50 | -1.7% | 16% |
| Momentum | 24 | 4.7% | 104.2% | 0.56 | -2.5% | 46% |
| SmaCrossover | 29 | 3.5% | 104.2% | 0.36 | -3.0% | 57% |

Each one lost to holding the stock, by a lot. Most of that is structural rather
than a verdict on the rules: a position is 10% of equity and there is one
symbol, so 90% of the capital never leaves cash while the benchmark is fully
invested throughout. The metrics say the rest — Sharpe under 0.6, and exposure
showing MeanReversion earned its return in 16% of the days.

The journal is the only source of truth. Regenerate the page whenever; throwing
it away costs nothing.

## Sizing

Positions are sized as a fraction of equity, not as a share count. Fifty shares
of a $880 stock is a 44% position on a $100k account and fifty shares of a $12
stock is under 1%, so a report comparing strategies by share count is mostly
comparing which of them traded expensive names.

A rebalance band (default 20%) stops the bot chasing its own target: as a price
rises, a fixed weight implies fewer shares, and without the band it would sell a
handful every bar to correct drift it was never asked to correct.

## Strategies

| Name | Rule | Parameters |
|---|---|---|
| SmaCrossover | Buy when the fast mean crosses above the slow one, sell on the reverse | fast, slow, size |
| MeanReversion | Buy at `z_entry` standard deviations below the mean, exit at `z_exit` | window, z_entry, z_exit, size |
| Momentum | Buy when the lookback return clears `threshold`, exit when it turns negative | lookback, threshold, size |
| Breakout | Buy a close above the lookback high, exit below the lookback low | lookback, size |

## Risk

Orders pass through `Risk::allow` before reaching the venue. It refuses sells
with no position behind them and caps how many names can be open at once. A
drawdown past the limit trips a halt, and once halted nothing further is sent
for the rest of the session.

## Layout

    include/quantiq/    headers
    src/                implementations, trader and fetch-bars entry points
    tests/              Catch2, 32 cases, all offline

## Performance

Replaying 1,255 daily bars through all four strategies, best of three runs:

| Build | Time |
|---|---|
| `-O0` | 30.7 ms |
| `-O3` | 13.0 ms |

2.4x, for 5,020 bar-evaluations. Worth keeping in proportion: the bot makes one
decision per symbol per day, and an Alpaca round trip is 20–200 ms, so the
optimisation matters to replay and to nothing else.

## Docs

    doxygen Doxyfile && open docs/html/index.html

## Money

Prices and cash are fixed-point integers with four decimal places, not doubles.
A cost basis that averages in binary floating point drifts, and the drift shows
up in realised profit.
