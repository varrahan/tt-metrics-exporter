#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace tt::metrics {

class HttpServer {
 public:
  using MetricsProvider = std::function<std::string()>;
  using DevicesProvider = std::function<std::string()>;

  HttpServer(std::string listen_address,
             std::uint16_t port,
             MetricsProvider metrics_provider,
             DevicesProvider devices_provider);
  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;
  ~HttpServer();

  int serve(std::atomic_bool& running);
  void stop();

 private:
  std::string listen_address_;
  std::uint16_t port_;
  MetricsProvider metrics_provider_;
  DevicesProvider devices_provider_;
  int server_fd_ = -1;

  bool open_socket();
  void serve_client(int client_fd);
};

}  // namespace tt::metrics
