#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

#include "quantiq/alpaca.hpp"
#include "quantiq/queue.hpp"
#include "quantiq/types.hpp"

namespace quantiq {

struct SessionOpen {
    Timestamp at;
};
struct SessionClose {
    Timestamp at;
};
struct FeedError {
    std::string what;
};

/// Everything the feed can say, in one type. A variant rather than a Bar with
/// nullable extras, because a session boundary and a transport error are not
/// bars with missing fields -- they are different things, and `visit` makes the
/// consumer handle each of them explicitly instead of forgetting one.
using FeedMessage = std::variant<Bar, SessionOpen, SessionClose, FeedError>;

/// Polls Alpaca for the latest daily bar per symbol on its own thread.
///
/// Daily bars are what the strategies consume, so a streaming socket would
/// deliver thousands of ticks an hour that nothing reads. Polling on an
/// interval is the honest shape for this data, and it keeps the threading
/// structure identical to what a socket would need.
///
/// The thread is started in the constructor and joined in the destructor, so
/// there is no way to leave it running behind a thrown exception.
class PollingFeed {
public:
    PollingFeed(AlpacaVenue& venue, std::vector<Symbol> symbols, BoundedQueue<FeedMessage>& out,
                std::chrono::seconds interval);
    ~PollingFeed();

    PollingFeed(const PollingFeed&) = delete;
    PollingFeed& operator=(const PollingFeed&) = delete;

    void stop();

private:
    void run();

    AlpacaVenue& venue_;
    std::vector<Symbol> symbols_;
    BoundedQueue<FeedMessage>& out_;
    std::chrono::seconds interval_;
    std::unordered_map<Symbol, Timestamp> last_seen_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

}  // namespace quantiq
