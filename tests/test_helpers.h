#pragma once

// Cross-platform test helpers. POSIX `mkstemp` / `mkdtemp` / `<unistd.h>`
// don't exist on MSVC, so we route through `std::filesystem` plus a
// little randomness. Same call works on Mac, Linux, and Windows.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

namespace dafeng {
namespace testing {

// Returns a process+wallclock+counter-derived hex id. Cheap collision
// avoidance for parallel-test temp paths; we still loop on
// `create_directory` to handle the residual race.
inline std::string UniqueToken() {
  static std::atomic<uint64_t> counter{0};
  const auto now =
      std::chrono::steady_clock::now().time_since_epoch().count();
  std::random_device rd;
  const uint64_t seed =
      static_cast<uint64_t>(now) ^
      (static_cast<uint64_t>(rd()) << 32 | static_cast<uint64_t>(rd())) ^
      counter.fetch_add(1, std::memory_order_relaxed);
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%016llx",
                static_cast<unsigned long long>(seed));
  return buf;
}

// Creates and returns a fresh, unique directory under the system's temp
// directory. Returns an empty path on failure.
inline std::filesystem::path MakeTempDir(
    const std::string& prefix = "dafeng-test-") {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path base = fs::temp_directory_path(ec);
  if (ec || base.empty()) base = fs::path(".");
  for (int attempt = 0; attempt < 100; ++attempt) {
    fs::path candidate = base / (prefix + UniqueToken());
    if (fs::create_directory(candidate, ec) && !ec) return candidate;
  }
  return {};
}

// Returns a fresh path under the temp directory (does NOT create the file).
// Use for socket / pipe paths where the framework wants an unused name.
inline std::filesystem::path MakeTempPath(
    const std::string& prefix = "dafeng-test-",
    const std::string& suffix = "") {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path base = fs::temp_directory_path(ec);
  if (ec || base.empty()) base = fs::path(".");
  return base / (prefix + UniqueToken() + suffix);
}

// Best-effort recursive remove; ignores errors (caller likely doesn't
// care, e.g. test teardown).
inline void RemoveAll(const std::filesystem::path& p) {
  std::error_code ec;
  std::filesystem::remove_all(p, ec);
}

// Returns a fresh, platform-appropriate endpoint address for IPC tests:
//   POSIX:   /tmp/dafeng-test-<token>.sock        (Unix Domain Socket)
//   Windows: \\.\pipe\dafeng-test-<token>          (Named Pipe)
// The address is unique per call. On POSIX we also pre-unlink any
// stale file so bind() doesn't trip EADDRINUSE.
inline std::string MakeTempEndpoint(const std::string& prefix = "dafeng-test-") {
#if defined(_WIN32)
  return "\\\\.\\pipe\\" + prefix + UniqueToken();
#else
  auto p = MakeTempPath(prefix, ".sock");
  std::error_code ec;
  std::filesystem::remove(p, ec);
  return p.string();
#endif
}

}  // namespace testing
}  // namespace dafeng
