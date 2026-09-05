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
9. `--dashboard`: the journal rendered as one static HTML file, with each
   strategy drawn against buy-and-hold, an underwater plot, and the standard
   ratios
10. Packaged for other people: LICENSE, `--init`, `--dry-run`, CI on Linux and
    macOS, and a dashboard published from replay

66 tests, all offline. `--check` and `--live` both verified against the live
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

**The dashboard generates a file, it does not serve one.** A read-only view has
no reason to be a service: the journal is the only state, so the page can be
regenerated from it at any point and thrown away afterwards. That also keeps
Python out of a C++ project for the sake of a chart.

**The curve is drawn from daily marks, not from closed trades.** The first
version plotted cumulative profit indexed by trade number, which cannot be lined
up against the market, cannot show when a drawdown happened, and gives two
strategies incomparable x-axes when they trade different numbers of times. The
engine now journals equity, close and position size every bar.

**Reporting in dollars hid the result.** "$4,314 net" reads as a success. "4.4%
against 104.2% for holding the stock" does not. Every strategy here loses to
buy-and-hold, and a report that could not say so was not worth reading.

**`--dry-run` exists because I kept placing real orders by accident.** Running
`--once` to check the wiring put live paper orders on the account twice. If that
happens to the person who wrote it, it happens to everyone trying it for the
first time.

**The journal is flushed on fills, not on marks.** Flushing every line cost 28x
on a replay (13ms to 367ms for 5,020 lines). Fills and rejections still flush at
once because a crash must not lose a trade that really happened; readers call
`flush()` before parsing, since the report and the dashboard read the file while
the bot still has it open.

## Left out on purpose

- A parameter sweep. Running the replay across a grid of parameters and
  colouring the results would show whether a good number sits on a robust
  plateau or is a single lucky cell -- and it is the natural first use of a
  thread pool. Not built.
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
construct unless `ALPACA_BASE_URL` is the paper endpoint. `--init` writes the
file and then verifies the keys reach a paper account.

The lookup resolves `.env` beside the config file and then in the working
directory. It used to be working-directory only, which meant the binary ran from
nowhere but the checkout.
