#include "quantiq/http.hpp"

#include <curl/curl.h>

#include "quantiq/errors.hpp"

namespace quantiq {

namespace {

std::size_t append(void* contents, std::size_t size, std::size_t count, void* userp) {
    const std::size_t bytes = size * count;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), bytes);
    return bytes;
}

/// Owns the CURL handle so an early return or a throw cannot leak it.
class CurlHandle {
public:
    CurlHandle() : handle_(curl_easy_init()) {
        if (!handle_) throw ApiError("curl_easy_init failed");
    }
    ~CurlHandle() { curl_easy_cleanup(handle_); }
    CurlHandle(const CurlHandle&) = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;
    CURL* get() const noexcept { return handle_; }

private:
    CURL* handle_;
};

}  // namespace

std::string http_get(const std::string& url) {
    CurlHandle curl;
    std::string body;

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, append);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "quantiq/0.1");

    const CURLcode rc = curl_easy_perform(curl.get());
    if (rc != CURLE_OK) throw ApiError(std::string("http get failed: ") + curl_easy_strerror(rc));

    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status == 401 || status == 403) throw AuthFailed("rejected by " + url);
    if (status == 429) throw RateLimited("rate limited by " + url);
    if (status >= 400) throw ApiError("http " + std::to_string(status) + " from " + url);

    return body;
}

}  // namespace quantiq
