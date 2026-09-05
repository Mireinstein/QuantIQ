#include "quantiq/http.hpp"

#include <curl/curl.h>

#include <cstdlib>
#include <fstream>

#include "quantiq/errors.hpp"

namespace quantiq {

namespace {

std::size_t append(void* contents, std::size_t size, std::size_t count, void* userp) {
    const std::size_t bytes = size * count;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), bytes);
    return bytes;
}

/// Owns the CURL handle and its header list so an early return or a throw
/// cannot leak either.
class CurlHandle {
public:
    CurlHandle() : handle_(curl_easy_init()) {
        if (!handle_) throw ApiError("curl_easy_init failed");
    }
    ~CurlHandle() {
        if (list_) curl_slist_free_all(list_);
        curl_easy_cleanup(handle_);
    }
    CurlHandle(const CurlHandle&) = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;

    CURL* get() const noexcept { return handle_; }
    void add_header(const std::string& header) {
        list_ = curl_slist_append(list_, header.c_str());
    }
    curl_slist* headers() const noexcept { return list_; }

private:
    CURL* handle_;
    curl_slist* list_ = nullptr;
};

}  // namespace

HttpClient::HttpClient(std::map<std::string, std::string> headers)
    : headers_(std::move(headers)) {}

Response HttpClient::perform(const std::string& method, const std::string& url,
                             const std::string& body,
                             const std::map<std::string, std::string>& extra) const {
    CurlHandle curl;
    Response response;

    for (const auto& [name, value] : headers_) curl.add_header(name + ": " + value);
    for (const auto& [name, value] : extra) curl.add_header(name + ": " + value);
    if (!body.empty() && extra.find("Content-Type") == extra.end()) {
        curl.add_header("Content-Type: application/json");
    }

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, append);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "quantiq/0.1");
    if (curl.headers()) curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, curl.headers());

    if (method == "POST") {
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
    } else if (method == "PUT") {
        curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    } else if (method != "GET") {
        curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, method.c_str());
    }

    const CURLcode rc = curl_easy_perform(curl.get());
    if (rc != CURLE_OK) throw ApiError(std::string("http failed: ") + curl_easy_strerror(rc));
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

std::string HttpClient::check(const std::string& url, const Response& r) const {
    // Mapped here rather than at each call site, so a caller can only ever see
    // a body it can parse or an exception naming what went wrong.
    if (r.status == 401 || r.status == 403) throw AuthFailed("rejected by " + url + ": " + r.body);
    if (r.status == 429) throw RateLimited("rate limited by " + url);
    if (r.status == 422) throw OrderRejected(r.body);
    if (r.status >= 400) throw ApiError("http " + std::to_string(r.status) + " from " + url + ": " + r.body);
    return r.body;
}

std::string HttpClient::get(const std::string& url) const {
    return check(url, perform("GET", url, "", {}));
}

std::string HttpClient::put(const std::string& url, const std::string& body,
                            const std::map<std::string, std::string>& extra_headers) const {
    return check(url, perform("PUT", url, body, extra_headers));
}

std::string HttpClient::post(const std::string& url, const std::string& json_body) const {
    return check(url, perform("POST", url, json_body, {}));
}

std::string HttpClient::del(const std::string& url) const {
    return check(url, perform("DELETE", url, "", {}));
}

std::string http_get(const std::string& url) { return HttpClient{}.get(url); }

void load_env_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (!value.empty() && value.back() == '\r') value.pop_back();

        // Anything already exported wins, so a deployment can override the file
        // without editing it.
        setenv(key.c_str(), value.c_str(), 0);
    }
}

std::string require_env(const std::string& name) {
    const char* value = std::getenv(name.c_str());
    if (value == nullptr || *value == '\0') {
        throw ConfigError(name + " is not set (put it in .env or export it)");
    }
    return value;
}

}  // namespace quantiq
