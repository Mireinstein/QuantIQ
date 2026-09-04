#pragma once

#include <string>

namespace quantiq {

/// Minimal HTTPS GET over libcurl, which ships with macOS. Used by fetch-bars
/// now and by the Alpaca venue later; keeping it in one place means the retry
/// and error-mapping behaviour is written once.
std::string http_get(const std::string& url);

}  // namespace quantiq
