// Reranker-only latency benchmark — no IPC, no daemon process. Measures
// raw service throughput so we can isolate "is the algorithm fast enough"
// from "is the IPC overhead in budget" (the latter is ipc_pingpong's job).
//
// DESIGN §3 budget for the rerank step: P99 < 30 ms. The deterministic
// backend should hit this with a margin of three orders of magnitude;
// MLX is the variable.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/daemon/services.h"

namespace {

uint64_t Percentile(std::vector<uint64_t>& samples, double pct) {
  if (samples.empty()) return 0;
  std::size_t i = static_cast<std::size_t>(samples.size() * pct);
  if (i >= samples.size()) i = samples.size() - 1;
  std::nth_element(samples.begin(), samples.begin() + i, samples.end());
  return samples[i];
}

dafeng::RerankRequest MakeRealisticRequest() {
  // Approximation of a real librime hand-off: short code, ~10 candidates,
  // a sentence of context that ends in a "rich" character.
  dafeng::RerankRequest req;
  req.code = "ggll";
  req.context_before = "今天我们一起去吃";
  req.candidates = {"饭", "面", "饺", "炒", "烤",
                     "煮", "菜", "汤", "锅", "肉"};
  req.app_id = "com.apple.Notes";
  return req;
}

}  // namespace

int main(int argc, char** argv) {
  int iterations = 5000;
  int warmup = 200;
  uint64_t p99_budget_us = 30'000;  // 30 ms = DESIGN §3 LLM cap
  std::string backend = "deterministic";
  std::string model_path;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
      iterations = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
      warmup = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--p99-budget-us") == 0 && i + 1 < argc) {
      p99_budget_us = static_cast<uint64_t>(std::atoll(argv[++i]));
    } else if (std::strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
      backend = argv[++i];
    } else if (std::strcmp(argv[i], "--model-path") == 0 && i + 1 < argc) {
      model_path = argv[++i];
    } else if (std::strcmp(argv[i], "--help") == 0 ||
               std::strcmp(argv[i], "-h") == 0) {
      std::printf(
          "Usage: reranker_latency [--backend NAME] [--iterations N]\n"
          "                        [--warmup N] [--p99-budget-us N]\n"
          "                        [--model-path PATH]\n"
          "  backend  : mock | deterministic | mlx (default: deterministic)\n"
          "  budget   : default 30000 us (DESIGN section 3 LLM cap)\n");
      return 0;
    }
  }

  std::unique_ptr<dafeng::IRerankService> svc;
  if (backend == "mock") {
    svc = dafeng::MakeMockReverseRerankService();
  } else if (backend == "deterministic") {
    svc = dafeng::MakeDeterministicRerankService();
  } else if (backend == "mlx") {
    dafeng::MLXRerankConfig cfg;
    cfg.model_path = model_path;
    svc = dafeng::MakeMLXRerankService(cfg);
    if (!svc) {
      std::fprintf(stderr,
                    "MLX backend unavailable; "
                    "rebuild with -DDAFENG_ENABLE_MLX=ON and pass --model-path\n");
      return 2;
    }
  } else if (backend == "llama_cpp" || backend == "llamacpp") {
    dafeng::MLXRerankConfig cfg;
    cfg.model_path = model_path;
    cfg.warmup_iterations = 2;
    svc = dafeng::MakeLlamaCppRerankService(cfg);
    if (!svc) {
      std::fprintf(stderr,
                    "llama_cpp backend unavailable; rebuild with "
                    "-DDAFENG_ENABLE_LLAMA_CPP=ON and pass --model-path "
                    "(GGUF file from tools/download_model.py)\n");
      return 2;
    }
  } else {
    std::fprintf(stderr, "unknown backend: %s\n", backend.c_str());
    return 1;
  }

  auto req = MakeRealisticRequest();

  for (int i = 0; i < warmup; ++i) (void)svc->Rerank(req);

  std::vector<uint64_t> latencies_us;
  latencies_us.reserve(iterations);
  for (int i = 0; i < iterations; ++i) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto resp = svc->Rerank(req);
    const auto t1 = std::chrono::steady_clock::now();
    (void)resp;
    latencies_us.push_back(static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()));
  }

  const auto p50 = Percentile(latencies_us, 0.50);
  const auto p95 = Percentile(latencies_us, 0.95);
  const auto p99 = Percentile(latencies_us, 0.99);
  const auto p999 = Percentile(latencies_us, 0.999);
  const auto maxv = *std::max_element(latencies_us.begin(), latencies_us.end());
  uint64_t sum = 0;
  for (auto v : latencies_us) sum += v;
  const auto mean = sum / latencies_us.size();

  std::printf("reranker_latency: backend=%s iterations=%d candidates=%zu\n",
              backend.c_str(), iterations, req.candidates.size());
  std::printf("  mean : %6llu us\n", static_cast<unsigned long long>(mean));
  std::printf("  P50  : %6llu us\n", static_cast<unsigned long long>(p50));
  std::printf("  P95  : %6llu us\n", static_cast<unsigned long long>(p95));
  std::printf("  P99  : %6llu us\n", static_cast<unsigned long long>(p99));
  std::printf("  P999 : %6llu us\n", static_cast<unsigned long long>(p999));
  std::printf("  max  : %6llu us\n", static_cast<unsigned long long>(maxv));

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
