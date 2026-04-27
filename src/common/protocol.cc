#include "dafeng/protocol.h"

#include <cstring>

#include <msgpack.hpp>

namespace dafeng {

namespace {

// Map key strings, kept short and stable. Changing any of these is a wire
// break and must bump kProtocolVersion.
constexpr const char* kKeyVersion = "v";
constexpr const char* kKeyType = "t";
constexpr const char* kKeyId = "id";
constexpr const char* kKeyCode = "code";
constexpr const char* kKeyContext = "ctx";
constexpr const char* kKeyCandidates = "cands";
constexpr const char* kKeyAppId = "app";
constexpr const char* kKeyOrder = "order";
constexpr const char* kKeyScores = "scores";
constexpr const char* kKeyLatencyUs = "lat_us";
constexpr const char* kKeyModelVersion = "mver";
constexpr const char* kKeyText = "text";
constexpr const char* kKeyErrCode = "ec";
constexpr const char* kKeyErrMsg = "em";

template <typename Packer>
void PackKey(Packer& pk, const char* key) {
  const auto len = static_cast<uint32_t>(std::strlen(key));
  pk.pack_str(len);
  pk.pack_str_body(key, len);
}

template <typename Packer>
void PackHeader(Packer& pk, MessageType t) {
  PackKey(pk, kKeyVersion);
  pk.pack(kProtocolVersion);
  PackKey(pk, kKeyType);
  pk.pack(static_cast<uint8_t>(t));
}

std::vector<uint8_t> SbufToVec(const msgpack::sbuffer& sbuf) {
  const auto* p = reinterpret_cast<const uint8_t*>(sbuf.data());
  return std::vector<uint8_t>(p, p + sbuf.size());
}

bool KeyEquals(const msgpack::object_kv& kv, const char* literal) {
  if (kv.key.type != msgpack::type::STR) return false;
  const auto& s = kv.key.via.str;
  const auto len = std::strlen(literal);
  return s.size == len && std::memcmp(s.ptr, literal, len) == 0;
}

template <typename T>
bool MapGet(const msgpack::object& map_obj, const char* key, T& out) {
  if (map_obj.type != msgpack::type::MAP) return false;
  for (uint32_t i = 0; i < map_obj.via.map.size; ++i) {
    const auto& kv = map_obj.via.map.ptr[i];
    if (KeyEquals(kv, key)) {
      try {
        kv.val.convert(out);
        return true;
      } catch (...) {
        return false;
      }
    }
  }
  return false;
}

std::optional<MessageType> MapGetType(const msgpack::object& map_obj) {
  if (map_obj.type != msgpack::type::MAP) return std::nullopt;
  for (uint32_t i = 0; i < map_obj.via.map.size; ++i) {
    const auto& kv = map_obj.via.map.ptr[i];
    if (KeyEquals(kv, kKeyType)) {
      uint8_t t = 0;
      try {
        kv.val.convert(t);
      } catch (...) {
        return std::nullopt;
      }
      return static_cast<MessageType>(t);
    }
  }
  return std::nullopt;
}

}  // namespace

// ---- Encoders ---------------------------------------------------------------

std::vector<uint8_t> EncodeBody(const RerankRequest& msg) {
  msgpack::sbuffer sbuf;
  msgpack::packer<msgpack::sbuffer> pk(&sbuf);
  pk.pack_map(7);
  PackHeader(pk, MessageType::kRerankRequest);
  PackKey(pk, kKeyId); pk.pack(msg.request_id);
  PackKey(pk, kKeyCode); pk.pack(msg.code);
  PackKey(pk, kKeyContext); pk.pack(msg.context_before);
  PackKey(pk, kKeyCandidates); pk.pack(msg.candidates);
  PackKey(pk, kKeyAppId); pk.pack(msg.app_id);
  return SbufToVec(sbuf);
}

std::vector<uint8_t> EncodeBody(const RerankResponse& msg) {
  msgpack::sbuffer sbuf;
  msgpack::packer<msgpack::sbuffer> pk(&sbuf);
  pk.pack_map(7);
  PackHeader(pk, MessageType::kRerankResponse);
  PackKey(pk, kKeyId); pk.pack(msg.request_id);
  PackKey(pk, kKeyOrder); pk.pack(msg.reordered_indices);
  PackKey(pk, kKeyScores); pk.pack(msg.scores);
  PackKey(pk, kKeyLatencyUs); pk.pack(msg.latency_us);
  PackKey(pk, kKeyModelVersion); pk.pack(msg.model_version);
  return SbufToVec(sbuf);
}

std::vector<uint8_t> EncodeBody(const CommitEvent& msg) {
  msgpack::sbuffer sbuf;
  msgpack::packer<msgpack::sbuffer> pk(&sbuf);
  pk.pack_map(5);
  PackHeader(pk, MessageType::kCommitEvent);
  PackKey(pk, kKeyCode); pk.pack(msg.code);
  PackKey(pk, kKeyText); pk.pack(msg.committed_text);
  PackKey(pk, kKeyContext); pk.pack(msg.context_before);
  return SbufToVec(sbuf);
}

std::vector<uint8_t> EncodeBody(const PingMessage& msg) {
  msgpack::sbuffer sbuf;
  msgpack::packer<msgpack::sbuffer> pk(&sbuf);
  pk.pack_map(3);
  PackHeader(pk, MessageType::kPing);
  PackKey(pk, kKeyId); pk.pack(msg.request_id);
  return SbufToVec(sbuf);
}

std::vector<uint8_t> EncodeBody(const PongMessage& msg) {
  msgpack::sbuffer sbuf;
  msgpack::packer<msgpack::sbuffer> pk(&sbuf);
  pk.pack_map(3);
  PackHeader(pk, MessageType::kPong);
  PackKey(pk, kKeyId); pk.pack(msg.request_id);
  return SbufToVec(sbuf);
}

std::vector<uint8_t> EncodeBody(const ErrorMessage& msg) {
  msgpack::sbuffer sbuf;
  msgpack::packer<msgpack::sbuffer> pk(&sbuf);
  pk.pack_map(5);
  PackHeader(pk, MessageType::kError);
  PackKey(pk, kKeyId); pk.pack(msg.request_id);
  PackKey(pk, kKeyErrCode); pk.pack(msg.code);
  PackKey(pk, kKeyErrMsg); pk.pack(msg.message);
  return SbufToVec(sbuf);
}

std::vector<uint8_t> EncodeBody(const Message& msg) {
  return std::visit([](const auto& m) { return EncodeBody(m); }, msg);
}

// ---- Decoder ---------------------------------------------------------------

std::optional<Message> DecodeBody(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0 || size > kMaxFrameBodyBytes) {
    return std::nullopt;
  }

  msgpack::object_handle oh;
  try {
    oh = msgpack::unpack(reinterpret_cast<const char*>(data), size);
  } catch (...) {
    return std::nullopt;
  }
  msgpack::object obj = oh.get();

  auto type_opt = MapGetType(obj);
  if (!type_opt) return std::nullopt;

  switch (*type_opt) {
    case MessageType::kRerankRequest: {
      RerankRequest m;
      MapGet(obj, kKeyId, m.request_id);
      MapGet(obj, kKeyCode, m.code);
      MapGet(obj, kKeyContext, m.context_before);
      MapGet(obj, kKeyCandidates, m.candidates);
      MapGet(obj, kKeyAppId, m.app_id);
      return Message{std::move(m)};
    }
    case MessageType::kRerankResponse: {
      RerankResponse m;
      MapGet(obj, kKeyId, m.request_id);
      MapGet(obj, kKeyOrder, m.reordered_indices);
      MapGet(obj, kKeyScores, m.scores);
      MapGet(obj, kKeyLatencyUs, m.latency_us);
      MapGet(obj, kKeyModelVersion, m.model_version);
      return Message{std::move(m)};
    }
    case MessageType::kCommitEvent: {
      CommitEvent m;
      MapGet(obj, kKeyCode, m.code);
      MapGet(obj, kKeyText, m.committed_text);
      MapGet(obj, kKeyContext, m.context_before);
      return Message{std::move(m)};
    }
    case MessageType::kPing: {
      PingMessage m;
      MapGet(obj, kKeyId, m.request_id);
      return Message{std::move(m)};
    }
    case MessageType::kPong: {
      PongMessage m;
      MapGet(obj, kKeyId, m.request_id);
      return Message{std::move(m)};
    }
    case MessageType::kError: {
      ErrorMessage m;
      MapGet(obj, kKeyId, m.request_id);
      MapGet(obj, kKeyErrCode, m.code);
      MapGet(obj, kKeyErrMsg, m.message);
      return Message{std::move(m)};
    }
    case MessageType::kInvalid:
    default:
      return std::nullopt;
  }
}

std::optional<MessageType> PeekMessageType(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0 || size > kMaxFrameBodyBytes) {
    return std::nullopt;
  }
  msgpack::object_handle oh;
  try {
    oh = msgpack::unpack(reinterpret_cast<const char*>(data), size);
  } catch (...) {
    return std::nullopt;
  }
  return MapGetType(oh.get());
}

// ---- Framing ----------------------------------------------------------------

void WriteFrameLength(uint32_t length, uint8_t* out4) {
  out4[0] = static_cast<uint8_t>((length >> 24) & 0xFF);
  out4[1] = static_cast<uint8_t>((length >> 16) & 0xFF);
  out4[2] = static_cast<uint8_t>((length >> 8) & 0xFF);
  out4[3] = static_cast<uint8_t>(length & 0xFF);
}

std::optional<uint32_t> ReadFrameLength(const uint8_t* header, size_t size) {
  if (header == nullptr || size < kFrameHeaderBytes) return std::nullopt;
  const uint32_t length = (static_cast<uint32_t>(header[0]) << 24) |
                          (static_cast<uint32_t>(header[1]) << 16) |
                          (static_cast<uint32_t>(header[2]) << 8) |
                          static_cast<uint32_t>(header[3]);
  if (length == 0 || length > kMaxFrameBodyBytes) return std::nullopt;
  return length;
}

std::vector<uint8_t> EncodeFrame(const std::vector<uint8_t>& body) {
  std::vector<uint8_t> frame;
  frame.reserve(kFrameHeaderBytes + body.size());
  frame.resize(kFrameHeaderBytes);
  WriteFrameLength(static_cast<uint32_t>(body.size()), frame.data());
  frame.insert(frame.end(), body.begin(), body.end());
  return frame;
}

}  // namespace dafeng
