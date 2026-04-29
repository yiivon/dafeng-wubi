// Windows Named Pipe IPC backend.
//
// Mirrors the Unix Domain Socket backend in endpoint_unix.cc — same
// length-prefixed framing semantics, same per-call timeout contract, same
// shutdown-from-another-thread story. The differences are pure plumbing:
//
//   - Each Accept() creates a fresh named pipe instance via
//     CreateNamedPipeW + ConnectNamedPipe with OVERLAPPED I/O. We block on
//     WaitForMultipleObjects of (connect_event, shutdown_event) so the
//     accept loop can be unblocked from another thread.
//
//   - Read/Write use OVERLAPPED I/O too, so we can implement millisecond
//     timeouts via WaitForSingleObject + CancelIoEx (POSIX poll() has no
//     direct equivalent on a HANDLE, hence the OVERLAPPED dance).
//
// Pipe naming convention is set by paths_win.cc — typically
// `\\.\pipe\dafeng-daemon-<username>` so two users on the same box don't
// collide and ACL inheritance keeps the pipe owner-only by default.

#include "dafeng/endpoint.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "dafeng/logging.h"

namespace dafeng {

namespace {

using std::chrono::milliseconds;
using std::chrono::steady_clock;

constexpr DWORD kPipeBufSize = 64 * 1024;  // ample for any single rerank frame

DWORD RemainingMs(const steady_clock::time_point& deadline) {
  auto rem = std::chrono::duration_cast<milliseconds>(deadline -
                                                      steady_clock::now())
                 .count();
  if (rem < 0) return 0;
  if (rem > std::numeric_limits<DWORD>::max()) {
    return std::numeric_limits<DWORD>::max();
  }
  return static_cast<DWORD>(rem);
}

// UTF-8 → UTF-16 for Win32 wide-char APIs. We accept std::string at the
// public boundary (cross-platform) but every Windows API call needs WCHAR.
std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                      static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), n);
  return out;
}

// Auto-reset would race with the cancel paths in Read/Write — manual-reset
// gives us deterministic semantics: we Reset() before issuing the I/O,
// the I/O signals it on completion, and the cancel path drains it.
struct ManualEvent {
  HANDLE h = nullptr;
  ManualEvent() {
    h = ::CreateEventW(nullptr, /*bManualReset=*/TRUE,
                       /*bInitialState=*/FALSE, nullptr);
  }
  ~ManualEvent() {
    if (h != nullptr) ::CloseHandle(h);
  }
  ManualEvent(const ManualEvent&) = delete;
  ManualEvent& operator=(const ManualEvent&) = delete;

  void Set() {
    if (h != nullptr) ::SetEvent(h);
  }
  void Reset() {
    if (h != nullptr) ::ResetEvent(h);
  }
  bool valid() const { return h != nullptr; }
};

class WinPipeConnection final : public Connection {
 public:
  explicit WinPipeConnection(HANDLE pipe) : pipe_(pipe) {}

  ~WinPipeConnection() override { Close(); }

  WinPipeConnection(const WinPipeConnection&) = delete;
  WinPipeConnection& operator=(const WinPipeConnection&) = delete;

  bool ReadFull(uint8_t* buf, std::size_t size,
                milliseconds timeout) override {
    return DoIo(buf, size, timeout, /*is_read=*/true);
  }

  bool WriteAll(const uint8_t* buf, std::size_t size,
                milliseconds timeout) override {
    return DoIo(const_cast<uint8_t*>(buf), size, timeout, /*is_read=*/false);
  }

  void Close() override {
    if (pipe_ != INVALID_HANDLE_VALUE && pipe_ != nullptr) {
      // Best-effort tell the peer we're done — server side does this via
      // DisconnectNamedPipe; the client side has no equivalent.
      ::FlushFileBuffers(pipe_);
      ::CloseHandle(pipe_);
      pipe_ = INVALID_HANDLE_VALUE;
    }
  }

  bool IsConnected() const override {
    return pipe_ != INVALID_HANDLE_VALUE && pipe_ != nullptr;
  }

 private:
  bool DoIo(uint8_t* buf, std::size_t size, milliseconds timeout,
            bool is_read) {
    if (!IsConnected()) return false;
    if (!io_event_.valid()) return false;
    const auto deadline = steady_clock::now() + timeout;
    std::size_t total = 0;
    while (total < size) {
      const DWORD rem = RemainingMs(deadline);
      if (rem == 0) return false;

      io_event_.Reset();
      OVERLAPPED ov{};
      ov.hEvent = io_event_.h;

      DWORD n = 0;
      const DWORD chunk = static_cast<DWORD>(
          std::min<std::size_t>(size - total, std::numeric_limits<DWORD>::max()));
      const BOOL rc = is_read
          ? ::ReadFile(pipe_, buf + total, chunk, &n, &ov)
          : ::WriteFile(pipe_, buf + total, chunk, &n, &ov);
      if (!rc) {
        const DWORD err = ::GetLastError();
        if (err != ERROR_IO_PENDING) {
          // ERROR_BROKEN_PIPE / ERROR_NO_DATA are both peer-closed; just
          // return false and let the caller treat that as a clean close.
          return false;
        }
      } else {
        // Synchronous completion. n holds bytes transferred.
        if (n == 0) return false;  // peer closed mid-call
        total += n;
        continue;
      }

      const DWORD wait = ::WaitForSingleObject(io_event_.h, rem);
      if (wait == WAIT_TIMEOUT) {
        ::CancelIoEx(pipe_, &ov);
        // Drain so the OVERLAPPED isn't reused while still associated.
        DWORD ignored = 0;
        ::GetOverlappedResult(pipe_, &ov, &ignored, /*bWait=*/TRUE);
        return false;
      }
      if (wait != WAIT_OBJECT_0) return false;

      DWORD got = 0;
      if (!::GetOverlappedResult(pipe_, &ov, &got, /*bWait=*/FALSE)) {
        return false;
      }
      if (got == 0) return false;
      total += got;
    }
    return true;
  }

  HANDLE pipe_;
  ManualEvent io_event_;
};

class WinPipeServer final : public EndpointServer {
 public:
  WinPipeServer(std::wstring name) : name_(std::move(name)) {}

  ~WinPipeServer() override { Shutdown(); }

  WinPipeServer(const WinPipeServer&) = delete;
  WinPipeServer& operator=(const WinPipeServer&) = delete;

  std::unique_ptr<Connection> Accept() override {
    if (!shutdown_event_.valid()) return nullptr;

    // One pipe instance per Accept() — multiple in flight is fine because
    // CreateNamedPipeW with PIPE_UNLIMITED_INSTANCES accepts the same name
    // repeatedly. Each instance is a separate HANDLE and lifecycle.
    HANDLE pipe = ::CreateNamedPipeW(
        name_.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES, kPipeBufSize, kPipeBufSize,
        /*nDefaultTimeOut=*/0, /*lpSecurityAttributes=*/nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
      DAFENG_LOG_ERROR("CreateNamedPipeW failed: %lu",
                       static_cast<unsigned long>(::GetLastError()));
      return nullptr;
    }

    ManualEvent connect_event;
    if (!connect_event.valid()) {
      ::CloseHandle(pipe);
      return nullptr;
    }
    OVERLAPPED ov{};
    ov.hEvent = connect_event.h;

    const BOOL connected = ::ConnectNamedPipe(pipe, &ov);
    DWORD err = ::GetLastError();
    if (connected) {
      // ConnectNamedPipe returns nonzero only if the call is unable to
      // queue the connect — treat as failure.
      ::CloseHandle(pipe);
      return nullptr;
    }
    if (err == ERROR_PIPE_CONNECTED) {
      // Client connected before we called ConnectNamedPipe. Common when a
      // burst of clients is hammering the daemon. Treat as success.
      return std::make_unique<WinPipeConnection>(pipe);
    }
    if (err != ERROR_IO_PENDING) {
      DAFENG_LOG_ERROR("ConnectNamedPipe failed: %lu",
                       static_cast<unsigned long>(err));
      ::CloseHandle(pipe);
      return nullptr;
    }

    // Async: wait for either a real client connect or our shutdown signal.
    HANDLE waits[2] = {connect_event.h, shutdown_event_.h};
    const DWORD r = ::WaitForMultipleObjects(2, waits, /*bWaitAll=*/FALSE,
                                             INFINITE);
    if (r == WAIT_OBJECT_0) {
      DWORD bytes = 0;
      if (!::GetOverlappedResult(pipe, &ov, &bytes, /*bWait=*/FALSE)) {
        ::CloseHandle(pipe);
        return nullptr;
      }
      return std::make_unique<WinPipeConnection>(pipe);
    }
    // Shutdown or wait error: cancel the pending connect and bail.
    ::CancelIoEx(pipe, &ov);
    DWORD ignored = 0;
    ::GetOverlappedResult(pipe, &ov, &ignored, /*bWait=*/TRUE);
    ::CloseHandle(pipe);
    return nullptr;
  }

  void Shutdown() override { shutdown_event_.Set(); }

 private:
  std::wstring name_;
  ManualEvent shutdown_event_;
};

}  // namespace

std::unique_ptr<EndpointServer> ListenEndpoint(const std::string& address) {
  if (address.empty()) {
    DAFENG_LOG_ERROR("ListenEndpoint: empty address");
    return nullptr;
  }
  std::wstring name = Utf8ToWide(address);
  if (name.empty()) {
    DAFENG_LOG_ERROR("ListenEndpoint: bad UTF-8 address");
    return nullptr;
  }

  // Sanity probe: actually try CreateNamedPipeW once so we surface bad
  // names (e.g. missing `\\.\pipe\` prefix) at construction time, then
  // close it — the real Accept() will create per-call instances.
  HANDLE probe = ::CreateNamedPipeW(
      name.c_str(),
      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
      PIPE_UNLIMITED_INSTANCES, kPipeBufSize, kPipeBufSize, 0, nullptr);
  if (probe == INVALID_HANDLE_VALUE) {
    const DWORD err = ::GetLastError();
    DAFENG_LOG_ERROR("ListenEndpoint(%s): CreateNamedPipeW failed: %lu",
                     address.c_str(), static_cast<unsigned long>(err));
    return nullptr;
  }
  ::CloseHandle(probe);

  DAFENG_LOG_INFO("listening on %s", address.c_str());
  return std::make_unique<WinPipeServer>(std::move(name));
}

std::unique_ptr<Connection> ConnectEndpoint(const std::string& address) {
  if (address.empty()) return nullptr;
  std::wstring name = Utf8ToWide(address);
  if (name.empty()) return nullptr;

  // Named-pipe concurrency is per-instance: an instance accepts one
  // client and is then "consumed" until the server creates the next
  // one. Multiple clients arriving simultaneously will race for the
  // same listening instance; only one CreateFileW wins, the rest see
  // ERROR_PIPE_BUSY. Unix Domain Sockets don't have this race because
  // the kernel queues all connect()s into the listen backlog.
  //
  // Compensate by retrying inside a hard 1 s budget. Each iteration
  // waits up to 50 ms for an instance to be free, then tries to
  // attach. If CreateFileW says BUSY (someone else won the race) we
  // loop. If WaitNamedPipeW returns FILE_NOT_FOUND there really is
  // no server, so we fail fast.
  using std::chrono::steady_clock;
  using std::chrono::milliseconds;
  const auto deadline = steady_clock::now() + std::chrono::seconds(1);

  while (steady_clock::now() < deadline) {
    if (!::WaitNamedPipeW(name.c_str(), 50 /*ms*/)) {
      const DWORD err = ::GetLastError();
      if (err == ERROR_FILE_NOT_FOUND) {
        // No server at all — caller treats this as "daemon not
        // running" and falls back to RIME-only.
        return nullptr;
      }
      // ERROR_SEM_TIMEOUT or similar: just spin on the deadline.
      continue;
    }

    HANDLE pipe = ::CreateFileW(name.c_str(),
                                GENERIC_READ | GENERIC_WRITE,
                                /*share=*/0, /*sa=*/nullptr,
                                OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                                /*template=*/nullptr);
    if (pipe != INVALID_HANDLE_VALUE) {
      // Match server: byte mode for length-prefixed framing.
      DWORD mode = PIPE_READMODE_BYTE;
      ::SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
      return std::make_unique<WinPipeConnection>(pipe);
    }
    const DWORD err = ::GetLastError();
    if (err != ERROR_PIPE_BUSY) {
      // Hard error (access denied, name invalid, etc.) — don't retry.
      return nullptr;
    }
    // Lost the race for the listening instance; loop and try again.
  }
  // Budget exhausted.
  return nullptr;
}

}  // namespace dafeng
