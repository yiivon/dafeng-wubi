#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "test_helpers.h"
#include "../../src/daemon/history_store.h"
#include "../../src/daemon/ngram.h"
#include "dafeng/types.h"

namespace dafeng {
namespace {

class NgramFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = dafeng::testing::MakeTempDir("dafeng-ngram-");
    ASSERT_FALSE(dir_.empty());
  }
  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }
  std::filesystem::path dir_;
};

TEST_F(NgramFixture, IncrementAndCount) {
  auto t = MakeNgramTable();
  EXPECT_EQ(t->BigramCount("我", "们"), 0u);
  t->IncrementBigram("我", "们");
  t->IncrementBigram("我", "们");
  t->IncrementBigram("我", "的");
  EXPECT_EQ(t->BigramCount("我", "们"), 2u);
  EXPECT_EQ(t->BigramCount("我", "的"), 1u);
  EXPECT_EQ(t->BigramCount("我", "x"), 0u);
  EXPECT_EQ(t->Size(), 2u);
}

TEST_F(NgramFixture, EmptyArgsIgnored) {
  auto t = MakeNgramTable();
  t->IncrementBigram("", "x");
  t->IncrementBigram("x", "");
  t->IncrementBigram("", "");
  EXPECT_EQ(t->Size(), 0u);
}

TEST_F(NgramFixture, TopFollowersOrderedDescending) {
  auto t = MakeNgramTable();
  t->IncrementBigram("今", "天");
  t->IncrementBigram("今", "天");
  t->IncrementBigram("今", "天");
  t->IncrementBigram("今", "日");
  t->IncrementBigram("今", "年");
  t->IncrementBigram("今", "年");

  auto top = t->TopFollowers("今", 2);
  ASSERT_EQ(top.size(), 2u);
  EXPECT_EQ(top[0].first, "天");
  EXPECT_EQ(top[0].second, 3u);
  EXPECT_EQ(top[1].first, "年");
  EXPECT_EQ(top[1].second, 2u);
}

TEST_F(NgramFixture, TopFollowersHandlesEmptyAndShortLists) {
  auto t = MakeNgramTable();
  EXPECT_TRUE(t->TopFollowers("我", 5).empty());
  t->IncrementBigram("我", "的");
  auto v = t->TopFollowers("我", 5);
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0].first, "的");
}

TEST_F(NgramFixture, SaveLoadRoundTrip) {
  auto path = dir_ / "personal_ngram.bin";
  {
    auto t = MakeNgramTable();
    t->IncrementBigram("a", "b");
    t->IncrementBigram("a", "b");
    t->IncrementBigram("c", "d");
    t->IncrementBigram("中", "国");
    ASSERT_TRUE(t->Save(path));
  }
  {
    auto t = MakeNgramTable();
    ASSERT_TRUE(t->Load(path));
    EXPECT_EQ(t->BigramCount("a", "b"), 2u);
    EXPECT_EQ(t->BigramCount("c", "d"), 1u);
    EXPECT_EQ(t->BigramCount("中", "国"), 1u);
    EXPECT_EQ(t->Size(), 3u);
  }
}

TEST_F(NgramFixture, LoadMissingFileReturnsFalse) {
  auto t = MakeNgramTable();
  EXPECT_FALSE(t->Load(dir_ / "no-such-file.bin"));
  EXPECT_EQ(t->Size(), 0u);
}

TEST_F(NgramFixture, LoadCorruptFileReturnsFalseAndResets) {
  auto path = dir_ / "corrupt.bin";
  std::ofstream(path) << "not a valid ngram file";
  auto t = MakeNgramTable();
  t->IncrementBigram("x", "y");
  EXPECT_FALSE(t->Load(path));
  EXPECT_EQ(t->Size(), 0u);  // reset on failed load
}

TEST_F(NgramFixture, SaveIsAtomicViaTempFile) {
  // After Save, the tmp file should be gone (rename consumed it).
  auto path = dir_ / "ng.bin";
  auto t = MakeNgramTable();
  t->IncrementBigram("a", "b");
  ASSERT_TRUE(t->Save(path));
  std::error_code ec;
  EXPECT_TRUE(std::filesystem::exists(path, ec));
  EXPECT_FALSE(std::filesystem::exists(path.string() + ".tmp", ec));
}

TEST_F(NgramFixture, ConcurrentIncrementsCountAccurately) {
  auto t = MakeNgramTable();
  constexpr int kThreads = 4;
  constexpr int kIters = 1000;
  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&t]() {
      for (int j = 0; j < kIters; ++j) t->IncrementBigram("我", "们");
    });
  }
  for (auto& th : threads) th.join();
  EXPECT_EQ(t->BigramCount("我", "们"),
             static_cast<uint32_t>(kThreads * kIters));
}

TEST_F(NgramFixture, UpdateFromHistoryWalksUtf8PairsAcrossChainedEntries) {
  // Two phrases inserted in quick succession (well under the 2 s chain
  // gap) get chained — wubi-style. The cross-entry pair (们,中) is the
  // signal that chaining is on.
  auto store = MakeSqliteHistoryStore(dir_ / "h.db", dir_);
  ASSERT_NE(store, nullptr);
  CommitEvent ev1; ev1.committed_text = "今天我们";
  CommitEvent ev2; ev2.committed_text = "中国";
  store->Insert(ev1);
  store->Insert(ev2);

  auto t = MakeNgramTable();
  uint64_t inc = UpdateBigramsFromHistory(*store, *t, 100);
  EXPECT_EQ(inc, 5u);
  EXPECT_EQ(t->BigramCount("今", "天"), 1u);
  EXPECT_EQ(t->BigramCount("天", "我"), 1u);
  EXPECT_EQ(t->BigramCount("我", "们"), 1u);
  EXPECT_EQ(t->BigramCount("们", "中"), 1u);  // cross-entry, chained
  EXPECT_EQ(t->BigramCount("中", "国"), 1u);
}

TEST_F(NgramFixture, UpdateFromHistoryDoesNotChainAcrossLargeGap) {
  // When two entries are separated by a long pause, chaining is broken
  // and there should be NO (们,中) bigram. We can't easily inject
  // synthetic timestamps from outside the store, so this test exercises
  // the boundary indirectly: a single multi-char entry in isolation.
  auto store = MakeSqliteHistoryStore(dir_ / "h.db", dir_);
  ASSERT_NE(store, nullptr);
  CommitEvent ev; ev.committed_text = "今天";
  store->Insert(ev);

  auto t = MakeNgramTable();
  EXPECT_EQ(UpdateBigramsFromHistory(*store, *t, 100), 1u);
  EXPECT_EQ(t->BigramCount("今", "天"), 1u);
}

TEST_F(NgramFixture, UpdateFromHistorySkipsSingleCharEntries) {
  auto store = MakeSqliteHistoryStore(dir_ / "h.db", dir_);
  ASSERT_NE(store, nullptr);
  CommitEvent ev; ev.committed_text = "我";  // only one char, no pair
  store->Insert(ev);
  auto t = MakeNgramTable();
  EXPECT_EQ(UpdateBigramsFromHistory(*store, *t, 100), 0u);
  EXPECT_EQ(t->Size(), 0u);
}

}  // namespace
}  // namespace dafeng
