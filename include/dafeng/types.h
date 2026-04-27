#pragma once

// Public message types shared between the IME-side client and the daemon.
// This header is part of the stable public surface — it must compile on Mac
// and Windows with no platform-specific headers leaking in.

#include <cstdint>
#include <string>
#include <vector>

namespace dafeng {

// Wire protocol version. Bumped only on incompatible changes.
inline constexpr uint32_t kProtocolVersion = 1;

enum class MessageType : uint8_t {
  kInvalid = 0,
  kRerankRequest = 1,
  kRerankResponse = 2,
  kCommitEvent = 3,
  kPing = 4,
  kPong = 5,
  kError = 6,
};

struct RerankRequest {
  uint32_t request_id = 0;
  std::string code;
  std::string context_before;
  std::vector<std::string> candidates;
  std::string app_id;
};

struct RerankResponse {
  uint32_t request_id = 0;
  // Permutation over the request's candidate list. Must be the same length.
  std::vector<int32_t> reordered_indices;
  // Optional per-candidate scores; same length as reordered_indices or empty.
  std::vector<float> scores;
  uint32_t latency_us = 0;
  uint8_t model_version = 0;
};

// Fire-and-forget. request_id is unused (always 0) — there is no response.
struct CommitEvent {
  std::string code;
  std::string committed_text;
  std::string context_before;
};

struct PingMessage {
  uint32_t request_id = 0;
};

struct PongMessage {
  uint32_t request_id = 0;
};

struct ErrorMessage {
  uint32_t request_id = 0;
  int32_t code = 0;
  std::string message;
};

}  // namespace dafeng
