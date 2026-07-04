cmake_minimum_required(VERSION 3.20)

project(tt_metrics_exporter LANGUAGES CXX)

find_package(Threads REQUIRED)

add_library(tt_metrics_exporter_lib
  src/device.cpp
  src/http_server.cpp
  src/json.cpp
  src/metrics.cpp
)
target_include_directories(tt_metrics_exporter_lib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(tt_metrics_exporter_lib PUBLIC cxx_std_17)
target_compile_options(tt_metrics_exporter_lib PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

add_executable(tt-metrics-exporter
  src/main.cpp
)
target_link_libraries(tt-metrics-exporter
  PRIVATE
    tt_metrics_exporter_lib
    Threads::Threads
)
target_compile_options(tt-metrics-exporter PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

enable_testing()

add_executable(tt_metrics_exporter_tests
  tests/sysfs_collector_test.cpp
)
target_link_libraries(tt_metrics_exporter_tests
  PRIVATE
    tt_metrics_exporter_lib
)
target_compile_options(tt_metrics_exporter_tests PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

add_test(NAME tt_metrics_exporter_tests COMMAND tt_metrics_exporter_tests)

install(TARGETS tt-metrics-exporter RUNTIME DESTINATION bin)
