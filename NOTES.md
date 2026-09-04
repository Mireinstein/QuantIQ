# Where this is

Working notes, not documentation. The README describes what the project does;
this file describes what is unfinished and what was deliberately left out.

## Done

All eight planned steps.

1. `Price`/`Money` fixed-point, `RingBuffer<T,N>`, exception hierarchy
2. `Venue` and `Strategy` interfaces, `MockVenue`, `Portfolio`, `Risk`
3. `fetch-bars` (Yahoo, no key needed), CSV replay, `Journal`, `--report`
4. Four strategies behind a registry, `--strategy all` to compare them
5. `AlpacaVenue` -- account, clock, positions, history, order submission
6. `PollingFeed` on its own thread, `variant<Bar, SessionOpen, SessionClose,
   FeedError>` messages over a bounded queue
7. Live trading: reconciliation against the broker at session start, atomic
   kill switch, clean shutdown on SIGINT
8. Market-clock loop so it runs itself daily, -O0 vs -O3 measurement, Doxygen

52 tests, all offline. `--check` and `--live` both verified against the live
paper account.

## Decisions that changed along the way

**Strategies return a target, not a buy/sell.** The first version had
`on_bar` return `optional<Signal>` meaning "buy 50 shares". That cannot survive
a restart (the bot has no idea what it already holds), lets two strategies fight
over one symbol, and drifts silently when the broker rejects an order. Now
`on_bar` returns a `Target` weight and the engine trades the difference against
what is actually held.

**Sizing is by value, not by share count.** Fifty shares of a $880 stock and
fifty of a $12 stock are not the same bet, and a report comparing strategies by
share count mostly compares which of them traded expensive names.

**A rebalance band exists because the first version churned.** Holding a fixed
weight while a price rises means steadily fewer shares, so the engine was
selling a handful every bar to chase its own target. Entries and exits always
trade; drift only trades once it exceeds the band.

**Polling, not a websocket.** The strategies consume daily bars. A streaming
socket would deliver thousands of ticks an hour that nothing reads. The thread
structure is what a socket would need, so swapping the feed later changes one
class.

## Left out on purpose

- Volatility-scaled sizing. Fixed-fractional is the baseline; scaling by ATR so
  each position carries equal *risk* rather than equal *dollars* is the
  systematic-trading standard and is the natural next change.
- Shorting. Targets are clamped to 0..1.
- Multiple symbols per strategy instance. One instance holds the state of one
  position, so the live trader builds one per symbol.

## Known rough edges

- Replay fills at the close of the bar that produced the signal. Real fills
  happen at the next available price, so replay numbers are optimistic by
  roughly one bar's move.
- `--report` pairs exits against entries FIFO per strategy and symbol; a
  position still open contributes nothing to the totals.
- The feed polls every symbol on one interval. With a large universe that
  becomes the bottleneck long before anything else does.

## Credentials

`.env` is gitignored and holds the Alpaca paper keys. `AlpacaVenue` refuses to
construct unless `ALPACA_BASE_URL` is the paper endpoint. A fresh clone needs
that file recreated -- see the README.
