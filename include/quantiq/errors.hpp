#pragma once

#include <stdexcept>
#include <string>

namespace quantiq {

/// One root for everything this project throws, so a caller that only wants to
/// log and carry on can catch a single type.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ConfigError : public Error {
public:
    using Error::Error;
};

/// Bad or missing market data: an unreadable CSV, a malformed bar, a gap where
/// a price should be.
class DataError : public Error {
public:
    using Error::Error;
};

/// Anything the broker rejected or could not answer. The subclasses matter
/// because the correct response differs: retry a RateLimited, stop entirely on
/// an AuthFailed, skip one order on an OrderRejected.
class ApiError : public Error {
public:
    using Error::Error;
};

class AuthFailed : public ApiError {
public:
    using ApiError::ApiError;
};

class RateLimited : public ApiError {
public:
    using ApiError::ApiError;
};

class OrderRejected : public ApiError {
public:
    using ApiError::ApiError;
};

class InsufficientFunds : public ApiError {
public:
    using ApiError::ApiError;
};

/// Raised by the risk layer when a limit would be breached. Not an ApiError:
/// the broker never saw the order, we stopped it ourselves.
class RiskBreach : public Error {
public:
    using Error::Error;
};

}  // namespace quantiq
