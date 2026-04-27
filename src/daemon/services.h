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
// actual model.
std::unique_ptr<IRerankService> MakeMockReverseRerankService();

// Phase 2.2: deterministic, scored reranker. Uses a small hand-curated
// bigram table + length / context heuristics. Not LLM-quality, but
// behaviorally meaningful (output depends on context, scores are real)
// and trivially testable. Suitable as the default backend until MLX is
// configured. model_version field is set to 1.
std::unique_ptr<IRerankService> MakeDeterministicRerankService();

// Phase 2.2: MLX-backed reranker. Returns nullptr if MLX is unavailable
// at build time; the daemon must fall back to a different backend in
// that case. Returns a service that fails IsReady() until the model is
// loaded successfully.
struct MLXRerankConfig {
  std::string model_path;       // path to MLX .npz / .safetensors
  uint32_t timeout_ms = 30;     // per-call deadline
  uint32_t warmup_iterations = 1;
};
std::unique_ptr<IRerankService> MakeMLXRerankService(
    const MLXRerankConfig& config);

// Phase 2.1: a no-op commit logger. Real impl (SQLite-backed) lands in
// Phase 2.4 as MakeSqliteCommitLogger.
std::unique_ptr<ICommitLogger> MakeNullCommitLogger();

}  // namespace dafeng
