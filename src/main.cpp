#include "tt_metrics_exporter/device.hpp"
#include "tt_metrics_exporter/http_server.hpp"
#include "tt_metrics_exporter/json.hpp"
#include "tt_metrics_exporter/metrics.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

std::atomic_bool* running_flag = nullptr;

void handle_signal(int) {
  if (running_flag != nullptr) {
    running_flag->store(false);
  }
}

struct Options {
  std::string listen_address = "0.0.0.0";
  std::uint16_t port = 9400;
  std::chrono::seconds poll_interval{5};
  std::filesystem::path sysfs_root = "/sys/class/tenstorrent";
  std::filesystem::path allocation_state_root;
  std::filesystem::path janitor_state_root;
  std::filesystem::path metalium_profiler_state_root;
  std::chrono::seconds metalium_profiler_stale_after{15};
  bool once = false;
  bool json = false;
  bool collect_pcie_counters = false;
};

void print_usage(const char* program) {
  std::cerr << "usage: " << program
            << " [--sysfs-root PATH] [--listen-address ADDR] [--port PORT]"
               " [--allocation-state-root PATH] [--janitor-state-root PATH]"
               " [--metalium-profiler-state-root PATH]"
               " [--metalium-profiler-stale-after SECONDS]"
               " [--poll-interval SECONDS] [--collect-pcie-counters]"
               " [--once] [--json]\n";
}

bool parse_uint16(const std::string& value, std::uint16_t& output) {
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || parsed <= 0 || parsed > 65535) {
    return false;
  }
  output = static_cast<std::uint16_t>(parsed);
  return true;
}

bool parse_seconds(const std::string& value, std::chrono::seconds& output) {
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || parsed <= 0) {
    return false;
  }
  output = std::chrono::seconds(parsed);
  return true;
}

bool parse_args(int argc, char** argv, Options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string arg(argv[index]);
    auto require_value = [&](const char* name) -> char* {
      if (index + 1 >= argc) {
        std::cerr << name << " requires a value\n";
        return nullptr;
      }
      return argv[++index];
    };

    if (arg == "--sysfs-root") {
      char* value = require_value("--sysfs-root");
      if (value == nullptr) {
        return false;
      }
      options.sysfs_root = value;
    } else if (arg == "--allocation-state-root") {
      char* value = require_value("--allocation-state-root");
      if (value == nullptr) {
        return false;
      }
      options.allocation_state_root = value;
    } else if (arg == "--janitor-state-root") {
      char* value = require_value("--janitor-state-root");
      if (value == nullptr) {
        return false;
      }
      options.janitor_state_root = value;
    } else if (arg == "--metalium-profiler-state-root") {
      char* value = require_value("--metalium-profiler-state-root");
      if (value == nullptr) {
        return false;
      }
      options.metalium_profiler_state_root = value;
    } else if (arg == "--metalium-profiler-stale-after") {
      char* value = require_value("--metalium-profiler-stale-after");
      if (value == nullptr ||
          !parse_seconds(value, options.metalium_profiler_stale_after)) {
        std::cerr << "--metalium-profiler-stale-after must be a positive "
                     "number of seconds\n";
        return false;
      }
    } else if (arg == "--listen-address") {
      char* value = require_value("--listen-address");
      if (value == nullptr) {
        return false;
      }
      options.listen_address = value;
    } else if (arg == "--port") {
      char* value = require_value("--port");
      if (value == nullptr || !parse_uint16(value, options.port)) {
        std::cerr << "--port must be in range 1..65535\n";
        return false;
      }
    } else if (arg == "--poll-interval") {
      char* value = require_value("--poll-interval");
      if (value == nullptr || !parse_seconds(value, options.poll_interval)) {
        std::cerr << "--poll-interval must be a positive number of seconds\n";
        return false;
      }
    } else if (arg == "--once") {
      options.once = true;
    } else if (arg == "--json") {
      options.json = true;
    } else if (arg == "--collect-pcie-counters") {
      options.collect_pcie_counters = true;
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    } else {
      std::cerr << "unknown argument: " << arg << '\n';
      return false;
    }
  }

  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse_args(argc, argv, options)) {
    print_usage(argv[0]);
    return 2;
  }

  tt::metrics::SysfsCollector collector(
      tt::metrics::CollectorConfig{options.sysfs_root,
                                   options.allocation_state_root,
                                   options.janitor_state_root,
                                   options.metalium_profiler_state_root,
                                   options.metalium_profiler_stale_after.count(),
                                   options.collect_pcie_counters});

  if (options.once) {
    const auto devices = collector.collect();
    if (options.json) {
      std::cout << tt::metrics::render_devices_json(devices);
    } else {
      std::cout << tt::metrics::render_prometheus(devices);
    }
    return 0;
  }

  std::atomic_bool running{true};
  running_flag = &running;
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  std::mutex metrics_mutex;
  std::string latest_metrics;
  std::string latest_devices_json;

  auto refresh_metrics = [&]() {
    const auto devices = collector.collect();
    const auto rendered = tt::metrics::render_prometheus(devices);
    const auto rendered_devices = tt::metrics::render_devices_json(devices);
    std::lock_guard<std::mutex> lock(metrics_mutex);
    latest_metrics = rendered;
    latest_devices_json = rendered_devices;
  };

  refresh_metrics();

  std::thread poller([&]() {
    while (running.load()) {
      for (int slept = 0;
           running.load() && slept < options.poll_interval.count(); ++slept) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
      if (running.load()) {
        refresh_metrics();
      }
    }
  });

  tt::metrics::HttpServer server(
      options.listen_address, options.port, [&]() -> std::string {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        return latest_metrics;
      }, [&]() -> std::string {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        return latest_devices_json;
      });

  std::cerr << "serving Tenstorrent metrics on " << options.listen_address
            << ':' << options.port << " from " << options.sysfs_root << '\n';
  const int result = server.serve(running);
  running.store(false);
  if (poller.joinable()) {
    poller.join();
  }
  running_flag = nullptr;
  return result;
}
