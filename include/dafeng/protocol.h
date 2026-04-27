#pragma once

// Wire-format encoders and decoders. msgpack is the encoding; this header
// does not expose it — implementation details live in src/common/protocol.cc.
//
// Frame layout:
//   [4 bytes big-endian length][MessagePack body]
// Body is a map keyed by short strings ("v", "t", "id", ...). Map encoding
// (vs array) gives forward-compatibility: unknown keys are ignored, missing
// keys take their default value.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "dafeng/api.h"
#include "dafeng/types.h"

namespace dafeng {

inline constexpr size_t kFrameHeaderBytes = 4;
inline constexpr size_t kMaxFrameBodyBytes = 1u << 20;  // 1 MiB hard cap.

using Message = std::variant<RerankRequest, RerankResponse, CommitEvent,
                             PingMessage, PongMessage, ErrorMessage>;

// Body encoders. Caller is responsible for prepending the length prefix
// (see EncodeFrame) before writing to a stream.
DAFENG_API std::vector<uint8_t> EncodeBody(const RerankRequest& msg);
DAFENG_API std::vector<uint8_t> EncodeBody(const RerankResponse& msg);
DAFENG_API std::vector<uint8_t> EncodeBody(const CommitEvent& msg);
DAFENG_API std::vector<uint8_t> EncodeBody(const PingMessage& msg);
DAFENG_API std::vector<uint8_t> EncodeBody(const PongMessage& msg);
DAFENG_API std::vector<uint8_t> EncodeBody(const ErrorMessage& msg);
DAFENG_API std::vector<uint8_t> EncodeBody(const Message& msg);

// Returns nullopt on malformed input or on size > kMaxFrameBodyBytes.
DAFENG_API std::optional<Message> DecodeBody(const uint8_t* data, size_t size);

// Frame helpers — manage the 4-byte big-endian length header.
DAFENG_API std::vector<uint8_t> EncodeFrame(const std::vector<uint8_t>& body);
DAFENG_API std::optional<uint32_t> ReadFrameLength(const uint8_t* header,
                                                   size_t size);
DAFENG_API void WriteFrameLength(uint32_t length, uint8_t* out4);

// Convenience: peek at the type tag without fully decoding the body. Used
// by routers that dispatch on type before paying the full decode cost.
DAFENG_API std::optional<MessageType> PeekMessageType(const uint8_t* data,
                                                      size_t size);

}  // namespace dafeng
