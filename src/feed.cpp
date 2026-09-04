#include "quantiq/feed.hpp"

#include "quantiq/errors.hpp"

namespace quantiq {

PollingFeed::PollingFeed(AlpacaVenue& venue, std::vector<Symbol> symbols,
                         BoundedQueue<FeedMessage>& out, std::chrono::seconds interval)
    : venue_(venue), symbols_(std::move(symbols)), out_(out), interval_(interval) {
    thread_ = std::thread(&PollingFeed::run, this);
}

PollingFeed::~PollingFeed() {
    stop();
    if (thread_.joinable()) thread_.join();
}

void PollingFeed::stop() { stop_.store(true); }

void PollingFeed::run() {
    while (!stop_.load()) {
        for (const auto& symbol : symbols_) {
            if (stop_.load()) return;

            try {
                const auto bars = venue_.history(symbol, 2);
                if (bars.empty()) continue;

                const Bar& latest = bars.back();
                auto& seen = last_seen_[symbol];

                // A bar is only news once. Alpaca keeps returning today's bar
                // as it updates, and acting on each revision would trade the
                // same day repeatedly.
                if (seen != Timestamp{} && latest.ts <= seen) continue;
                seen = latest.ts;

                if (!out_.push(latest)) return;
            } catch (const Error& e) {
                // A feed error is data, not a reason to take the thread down --
                // the next poll may well succeed, and the consumer decides
                // whether a run of them matters.
                if (!out_.push(FeedError{e.what()})) return;
            }
        }

        // Woken in short slices so a stop is acted on promptly rather than
        // after a full interval.
        for (int i = 0; i < interval_.count() && !stop_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

}  // namespace quantiq
