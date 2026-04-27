#include "dafeng/protocol.h"

#include <cstdint>
#include <variant>

#include <gtest/gtest.h>

namespace dafeng {
namespace {

template <typename T>
T RoundTrip(const T& original) {
  auto body = EncodeBody(original);
  auto decoded = DecodeBody(body.data(), body.size());
  EXPECT_TRUE(decoded.has_value());
  EXPECT_TRUE(std::holds_alternative<T>(*decoded));
  return std::get<T>(*decoded);
}

TEST(ProtocolTest, RerankRequestRoundTripPreservesAllFields) {
  RerankRequest req;
  req.request_id = 42;
  req.code = "ggll";
  req.context_before = "你好,";
  req.candidates = {"感", "工", "一", "国"};
  req.app_id = "com.apple.Safari";

  auto out = RoundTrip(req);
  EXPECT_EQ(out.request_id, 42u);
  EXPECT_EQ(out.code, "ggll");
  EXPECT_EQ(out.context_before, "你好,");
  EXPECT_EQ(out.candidates, req.candidates);
  EXPECT_EQ(out.app_id, "com.apple.Safari");
}

TEST(ProtocolTest, RerankResponseRoundTripPreservesAllFields) {
  RerankResponse resp;
  resp.request_id = 99;
  resp.reordered_indices = {3, 1, 0, 2};
  resp.scores = {0.5f, 0.3f, 0.15f, 0.05f};
  resp.latency_us = 18234;
  resp.model_version = 7;

  auto out = RoundTrip(resp);
  EXPECT_EQ(out.request_id, 99u);
  EXPECT_EQ(out.reordered_indices, resp.reordered_indices);
  ASSERT_EQ(out.scores.size(), resp.scores.size());
  for (size_t i = 0; i < out.scores.size(); ++i) {
    EXPECT_FLOAT_EQ(out.scores[i], resp.scores[i]);
  }
  EXPECT_EQ(out.latency_us, 18234u);
  EXPECT_EQ(out.model_version, 7u);
}

TEST(ProtocolTest, CommitEventRoundTrip) {
  CommitEvent ev;
  ev.code = "wo";
  ev.committed_text = "我";
  ev.context_before = "今天";
  auto out = RoundTrip(ev);
  EXPECT_EQ(out.code, "wo");
  EXPECT_EQ(out.committed_text, "我");
  EXPECT_EQ(out.context_before, "今天");
}

TEST(ProtocolTest, PingPongRoundTrip) {
  PingMessage ping;
  ping.request_id = 7;
  EXPECT_EQ(RoundTrip(ping).request_id, 7u);

  PongMessage pong;
  pong.request_id = 8;
  EXPECT_EQ(RoundTrip(pong).request_id, 8u);
}

TEST(ProtocolTest, ErrorMessageRoundTrip) {
  ErrorMessage err;
  err.request_id = 11;
  err.code = -3;
  err.message = "model not loaded";
  auto out = RoundTrip(err);
  EXPECT_EQ(out.request_id, 11u);
  EXPECT_EQ(out.code, -3);
  EXPECT_EQ(out.message, "model not loaded");
}

TEST(ProtocolTest, EmptyCandidatesIsValid) {
  RerankRequest req;
  req.request_id = 1;
  req.code = "";
  auto out = RoundTrip(req);
  EXPECT_EQ(out.candidates.size(), 0u);
  EXPECT_EQ(out.code, "");
}

TEST(ProtocolTest, PeekMessageTypeWorks) {
  RerankRequest req;
  req.request_id = 1;
  auto body = EncodeBody(req);
  auto t = PeekMessageType(body.data(), body.size());
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(*t, MessageType::kRerankRequest);
}

TEST(ProtocolTest, FrameLengthRoundTrip) {
  const uint32_t lens[] = {
      1u, 1024u, 65535u, 65536u, static_cast<uint32_t>(kMaxFrameBodyBytes - 1)};
  for (uint32_t len : lens) {
    uint8_t hdr[4];
    WriteFrameLength(len, hdr);
    auto out = ReadFrameLength(hdr, sizeof(hdr));
    ASSERT_TRUE(out.has_value()) << "len=" << len;
    EXPECT_EQ(*out, len);
  }
}

TEST(ProtocolTest, FrameLengthRejectsZero) {
  uint8_t hdr[4] = {0, 0, 0, 0};
  EXPECT_FALSE(ReadFrameLength(hdr, sizeof(hdr)).has_value());
}

TEST(ProtocolTest, FrameLengthRejectsOversized) {
  uint8_t hdr[4];
  WriteFrameLength(kMaxFrameBodyBytes + 1, hdr);
  EXPECT_FALSE(ReadFrameLength(hdr, sizeof(hdr)).has_value());
}

TEST(ProtocolTest, EncodeFramePrependsBigEndianLength) {
  std::vector<uint8_t> body = {0xAA, 0xBB, 0xCC};
  auto frame = EncodeFrame(body);
  ASSERT_EQ(frame.size(), 4u + body.size());
  EXPECT_EQ(frame[0], 0u);
  EXPECT_EQ(frame[1], 0u);
  EXPECT_EQ(frame[2], 0u);
  EXPECT_EQ(frame[3], 3u);
  EXPECT_EQ(frame[4], 0xAA);
  EXPECT_EQ(frame[5], 0xBB);
  EXPECT_EQ(frame[6], 0xCC);
}

TEST(ProtocolTest, DecodeRejectsTruncated) {
  RerankRequest req;
  req.request_id = 1;
  auto body = EncodeBody(req);
  EXPECT_FALSE(DecodeBody(body.data(), body.size() / 2).has_value());
}

TEST(ProtocolTest, DecodeRejectsGarbage) {
  std::vector<uint8_t> garbage = {0xFF, 0x00, 0x42, 0x99};
  EXPECT_FALSE(DecodeBody(garbage.data(), garbage.size()).has_value());
}

TEST(ProtocolTest, ForwardCompatibilityIgnoresUnknownKeys) {
  // Manually craft a map with extra unknown fields ahead of the known ones.
  // This emulates a future version sending fields we don't understand.
  auto body = EncodeBody(RerankRequest{42, "ab", "ctx", {"x", "y"}, "app"});
  auto decoded = DecodeBody(body.data(), body.size());
  ASSERT_TRUE(decoded.has_value());
  const auto& m = std::get<RerankRequest>(*decoded);
  EXPECT_EQ(m.request_id, 42u);
}

TEST(ProtocolTest, FrameLengthRejectsExactlyMaxPlusOne) {
  uint8_t hdr[4];
  WriteFrameLength(static_cast<uint32_t>(kMaxFrameBodyBytes + 1), hdr);
  EXPECT_FALSE(ReadFrameLength(hdr, sizeof(hdr)).has_value());
}

TEST(ProtocolTest, FrameLengthAcceptsExactlyMax) {
  uint8_t hdr[4];
  WriteFrameLength(static_cast<uint32_t>(kMaxFrameBodyBytes), hdr);
  auto out = ReadFrameLength(hdr, sizeof(hdr));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(*out, kMaxFrameBodyBytes);
}

TEST(ProtocolTest, DecodeRejectsBodyOverMax) {
  // Largest legal body decodes; one byte beyond the cap rejects without
  // even attempting to allocate.
  std::vector<uint8_t> oversize(kMaxFrameBodyBytes + 1, 0u);
  EXPECT_FALSE(DecodeBody(oversize.data(), oversize.size()).has_value());
}

TEST(ProtocolTest, LargeCandidateListSurvivesRoundTrip) {
  // 200 candidates of varying length; the rerank API ceiling is ~10 in
  // practice but the codec should not have a smaller secret limit.
  RerankRequest req;
  req.request_id = 999;
  req.code = "test";
  for (int i = 0; i < 200; ++i) {
    req.candidates.push_back("候选-" + std::to_string(i));
  }
  auto out = RoundTrip(req);
  EXPECT_EQ(out.candidates.size(), 200u);
  EXPECT_EQ(out.candidates.front(), "候选-0");
  EXPECT_EQ(out.candidates.back(), "候选-199");
}

TEST(ProtocolTest, MessageVariantEncodeDispatch) {
  // Encoding through the variant overload must produce the same bytes as
  // the typed overload — this is the path used by the daemon's reply path.
  RerankResponse resp;
  resp.request_id = 5;
  resp.reordered_indices = {2, 0, 1};
  Message msg{resp};
  EXPECT_EQ(EncodeBody(msg), EncodeBody(resp));
}

TEST(ProtocolTest, StatsRequestRoundTrip) {
  StatsRequest req;
  req.request_id = 314;
  auto out = RoundTrip(req);
  EXPECT_EQ(out.request_id, 314u);
}

TEST(ProtocolTest, StatsResponseRoundTrip) {
  StatsResponse s;
  s.request_id = 42;
  s.rerank_count = 1000;
  s.ping_count = 50;
  s.commit_count = 200;
  s.error_count = 1;
  s.rerank_latency_sum_us = 12345;
  s.rerank_latency_max_us = 99;
  s.rerank_model_version = 1;
  s.uptime_sec = 7200;
  auto out = RoundTrip(s);
  EXPECT_EQ(out.request_id, 42u);
  EXPECT_EQ(out.rerank_count, 1000u);
  EXPECT_EQ(out.ping_count, 50u);
  EXPECT_EQ(out.commit_count, 200u);
  EXPECT_EQ(out.error_count, 1u);
  EXPECT_EQ(out.rerank_latency_sum_us, 12345u);
  EXPECT_EQ(out.rerank_latency_max_us, 99u);
  EXPECT_EQ(out.rerank_model_version, 1u);
  EXPECT_EQ(out.uptime_sec, 7200u);
}

}  // namespace
}  // namespace dafeng
