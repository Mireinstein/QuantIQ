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

## Money

Prices and cash are fixed-point integers with four decimal places, not doubles.
A cost basis that averages in binary floating point drifts, and the drift shows
up in realised profit.
