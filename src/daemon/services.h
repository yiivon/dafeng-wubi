#pragma once

// Daemon-internal service interfaces. These are NOT part of the public ABI —
// they live in src/daemon to make the eventual MLX / llama.cpp swap a single
// factory change.

#include <memory>

#include "dafeng/types.h"

namespace dafeng {

class IRerankService {
 public:
  virtual ~IRerankService() = default;

  // Hot path. Latency budget is set by the caller (DESIGN §3 = 30 ms cap).
  // Implementations are expected to be reentrant — the daemon may run
  // multiple Rerank calls concurrently.
  virtual RerankResponse Rerank(const RerankRequest& req) = 0;

  // Returns true once the model is loaded and ready to serve.
  virtual bool IsReady() const = 0;
};

class ICommitLogger {
 public:
  virtual ~ICommitLogger() = default;

  // Cold path. Fire-and-forget — never blocks the IME's hot path.
  virtual void Record(const CommitEvent& ev) = 0;
};

// Phase 2.1: a deterministic mock that reverses the candidate list. Used
// to prove the IPC pipeline is working end to end without paying for an
// actual model. Replaced by MakeMLXRerankService / MakeLlamaCppRerankService
// in Phase 2.2.
std::unique_ptr<IRerankService> MakeMockReverseRerankService();

// Phase 2.1: a no-op commit logger. Real impl in Phase 2.4 with SQLite.
std::unique_ptr<ICommitLogger> MakeNullCommitLogger();

}  // namespace dafeng
