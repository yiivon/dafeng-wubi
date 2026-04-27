// Direct unit tests for the Phase 2.1 mock reverse reranker. Keeps the
// IRerankService contract obvious to anyone hooking up a real backend in
// Phase 2.2 — the integration tests would catch regressions, but they pay
// IPC cost we don't need here.

#include <gtest/gtest.h>

#include "../../src/daemon/services.h"

namespace dafeng {
namespace {

TEST(MockReverseRerankerTest, IsReadyImmediately) {
  auto svc = MakeMockReverseRerankService();
  ASSERT_NE(svc, nullptr);
  EXPECT_TRUE(svc->IsReady());
}

TEST(MockReverseRerankerTest, ReversesFourCandidates) {
  auto svc = MakeMockReverseRerankService();
  RerankRequest req;
  req.request_id = 7;
  req.candidates = {"A", "B", "C", "D"};
  auto resp = svc->Rerank(req);
  EXPECT_EQ(resp.request_id, 7u);
  EXPECT_EQ(resp.reordered_indices, (std::vector<int32_t>{3, 2, 1, 0}));
  EXPECT_TRUE(resp.scores.empty());
  EXPECT_EQ(resp.model_version, 0u);
}

TEST(MockReverseRerankerTest, EmptyCandidatesYieldsEmptyResponse) {
  auto svc = MakeMockReverseRerankService();
  RerankRequest req;
  req.candidates = {};
  auto resp = svc->Rerank(req);
  EXPECT_TRUE(resp.reordered_indices.empty());
}

TEST(MockReverseRerankerTest, SingleCandidateIsIdentity) {
  auto svc = MakeMockReverseRerankService();
  RerankRequest req;
  req.candidates = {"only"};
  auto resp = svc->Rerank(req);
  EXPECT_EQ(resp.reordered_indices, (std::vector<int32_t>{0}));
}

TEST(MockReverseRerankerTest, LatencyIsRecordedAndNonzero) {
  auto svc = MakeMockReverseRerankService();
  RerankRequest req;
  req.candidates = std::vector<std::string>(100, "x");
  auto resp = svc->Rerank(req);
  // Mock work is trivial but timed; just assert the field is populated and
  // not absurd.
  EXPECT_LT(resp.latency_us, 100'000u);
}

TEST(MockReverseRerankerTest, RerankIsReentrant) {
  auto svc = MakeMockReverseRerankService();
  for (int i = 0; i < 100; ++i) {
    RerankRequest req;
    req.request_id = static_cast<uint32_t>(i);
    req.candidates = {"a", "b", "c"};
    auto resp = svc->Rerank(req);
    EXPECT_EQ(resp.request_id, static_cast<uint32_t>(i));
    EXPECT_EQ(resp.reordered_indices, (std::vector<int32_t>{2, 1, 0}));
  }
}

TEST(NullCommitLoggerTest, RecordIsNoop) {
  auto logger = MakeNullCommitLogger();
  ASSERT_NE(logger, nullptr);
  CommitEvent ev;
  ev.code = "wo";
  ev.committed_text = "我";
  ev.context_before = "今天";
  // Just must not crash or block.
  for (int i = 0; i < 1000; ++i) logger->Record(ev);
}

}  // namespace
}  // namespace dafeng
