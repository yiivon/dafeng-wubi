#include "dafeng/endpoint.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "dafeng/logging.h"

namespace dafeng {

namespace {

using std::chrono::milliseconds;
using std::chrono::steady_clock;

int RemainingMs(const steady_clock::time_point& deadline) {
  auto rem = std::chrono::duration_cast<milliseconds>(deadline -
                                                      steady_clock::now())
                 .count();
  if (rem < 0) return 0;
  if (rem > std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
  return static_cast<int>(rem);
}

class UnixConnection final : public Connection {
 public:
  explicit UnixConnection(int fd) : fd_(fd) {}
  ~UnixConnection() override { Close(); }

  UnixConnection(const UnixConnection&) = delete;
  UnixConnection& operator=(const UnixConnection&) = delete;

  bool ReadFull(uint8_t* buf, std::size_t size,
                milliseconds timeout) override {
    if (fd_ < 0) return false;
    const auto deadline = steady_clock::now() + timeout;
    std::size_t total = 0;
    while (total < size) {
      const int rem = RemainingMs(deadline);
      if (rem == 0) {
        errno = ETIMEDOUT;
        return false;
      }
      pollfd pfd = {fd_, POLLIN, 0};
      const int rc = ::poll(&pfd, 1, rem);
      if (rc == 0) {
        errno = ETIMEDOUT;
        return false;
      }
      if (rc < 0) {
        if (errno == EINTR) continue;
        return false;
      }
      const ssize_t n = ::read(fd_, buf + total, size - total);
      if (n == 0) return false;  // peer closed
      if (n < 0) {
        if (errno == EINTR || errno == EAGAIN) continue;
        return false;
      }
      total += static_cast<std::size_t>(n);
    }
    return true;
  }

  bool WriteAll(const uint8_t* buf, std::size_t size,
                milliseconds timeout) override {
    if (fd_ < 0) return false;
    const auto deadline = steady_clock::now() + timeout;
    std::size_t total = 0;
    while (total < size) {
      const int rem = RemainingMs(deadline);
      if (rem == 0) {
        errno = ETIMEDOUT;
        return false;
      }
      pollfd pfd = {fd_, POLLOUT, 0};
      const int rc = ::poll(&pfd, 1, rem);
      if (rc == 0) {
        errno = ETIMEDOUT;
        return false;
      }
      if (rc < 0) {
        if (errno == EINTR) continue;
        return false;
      }
      const ssize_t n = ::write(fd_, buf + total, size - total);
      if (n < 0) {
        if (errno == EINTR || errno == EAGAIN) continue;
        return false;
      }
      total += static_cast<std::size_t>(n);
    }
    return true;
  }

  void Close() override {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  bool IsConnected() const override { return fd_ >= 0; }

 private:
  int fd_;
};

class UnixServer final : public EndpointServer {
 public:
  UnixServer(int listen_fd, std::string path)
      : listen_fd_(listen_fd), path_(std::move(path)) {
    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) == 0) {
      shutdown_read_ = pipe_fds[0];
      shutdown_write_ = pipe_fds[1];
      MakeNonblocking(shutdown_read_);
      MakeNonblocking(shutdown_write_);
    }
  }

  ~UnixServer() override {
    Shutdown();
    if (listen_fd_ >= 0) ::close(listen_fd_);
    if (shutdown_read_ >= 0) ::close(shutdown_read_);
    if (shutdown_write_ >= 0) ::close(shutdown_write_);
    if (!path_.empty()) ::unlink(path_.c_str());
  }

  UnixServer(const UnixServer&) = delete;
  UnixServer& operator=(const UnixServer&) = delete;

  std::unique_ptr<Connection> Accept() override {
    if (listen_fd_ < 0) return nullptr;
    pollfd pfds[2] = {
        {listen_fd_, POLLIN, 0},
        {shutdown_read_, POLLIN, 0},
    };
    while (true) {
      const int rc = ::poll(pfds, 2, -1);
      if (rc < 0) {
        if (errno == EINTR) continue;
        return nullptr;
      }
      if (pfds[1].revents & POLLIN) return nullptr;  // shutdown
      if (!(pfds[0].revents & POLLIN)) continue;
      const int conn_fd = ::accept(listen_fd_, nullptr, nullptr);
      if (conn_fd < 0) {
        if (errno == EINTR) continue;
        return nullptr;
      }
      return std::make_unique<UnixConnection>(conn_fd);
    }
  }

  void Shutdown() override {
    if (shutdown_write_ >= 0) {
      const uint8_t b = 1;
      while (::write(shutdown_write_, &b, 1) < 0 && errno == EINTR) {
      }
    }
  }

 private:
  static void MakeNonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }

  int listen_fd_ = -1;
  int shutdown_read_ = -1;
  int shutdown_write_ = -1;
  std::string path_;
};

}  // namespace

std::unique_ptr<EndpointServer> ListenEndpoint(const std::string& address) {
  ::unlink(address.c_str());

  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    DAFENG_LOG_ERROR("socket() failed: %s", std::strerror(errno));
    return nullptr;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (address.size() >= sizeof(addr.sun_path)) {
    DAFENG_LOG_ERROR("socket path too long: %zu (max %zu)", address.size(),
                     sizeof(addr.sun_path) - 1);
    ::close(fd);
    return nullptr;
  }
  std::strncpy(addr.sun_path, address.c_str(), sizeof(addr.sun_path) - 1);

  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    DAFENG_LOG_ERROR("bind(%s) failed: %s", address.c_str(),
                     std::strerror(errno));
    ::close(fd);
    return nullptr;
  }

  // Owner-only access on the socket file. Keeps other local users out.
  ::chmod(address.c_str(), S_IRUSR | S_IWUSR);

  if (::listen(fd, 16) < 0) {
    DAFENG_LOG_ERROR("listen() failed: %s", std::strerror(errno));
    ::close(fd);
    ::unlink(address.c_str());
    return nullptr;
  }

  DAFENG_LOG_INFO("listening on %s", address.c_str());
  return std::make_unique<UnixServer>(fd, address);
}

std::unique_ptr<Connection> ConnectEndpoint(const std::string& address) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return nullptr;

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (address.size() >= sizeof(addr.sun_path)) {
    ::close(fd);
    return nullptr;
  }
  std::strncpy(addr.sun_path, address.c_str(), sizeof(addr.sun_path) - 1);

  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return nullptr;
  }

  return std::make_unique<UnixConnection>(fd);
}

}  // namespace dafeng
