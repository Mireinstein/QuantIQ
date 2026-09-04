# Where this is

Working notes, not documentation. The README describes what the project does;
this file describes what is half-finished and what has not been decided.

## Done

Steps 1-5 of the plan, all committed:

1. `Price`/`Money` fixed-point, `RingBuffer<T,N>`, exception hierarchy
2. `Venue` and `Strategy` interfaces, `MockVenue`, `Portfolio`, `Risk`
3. `fetch-bars` (Yahoo, no key needed), CSV replay, `Journal`, `--report`
4. Four strategies behind a registry, `--strategy all` to compare them
5. `AlpacaVenue` -- account, clock, positions, history, order submission

34 tests, all offline. `./build/trader --check` reads the live paper account.

## Not done

6. Market data feed: websocket, `variant<Trade,Quote,Bar,Status>` + visit,
   into the queue `AlpacaVenue::push_bar` already writes to
7. Threads (receive / strategy / order), kill switch wired to live trading,
   state persisted across restarts
8. Market-clock loop so it runs itself daily, profiling pass, Doxygen

## The open design decision

Strategies currently return `optional<Signal>` -- "buy 50 shares". That is the
wrong shape for live trading and needs to change before step 7.

The standard design has strategies return a **target position** instead:

    virtual double target(const Bar&) = 0;   // 1.0 = fully long, 0.0 = flat

and a separate stage nets targets across strategies, sizes them, and diffs
against what the broker actually holds. That diff is what makes the bot
restartable: it asks Alpaca what it owns, compares to what it wants, trades the
gap. With "buy 50", a restart mid-session has no idea what it already holds, two
strategies on the same symbol fight, and a rejected order leaves state drifting.

Sizing follows the same staging. Right now every trade is a flat 50 shares,
which means 50 NVDA is a 44% position and 50 Ford is 0.6% -- so the report
cannot fairly compare strategies. Fixed-fractional (X% of equity) is the
baseline; volatility-scaled (`qty = risk_budget * equity / (ATR * price)`) is
the systematic-trading standard, and is worth doing once ATR exists.

## Known rough edges

- Replay fills at the close of the bar that produced the signal, so replay
  numbers are optimistic by roughly one bar's move.
- `MockVenue` ignores cash: it will "buy" past the account balance.
- Only `SmaCrossover` has had its warm-up behaviour tested carefully.
- Strategies hold a single `holding_` flag, so one instance is implicitly tied
  to one symbol. Multi-symbol needs one instance per strategy per symbol.

## Credentials

`.env` is gitignored and holds the Alpaca paper keys. `AlpacaVenue` refuses to
construct unless `ALPACA_BASE_URL` is the paper endpoint. A fresh clone needs
that file recreated -- see the README.
