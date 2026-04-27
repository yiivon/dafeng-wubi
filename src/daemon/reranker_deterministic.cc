// Deterministic, scored reranker. The output depends on the candidate
// strings and the input context — unlike the Phase 2.1 mock-reverse it
// produces orderings that look "real" — but the rules are hand-coded so
// the reranker is reproducible and trivially fast.
//
// Use cases:
//   - Phase 2.2 default backend until MLX is configured.
//   - Test fixture for Phase 2.3 reranker integration code.
//   - Latency baseline (~1 us per candidate, well under any budget).
//
// What this is NOT: LLM-quality semantic ranking. The bigram table is
// ~30 entries chosen to demonstrate context-sensitive scoring; it is
// deliberately not "trained" or comprehensive.

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "services.h"

namespace dafeng {

namespace {

// Hand-picked common bigrams. Each entry says "if the last UTF-8 char of
// the context is `prev`, prefer candidates whose first UTF-8 char is
// `favor`." Order does not matter; ties are broken by length / hash.
struct Bigram {
  const char* prev;
  const char* favor;
};

constexpr Bigram kBigrams[] = {
    {"今", "天"}, {"今", "日"}, {"今", "年"},
    {"明", "天"}, {"明", "年"}, {"明", "白"},
    {"昨", "天"}, {"前", "天"}, {"后", "天"},
    {"天", "气"}, {"天", "上"}, {"天", "空"},
    {"我", "们"}, {"我", "的"}, {"我", "是"},
    {"你", "们"}, {"你", "的"}, {"你", "是"}, {"你", "好"},
    {"他", "们"}, {"她", "们"}, {"它", "们"},
    {"中", "国"}, {"中", "文"}, {"中", "间"},
    {"一", "个"}, {"一", "些"}, {"一", "样"}, {"一", "直"},
    {"是", "的"}, {"的", "是"}, {"的", "话"},
    {"这", "个"}, {"这", "是"}, {"这", "些"}, {"这", "样"},
    {"那", "个"}, {"那", "是"}, {"那", "些"}, {"那", "样"},
    {"什", "么"}, {"怎", "么"}, {"为", "什"},
    {"工", "作"}, {"学", "习"}, {"时", "候"},
    {"问", "题"}, {"地", "方"}, {"东", "西"},
    {"开", "始"}, {"结", "束"}, {"知", "道"},
};

// FNV-1a 32-bit. Stable across platforms / libc versions, which std::hash
// is not — we want test outputs to match across machines.
uint32_t FnvHash(const std::string& s) {
  uint32_t h = 0x811C9DC5u;
  for (unsigned char c : s) {
    h ^= c;
    h *= 0x01000193u;
  }
  return h;
}

// Returns the bytes of the last UTF-8 character in `s`, or an empty string.
std::string LastUtf8Char(const std::string& s) {
  if (s.empty()) return {};
  std::size_t i = s.size();
  // Walk back over continuation bytes (10xxxxxx).
  do {
    --i;
  } while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
  return s.substr(i);
}

bool StartsWith(const std::string& s, const char* prefix) {
  const std::size_t plen = std::strlen(prefix);
  return s.size() >= plen && std::memcmp(s.data(), prefix, plen) == 0;
}

float ScoreCandidate(const std::string& cand, const std::string& last_char,
                      const std::string& full_ctx) {
  float score = 0.0f;

  // Avoid immediate single-char repetition: heavy negative if cand starts
  // with the same character that just ended the context.
  if (!cand.empty() && !last_char.empty() && StartsWith(cand, last_char.c_str())) {
    score -= 5.0f;
  }

  // Bigram bonus: if the last char of context has a known follower, and the
  // candidate starts with that follower, big boost.
  if (!last_char.empty()) {
    for (const auto& e : kBigrams) {
      if (last_char == e.prev && StartsWith(cand, e.favor)) {
        score += 10.0f;
        break;
      }
    }
  }

  // Mild preference for slightly longer phrases — clipped at ~3 UTF-8 chars
  // (UTF-8 chars in CJK are 3 bytes, so 12 bytes ~= 4 chars).
  score += 0.5f * std::min<int>(static_cast<int>(cand.size()), 12);

  // Tiny deterministic tie-breaker so two similarly-scored candidates don't
  // permute on identical inputs. FNV-1a is deterministic and stable.
  score += static_cast<float>(FnvHash(cand + full_ctx) % 7) * 0.1f;

  return score;
}

class DeterministicReranker final : public IRerankService {
 public:
  RerankResponse Rerank(const RerankRequest& req) override {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();

    RerankResponse resp;
    resp.request_id = req.request_id;
    resp.model_version = kModelVersion;

    const int n = static_cast<int>(req.candidates.size());
    if (n == 0) {
      resp.latency_us = 0;
      return resp;
    }

    const std::string last_char = LastUtf8Char(req.context_before);

    std::vector<std::pair<float, int>> scored;
    scored.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      const float s = ScoreCandidate(req.candidates[i], last_char,
                                       req.context_before);
      scored.emplace_back(s, i);
    }

    // Sort by score descending; stable so equal scores keep the librime
    // ordering — this preserves muscle memory on ties (DESIGN §9.2 R5).
    std::stable_sort(
        scored.begin(), scored.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });

    resp.reordered_indices.reserve(static_cast<std::size_t>(n));
    resp.scores.reserve(static_cast<std::size_t>(n));
    for (const auto& [s, idx] : scored) {
      resp.reordered_indices.push_back(idx);
      resp.scores.push_back(s);
    }

    resp.latency_us = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(clock::now() -
                                                                t0)
            .count());
    return resp;
  }

  bool IsReady() const override { return true; }

 private:
  static constexpr uint8_t kModelVersion = 1;
};

}  // namespace

std::unique_ptr<IRerankService> MakeDeterministicRerankService() {
  return std::make_unique<DeterministicReranker>();
}

}  // namespace dafeng
