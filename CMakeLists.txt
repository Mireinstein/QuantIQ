cmake_minimum_required(VERSION 3.20)
project(quantiq CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(json Catch2)

find_package(CURL REQUIRED)

add_library(quantiq_core
  src/price.cpp
  src/portfolio.cpp
  src/strategies.cpp
  src/mock_venue.cpp
  src/journal.cpp
  src/report.cpp
  src/engine.cpp
  src/risk.cpp
  src/http.cpp)
target_include_directories(quantiq_core PUBLIC include)
target_link_libraries(quantiq_core PUBLIC nlohmann_json::nlohmann_json CURL::libcurl)

add_executable(trader src/main.cpp)
target_link_libraries(trader PRIVATE quantiq_core)

add_executable(fetch-bars src/fetch_bars.cpp)
target_link_libraries(fetch-bars PRIVATE quantiq_core)

enable_testing()
add_executable(tests
  tests/test_price.cpp
  tests/test_ringbuffer.cpp
  tests/test_portfolio.cpp
  tests/test_strategies.cpp
  tests/test_engine.cpp)
target_link_libraries(tests PRIVATE quantiq_core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(tests)
