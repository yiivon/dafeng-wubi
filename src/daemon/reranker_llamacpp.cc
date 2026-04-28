// llama.cpp-backed reranker. Compiled when DAFENG_ENABLE_LLAMA_CPP=ON.
// Falls back to a stub at link time when OFF (see reranker_llamacpp_stub.cc).
//
// Design choice (DESIGN appendix §7.3): rerank == score, not generate.
// For each request:
//   1. Build a short prompt from `context_before` (≤ 64 tokens).
//   2. Decode the prompt once → get next-position logits.
//   3. For each candidate, look up its leading token's logit.
//   4. Sort candidates by descending logit.
//
// This is one forward pass per request, not one per candidate.
// On M-series with Qwen 2.5 0.5B Q4_K_M, target P99 < 30 ms.

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <ggml-backend.h>
#include <llama.h>

#include "dafeng/logging.h"
#include "services.h"

namespace dafeng {

namespace {

// We score by the probability the model would assign to a candidate's
// FIRST token following the prompt. For multi-token candidates (long
// phrases) this is an approximation — a future iteration can run a
// per-candidate forward to score the full candidate. Good first cut.

class LlamaCppReranker final : public IRerankService {
 public:
  explicit LlamaCppReranker(MLXRerankConfig config)
      : config_(std::move(config)) {}

  ~LlamaCppReranker() override {
    if (ctx_ != nullptr) llama_free(ctx_);
    if (model_ != nullptr) llama_model_free(model_);
  }

  bool Initialize() {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (config_.model_path.empty() ||
        !fs::exists(config_.model_path, ec) || ec) {
      DAFENG_LOG_WARN("llama_cpp: model not found at %s",
                       config_.model_path.c_str());
      return false;
    }

    // Process-wide backend init (ref-counted via static counter).
    InitBackendOnce();

    llama_model_params mparams = llama_model_default_params();
    // Use Metal on Apple Silicon by default; -1 = "as many layers as fit
    // on GPU". For small models the whole thing fits.
    mparams.n_gpu_layers = -1;
    model_ = llama_model_load_from_file(config_.model_path.c_str(), mparams);
    if (model_ == nullptr) {
      DAFENG_LOG_ERROR("llama_cpp: model load failed");
      return false;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = 512;     // generous; we only feed up to ~80 tokens
    cparams.n_batch = 512;   // batch size for prefill
    cparams.n_threads = 4;
    cparams.n_threads_batch = 4;
    ctx_ = llama_init_from_model(model_, cparams);
    if (ctx_ == nullptr) {
      DAFENG_LOG_ERROR("llama_cpp: context init failed");
      llama_model_free(model_);
      model_ = nullptr;
      return false;
    }

    vocab_ = llama_model_get_vocab(model_);

    // Warmup: a couple of small forward passes so the first real
    // request doesn't pay the JIT / cache miss cost.
    for (uint32_t i = 0; i < config_.warmup_iterations; ++i) {
      RerankRequest dummy;
      dummy.context_before = "今天";
      dummy.candidates = {"是", "的", "好"};
      (void)Rerank(dummy);
    }

    DAFENG_LOG_INFO("llama_cpp: ready, model=%s, n_ctx=%u",
                     config_.model_path.c_str(), cparams.n_ctx);
    ready_ = true;
    return true;
  }

  RerankResponse Rerank(const RerankRequest& req) override {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();

    RerankResponse resp;
    resp.request_id = req.request_id;
    resp.model_version = kModelVersion;

    const int n = static_cast<int>(req.candidates.size());
    if (n == 0) return resp;

    std::lock_guard<std::mutex> lock(mu_);

    // Score; on any failure we fall back to identity ordering so the
    // caller (Server) can still produce a response.
    auto scored = ScoreCandidates(req);
    if (!scored.empty()) {
      // Sort by score descending; stable sort keeps tied candidates in
      // librime's incoming order (DESIGN §9.2 R5 stability).
      std::stable_sort(
          scored.begin(), scored.end(),
          [](const auto& a, const auto& b) { return a.first > b.first; });
      resp.reordered_indices.reserve(static_cast<std::size_t>(n));
      resp.scores.reserve(static_cast<std::size_t>(n));
      for (const auto& [s, idx] : scored) {
        resp.reordered_indices.push_back(idx);
        resp.scores.push_back(s);
      }
    } else {
      resp.reordered_indices.reserve(static_cast<std::size_t>(n));
      for (int i = 0; i < n; ++i) resp.reordered_indices.push_back(i);
    }

    resp.latency_us = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(clock::now() -
                                                                t0)
            .count());
    return resp;
  }

  bool IsReady() const override { return ready_; }

 private:
  // Returns vector of (score, original_index) pairs, OR empty on failure.
  std::vector<std::pair<float, int>> ScoreCandidates(const RerankRequest& req) {
    if (ctx_ == nullptr || vocab_ == nullptr) return {};

    // 1. Tokenize prompt.
    auto prompt_tokens = Tokenize(req.context_before, /*add_bos=*/true);
    if (prompt_tokens.empty()) {
      // No context — fall back to a single BOS so the forward pass has
      // something. This makes scoring uniform-ish but still produces
      // candidate-specific logits for very common tokens.
      llama_token bos = llama_vocab_bos(vocab_);
      if (bos != LLAMA_TOKEN_NULL) prompt_tokens.push_back(bos);
    }
    if (prompt_tokens.empty()) return {};
    if (prompt_tokens.size() > 256) {
      // Trim from the front — keep most-recent context.
      prompt_tokens.erase(
          prompt_tokens.begin(),
          prompt_tokens.begin() +
              (prompt_tokens.size() - 256));
    }

    // 2. Decode the prompt. KV cache is reset per call (small prompts,
    // simpler than incremental cache management).
    llama_memory_clear(llama_get_memory(ctx_), /*data=*/true);
    llama_batch batch = llama_batch_get_one(prompt_tokens.data(),
                                              static_cast<int32_t>(prompt_tokens.size()));
    // Tell llama_decode we want logits at the LAST token only.
    if (llama_decode(ctx_, batch) != 0) {
      DAFENG_LOG_WARN("llama_cpp: decode failed");
      return {};
    }

    // 3. Get logits at the last position (where the next token would go).
    const float* logits = llama_get_logits_ith(
        ctx_, static_cast<int32_t>(prompt_tokens.size() - 1));
    if (logits == nullptr) {
      DAFENG_LOG_WARN("llama_cpp: logits unavailable");
      return {};
    }

    // 4. For each candidate, tokenize and score by its first token's logit.
    const int n = static_cast<int>(req.candidates.size());
    std::vector<std::pair<float, int>> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      auto cand_tokens = Tokenize(req.candidates[i], /*add_bos=*/false);
      if (cand_tokens.empty()) {
        out.emplace_back(-1e9f, i);  // can't score → sink
        continue;
      }
      const llama_token first = cand_tokens[0];
      const int vocab_size = llama_vocab_n_tokens(vocab_);
      if (first < 0 || first >= vocab_size) {
        out.emplace_back(-1e9f, i);
        continue;
      }
      out.emplace_back(logits[first], i);
    }
    return out;
  }

  std::vector<llama_token> Tokenize(const std::string& s, bool add_bos) {
    if (vocab_ == nullptr) return {};
    if (s.empty()) return {};
    // First call: figure out the size we need.
    int needed = -llama_tokenize(vocab_, s.data(), static_cast<int32_t>(s.size()),
                                   nullptr, 0, add_bos, /*parse_special=*/false);
    if (needed <= 0) return {};
    std::vector<llama_token> out(static_cast<std::size_t>(needed));
    int got = llama_tokenize(vocab_, s.data(), static_cast<int32_t>(s.size()),
                              out.data(), needed, add_bos,
                              /*parse_special=*/false);
    if (got < 0) return {};
    out.resize(static_cast<std::size_t>(got));
    return out;
  }

  static void InitBackendOnce() {
    static std::once_flag flag;
    std::call_once(flag, [] {
      // ggml's backend search: discovers libggml-{cpu,metal,blas}.so from
      // standard paths. MUST happen before llama_model_load_from_file or
      // you get "no backends are loaded". The default search uses
      // DYLD_LIBRARY_PATH on Mac, which a launchd-spawned daemon won't
      // have set. So after the default attempt, we also probe brew's
      // canonical ggml libexec paths — covers all common installs.
      ggml_backend_load_all();
      const char* candidate_paths[] = {
          "/opt/homebrew/opt/ggml/libexec",      // Apple Silicon brew
          "/usr/local/opt/ggml/libexec",          // Intel brew
          "/opt/homebrew/lib/ggml",               // alternate layout
          "/usr/local/lib/ggml",
      };
      for (const char* p : candidate_paths) {
        ggml_backend_load_all_from_path(p);  // no-op if path missing
      }
      // Allow override / addition via env, useful for non-brew installs.
      if (const char* env = std::getenv("DAFENG_GGML_LIBEXEC")) {
        ggml_backend_load_all_from_path(env);
      }
      llama_backend_init();
      // Quiet llama.cpp's per-token logging in production. Flip to a
      // forwarder that goes to DAFENG_LOG_DEBUG if you ever need to see
      // model-load progress live.
      llama_log_set([](enum ggml_log_level, const char*, void*) {}, nullptr);
    });
  }

  static constexpr uint8_t kModelVersion = 3;

  MLXRerankConfig config_;  // re-using this struct since it has model_path
  std::mutex mu_;            // serialize Rerank — context is single-threaded
  llama_model* model_ = nullptr;
  llama_context* ctx_ = nullptr;
  const llama_vocab* vocab_ = nullptr;
  bool ready_ = false;
};

}  // namespace

std::unique_ptr<IRerankService> MakeLlamaCppRerankService(
    const MLXRerankConfig& config) {
  auto svc = std::make_unique<LlamaCppReranker>(config);
  if (!svc->Initialize()) return nullptr;
  return svc;
}

}  // namespace dafeng
