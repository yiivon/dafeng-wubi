// IPC ping-pong benchmark. Spins up a Server in-process, connects a real
// DafengClient over UDS, and times round-trip latency. Reports P50/P95/P99
// and exits non-zero if the P99 budget is breached — wired into ctest so
// regressions in IPC hot-path land as red CI runs.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "dafeng/client.h"
#include "dafeng/endpoint.h"
#include "dafeng/logging.h"
#include "server.h"
#include "services.h"

namespace {

std::string MakeTempSocketPath() {
  char tmpl[] = "/tmp/dafeng-bench-XXXXXX";
  int fd = ::mkstemp(tmpl);
  if (fd >= 0) ::close(fd);
  ::unlink(tmpl);
  return tmpl;
}

uint64_t Percentile(std::vector<uint64_t>& samples, double pct) {
  if (samples.empty()) return 0;
  size_t i = static_cast<size_t>(samples.size() * pct);
  if (i >= samples.size()) i = samples.size() - 1;
  std::nth_element(samples.begin(), samples.begin() + i, samples.end());
  return samples[i];
}

}  // namespace

int main(int argc, char** argv) {
  int iterations = 1000;
  int warmup = 100;
  uint64_t p99_budget_us = 0;  // 0 = informational only
  std::string external_addr;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
      iterations = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
      warmup = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--p99-budget-us") == 0 && i + 1 < argc) {
      p99_budget_us = static_cast<uint64_t>(std::atoll(argv[++i]));
    } else if (std::strcmp(argv[i], "--addr") == 0 && i + 1 < argc) {
      external_addr = argv[++i];
    } else if (std::strcmp(argv[i], "--help") == 0 ||
               std::strcmp(argv[i], "-h") == 0) {
      std::printf(
          "Usage: ipc_pingpong [--iterations N] [--warmup N]\n"
          "                    [--p99-budget-us N] [--addr PATH]\n");
      return 0;
    }
  }

  // Quiet logs from the in-process daemon — they'd noise up the report.
  dafeng::SetLogLevel(dafeng::LogLevel::kError);

  std::unique_ptr<dafeng::Server> server;
  std::thread server_thread;
  std::string addr;

  if (!external_addr.empty()) {
    addr = external_addr;
  } else {
    addr = MakeTempSocketPath();
    auto endpoint = dafeng::ListenEndpoint(addr);
    if (!endpoint) {
      std::fprintf(stderr, "failed to listen on %s\n", addr.c_str());
      return 2;
    }
    server = std::make_unique<dafeng::Server>(
        std::move(endpoint), dafeng::MakeMockReverseRerankService(),
        dafeng::MakeNullCommitLogger());
    server_thread = std::thread([&]() { server->Run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  dafeng::DafengClient client(addr);

  // Warmup.
  for (int i = 0; i < warmup; ++i) {
    if (!client.Ping(std::chrono::milliseconds(1000))) {
      std::fprintf(stderr, "warmup ping failed (iter %d)\n", i);
      if (server) {
        server->Shutdown();
        server_thread.join();
      }
      return 3;
    }
  }

  // Measure.
  std::vector<uint64_t> latencies_us;
  latencies_us.reserve(iterations);
  for (int i = 0; i < iterations; ++i) {
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = client.Ping(std::chrono::milliseconds(1000));
    const auto t1 = std::chrono::steady_clock::now();
    if (!ok) {
      std::fprintf(stderr, "ping failed at iter %d\n", i);
      if (server) {
        server->Shutdown();
        server_thread.join();
      }
      return 4;
    }
    latencies_us.push_back(static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()));
  }

  if (server) {
    server->Shutdown();
    server_thread.join();
  }

  // Percentiles.
  const uint64_t p50 = Percentile(latencies_us, 0.50);
  const uint64_t p95 = Percentile(latencies_us, 0.95);
  const uint64_t p99 = Percentile(latencies_us, 0.99);
  const uint64_t p999 = Percentile(latencies_us, 0.999);
  const uint64_t maxv = *std::max_element(latencies_us.begin(),
                                           latencies_us.end());
  uint64_t sum = 0;
  for (auto v : latencies_us) sum += v;
  const uint64_t mean = sum / latencies_us.size();

  std::printf("ipc_pingpong: iterations=%d warmup=%d\n", iterations, warmup);
  std::printf("  mean : %6llu us\n", static_cast<unsigned long long>(mean));
  std::printf("  P50  : %6llu us\n", static_cast<unsigned long long>(p50));
  std::printf("  P95  : %6llu us\n", static_cast<unsigned long long>(p95));
  std::printf("  P99  : %6llu us\n", static_cast<unsigned long long>(p99));
  std::printf("  P999 : %6llu us\n", static_cast<unsigned long long>(p999));
  std::printf("  max  : %6llu us\n", static_cast<unsigned long long>(maxv));

  if (p99_budget_us > 0) {
    if (p99 <= p99_budget_us) {
      std::printf("PASS: P99 %llu us <= budget %llu us\n",
                  static_cast<unsigned long long>(p99),
                  static_cast<unsigned long long>(p99_budget_us));
      return 0;
    }
    std::printf("FAIL: P99 %llu us > budget %llu us\n",
                static_cast<unsigned long long>(p99),
                static_cast<unsigned long long>(p99_budget_us));
    return 1;
  }
  return 0;
}
