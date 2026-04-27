#pragma once

#include <atomic>
#include <chrono>
#include <memory>

#include "dafeng/endpoint.h"
#include "dafeng/types.h"
#include "services.h"

namespace dafeng {

class Server {
 public:
  Server(std::unique_ptr<EndpointServer> endpoint,
         std::unique_ptr<IRerankService> reranker,
         std::unique_ptr<ICommitLogger> logger);
  ~Server();

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  // Block in the accept loop until Shutdown() is called or the endpoint
  // returns an error. Spawns a detached worker thread per accepted client.
  void Run();

  // Async-signal-safe — delegates to EndpointServer::Shutdown which only
  // writes one byte to a self-pipe.
  void Shutdown();

  // Snapshot the current counters. Safe to call from any thread.
  StatsResponse GetStats() const;

 private:
  void HandleConnection(std::unique_ptr<Connection> conn);

  std::unique_ptr<EndpointServer> endpoint_;
  std::unique_ptr<IRerankService> reranker_;
  std::unique_ptr<ICommitLogger> logger_;

  // Monotonic counters. Update from worker threads, read from anywhere.
  // Plain relaxed atomics — counter sums don't need release/acquire.
  std::atomic<uint64_t> rerank_count_{0};
  std::atomic<uint64_t> ping_count_{0};
  std::atomic<uint64_t> commit_count_{0};
  std::atomic<uint64_t> error_count_{0};
  std::atomic<uint64_t> rerank_latency_sum_us_{0};
  std::atomic<uint32_t> rerank_latency_max_us_{0};
  std::atomic<uint8_t> rerank_model_version_{0};

  // Used to compute uptime in StatsResponse. Wall-clock would tie us to
  // ntp jumps; steady_clock is monotonic.
  std::chrono::steady_clock::time_point start_time_;
};

}  // namespace dafeng
