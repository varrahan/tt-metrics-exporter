#include "tt_metrics_exporter/http_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <utility>

namespace tt::metrics {
namespace {

std::string response(int status, std::string_view reason,
                     std::string_view content_type, std::string_view body) {
  std::ostringstream output;
  output << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
         << "Content-Type: " << content_type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: close\r\n"
         << "\r\n"
         << body;
  return output.str();
}

void write_all(int fd, const std::string& data) {
  const char* cursor = data.data();
  std::size_t remaining = data.size();
  while (remaining > 0) {
    const ssize_t written = ::send(fd, cursor, remaining, MSG_NOSIGNAL);
    if (written <= 0) {
      return;
    }
    cursor += written;
    remaining -= static_cast<std::size_t>(written);
  }
}

std::string request_path(std::string_view request) {
  const std::size_t line_end = request.find("\r\n");
  const std::string_view line =
      line_end == std::string_view::npos ? request : request.substr(0, line_end);
  const std::size_t first_space = line.find(' ');
  if (first_space == std::string_view::npos) {
    return "/";
  }
  const std::size_t second_space = line.find(' ', first_space + 1);
  if (second_space == std::string_view::npos) {
    return "/";
  }
  return std::string(
      line.substr(first_space + 1, second_space - first_space - 1));
}

}  // namespace

HttpServer::HttpServer(std::string listen_address,
                       std::uint16_t port,
                       MetricsProvider metrics_provider,
                       DevicesProvider devices_provider)
    : listen_address_(std::move(listen_address)),
      port_(port),
      metrics_provider_(std::move(metrics_provider)),
      devices_provider_(std::move(devices_provider)) {}

HttpServer::~HttpServer() { stop(); }

int HttpServer::serve(std::atomic_bool& running) {
  if (!open_socket()) {
    return 1;
  }

  while (running.load()) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(server_fd_, &read_fds);

    timeval timeout{};
    timeout.tv_sec = 1;

    const int ready = ::select(server_fd_ + 1, &read_fds, nullptr, nullptr,
                               &timeout);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::cerr << "select failed: " << std::strerror(errno) << '\n';
      return 1;
    }
    if (ready == 0) {
      continue;
    }

    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    const int client_fd =
        ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr),
                 &client_len);
    if (client_fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::cerr << "accept failed: " << std::strerror(errno) << '\n';
      return 1;
    }
    serve_client(client_fd);
    ::close(client_fd);
  }

  return 0;
}

void HttpServer::stop() {
  if (server_fd_ >= 0) {
    ::close(server_fd_);
    server_fd_ = -1;
  }
}

bool HttpServer::open_socket() {
  server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    std::cerr << "socket failed: " << std::strerror(errno) << '\n';
    return false;
  }

  int reuse = 1;
  if (::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse,
                   sizeof(reuse)) < 0) {
    std::cerr << "setsockopt(SO_REUSEADDR) failed: " << std::strerror(errno)
              << '\n';
    stop();
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  if (::inet_pton(AF_INET, listen_address_.c_str(), &addr.sin_addr) != 1) {
    std::cerr << "invalid listen address: " << listen_address_ << '\n';
    stop();
    return false;
  }

  if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) <
      0) {
    std::cerr << "bind failed on " << listen_address_ << ':' << port_ << ": "
              << std::strerror(errno) << '\n';
    stop();
    return false;
  }

  if (::listen(server_fd_, 16) < 0) {
    std::cerr << "listen failed: " << std::strerror(errno) << '\n';
    stop();
    return false;
  }

  return true;
}

void HttpServer::serve_client(int client_fd) {
  char buffer[4096]{};
  const ssize_t read_size = ::recv(client_fd, buffer, sizeof(buffer) - 1, 0);
  if (read_size <= 0) {
    return;
  }

  const std::string path =
      request_path(std::string_view(buffer, static_cast<std::size_t>(read_size)));
  if (path == "/metrics") {
    write_all(client_fd,
              response(200, "OK", "text/plain; version=0.0.4; charset=utf-8",
                       metrics_provider_()));
    return;
  }
  if (path == "/v1/devices") {
    write_all(client_fd,
              response(200, "OK", "application/json", devices_provider_()));
    return;
  }
  if (path == "/healthz") {
    write_all(client_fd, response(200, "OK", "text/plain", "ok\n"));
    return;
  }

  write_all(client_fd, response(404, "Not Found", "text/plain", "not found\n"));
}

}  // namespace tt::metrics
