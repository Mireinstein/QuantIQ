#pragma once

#include <map>
#include <string>

namespace quantiq {

struct Response {
    long status = 0;
    std::string body;
};

/// Thin wrapper over libcurl. Holds the headers that every request to a given
/// host needs, so the venue code is not rebuilding an auth header on each call.
/// Status codes are mapped onto the exception hierarchy at the one place they
/// arrive, which is why callers never have to check a return code.
class HttpClient {
public:
    HttpClient() = default;
    explicit HttpClient(std::map<std::string, std::string> headers);

    std::string get(const std::string& url) const;
    std::string post(const std::string& url, const std::string& json_body) const;
    std::string del(const std::string& url) const;

    /// Uploads a body to a pre-signed URL. Used to publish the dashboard to
    /// blob storage, which needs no SDK and no CLI in the image -- the URL
    /// already carries the only credential involved.
    std::string put(const std::string& url, const std::string& body,
                    const std::map<std::string, std::string>& extra_headers = {}) const;

private:
    Response perform(const std::string& method, const std::string& url, const std::string& body,
                     const std::map<std::string, std::string>& extra) const;
    std::string check(const std::string& url, const Response& r) const;

    std::map<std::string, std::string> headers_;
};

/// Unauthenticated GET, for public endpoints such as the Yahoo bar download.
std::string http_get(const std::string& url);

/// Reads KEY=VALUE lines from a .env file into the process environment. Absent
/// files are not an error -- credentials may equally come from the environment
/// already, and anything already exported wins over the file.
void load_env_file(const std::string& path);

/// Looks for .env beside `near` and then in the working directory, and loads
/// the first one found.
///
/// Resolving it against the config file rather than the process's directory is
/// what lets the binary be run from anywhere; a lookup relative to the working
/// directory means it only ever works from inside the checkout.
void load_env_near(const std::string& near);

/// Throws ConfigError naming the variable, rather than returning an empty
/// string that fails later as a confusing 401.
std::string require_env(const std::string& name);

}  // namespace quantiq
