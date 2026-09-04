#include <filesystem>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

#include "quantiq/errors.hpp"
#include "quantiq/http.hpp"
#include "quantiq/types.hpp"

using namespace quantiq;

namespace {

/// Yahoo's chart endpoint needs no account, which keeps replay usable before
/// any broker credentials exist. Live trading uses Alpaca; the two only have to
/// agree on the shape of a bar, not on the vendor.
std::string chart_url(const std::string& symbol, const std::string& range) {
    return "https://query1.finance.yahoo.com/v8/finance/chart/" + symbol + "?range=" + range +
           "&interval=1d";
}

int usage() {
    std::cerr << "usage: fetch-bars SYMBOL [RANGE] [-o OUT.csv]\n"
              << "  RANGE is a Yahoo window such as 1y, 5y, 10y, max (default 5y)\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();

    const std::string symbol = argv[1];
    std::string range = "5y";
    std::string out_path;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (arg.rfind('-', 0) != 0) {
            range = arg;
        } else {
            return usage();
        }
    }
    if (out_path.empty()) out_path = "bars/" + symbol + "-" + range + ".csv";

    try {
        const auto body = http_get(chart_url(symbol, range));
        const auto json = nlohmann::json::parse(body);

        const auto& result = json.at("chart").at("result").at(0);
        const auto& stamps = result.at("timestamp");
        const auto& quote = result.at("indicators").at("quote").at(0);

        std::filesystem::create_directories(std::filesystem::path(out_path).parent_path());
        std::ofstream out(out_path);
        if (!out) throw DataError("cannot write " + out_path);
        out << "symbol,date,open,high,low,close,volume\n";

        std::size_t written = 0;
        for (std::size_t i = 0; i < stamps.size(); ++i) {
            // Yahoo emits nulls for halted or untraded sessions; a bar with a
            // missing close cannot be acted on, so it is dropped rather than
            // guessed at.
            if (quote.at("close").at(i).is_null()) continue;

            const auto ts = Timestamp{std::chrono::seconds{stamps.at(i).get<std::int64_t>()}};
            out << symbol << ',' << to_date(ts) << ',' << quote.at("open").at(i).get<double>() << ','
                << quote.at("high").at(i).get<double>() << ','
                << quote.at("low").at(i).get<double>() << ','
                << quote.at("close").at(i).get<double>() << ','
                << quote.at("volume").at(i).get<std::int64_t>() << '\n';
            ++written;
        }

        std::cout << "wrote " << written << " bars to " << out_path << '\n';
        return 0;
    } catch (const Error& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: unexpected response from Yahoo (" << e.what() << ")\n";
        return 1;
    }
}
