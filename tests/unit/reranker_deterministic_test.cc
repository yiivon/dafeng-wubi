// Behavioral tests for DeterministicReranker. Pins down: same input -> same
// output, output is a valid permutation, context-sensitive scoring, latency
// budget, and a few specific bigram-driven orderings that document what the
// scoring rules actually do.

#include <algorithm>
#include <chrono>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "../../src/daemon/services.h"

namespace dafeng {
namespace {

bool IsPermutationOfFirstN(const std::vector<int32_t>& order, int n) {
  if (static_cast<int>(order.size()) != n) return false;
  std::set<int32_t> seen(order.begin(), order.end());
  if (static_cast<int>(seen.size()) != n) return false;
  for (auto v : order) {
    if (v < 0 || v >= n) return false;
  }
  return true;
}

TEST(DeterministicRerankerTest, IsReadyImmediately) {
  auto svc = MakeDeterministicRerankService();
  ASSERT_NE(svc, nullptr);
  EXPECT_TRUE(svc->IsReady());
}

TEST(DeterministicRerankerTest, EmptyCandidatesIsEmpty) {
  auto svc = MakeDeterministicRerankService();
  RerankRequest req;
  req.candidates = {};
  auto resp = svc->Rerank(req);
  EXPECT_TRUE(resp.reordered_indices.empty());
  EXPECT_TRUE(resp.scores.empty());
}

TEST(DeterministicRerankerTest, OutputIsValidPermutation) {
  auto svc = MakeDeterministicRerankService();
  RerankRequest req;
  req.context_before = "今天";
  req.candidates = {"感", "工", "一", "国", "天", "气"};
  auto resp = svc->Rerank(req);
  EXPECT_TRUE(IsPermutationOfFirstN(resp.reordered_indices,
                                     static_cast<int>(req.candidates.size())));
  EXPECT_EQ(resp.scores.size(), req.candidates.size());
}

TEST(DeterministicRerankerTest, DeterministicAcrossCalls) {
  auto svc = MakeDeterministicRerankService();
  RerankRequest req;
  req.context_before = "我";
  req.candidates = {"们", "的", "是", "有", "国", "感"};
  auto first = svc->Rerank(req);
  for (int i = 0; i < 50; ++i) {
    auto subsequent = svc->Rerank(req);
    EXPECT_EQ(subsequent.reordered_indices, first.reordered_indices)
        << "iteration " << i;
    EXPECT_EQ(subsequent.scores, first.scores);
  }
}

TEST(DeterministicRerankerTest, ContextSensitiveOrdering) {
  auto svc = MakeDeterministicRerankService();
  RerankRequest a;
  a.context_before = "今";
  a.candidates = {"工", "天", "国", "些"};
  RerankRequest b = a;
  b.context_before = "一";  // expected favor: "个" / "些"
  auto ra = svc->Rerank(a);
  auto rb = svc->Rerank(b);
  // The two contexts pick different favored characters; the orderings
  // should differ. (If they happen to coincide on this particular set,
  // the test is uninformative — but kBigrams was tuned so they don't.)
  EXPECT_NE(ra.reordered_indices, rb.reordered_indices);
}

TEST(DeterministicRerankerTest, BigramFavorWins) {
  // With context ending in "今", the bigram "今天" should pull "天" toward
  // the top.
  auto svc = MakeDeterministicRerankService();
  RerankRequest req;
  req.context_before = "今";
  req.candidates = {"工", "国", "天", "些"};  // "天" is at index 2
  auto resp = svc->Rerank(req);
  ASSERT_EQ(resp.reordered_indices.size(), 4u);
  // "天" should be ranked first.
  EXPECT_EQ(resp.reordered_indices.front(), 2);
}

TEST(DeterministicRerankerTest, RepetitionAvoided) {
  // With context "国", a candidate starting with "国" should be penalized
  // versus a candidate that doesn't repeat the last char.
  auto svc = MakeDeterministicRerankService();
  RerankRequest req;
  req.context_before = "国";
  req.candidates = {"国", "家"};
  auto resp = svc->Rerank(req);
  // "家" (index 1) should rank above "国" (index 0).
  ASSERT_EQ(resp.reordered_indices.size(), 2u);
  EXPECT_EQ(resp.reordered_indices.front(), 1);
}

TEST(DeterministicRerankerTest, StableForTies) {
  // With no useful context, length is the dominant signal; equal-length
  // candidates with same hash bucket should retain relative order from
  // librime (stable sort).
  auto svc = MakeDeterministicRerankService();
  RerankRequest req;
  req.context_before = "";
  req.candidates = {"a", "b", "c", "d", "e", "f"};
  auto resp = svc->Rerank(req);
  // Different scores would imply the test data is bad; but the property
  // we care about is that re-calling produces the same answer.
  auto second = svc->Rerank(req);
  EXPECT_EQ(resp.reordered_indices, second.reordered_indices);
}

TEST(DeterministicRerankerTest, LatencyUnder1Ms) {
  auto svc = MakeDeterministicRerankService();
  RerankRequest req;
  req.context_before = "今天 ";
  req.candidates = std::vector<std::string>(50, "天气晴朗");
  // Warmup.
  for (int i = 0; i < 10; ++i) (void)svc->Rerank(req);

  uint64_t total_us = 0;
  const int N = 1000;
  for (int i = 0; i < N; ++i) {
    auto resp = svc->Rerank(req);
    total_us += resp.latency_us;
  }
  const auto avg_us = total_us / N;
  // 50 candidates @ <20us avg is generous on slow CI; budget is 1 ms.
  EXPECT_LT(avg_us, 1000u) << "avg=" << avg_us << "us";
}

TEST(DeterministicRerankerTest, ScoresAreSortedDescending) {
  auto svc = MakeDeterministicRerankService();
  RerankRequest req;
  req.context_before = "我";
  req.candidates = {"们", "国", "天", "气", "学", "时"};
  auto resp = svc->Rerank(req);
  ASSERT_EQ(resp.scores.size(), req.candidates.size());
  for (size_t i = 1; i < resp.scores.size(); ++i) {
    EXPECT_GE(resp.scores[i - 1], resp.scores[i])
        << "scores not descending at i=" << i;
  }
}

TEST(DeterministicRerankerTest, MLXStubReturnsNull) {
  // When MLX isn't built in (the default), the factory must return null
  // so the daemon can fall back without crashing.
  MLXRerankConfig cfg;
  cfg.model_path = "/nope";
  auto svc = MakeMLXRerankService(cfg);
  EXPECT_EQ(svc, nullptr);
}

}  // namespace
}  // namespace dafeng
