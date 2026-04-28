// History store tests. Privacy-sensitive — these tests deliberately exercise
// the path-escape guard so that a regression that lets history.db be opened
// outside the user data directory fails the build.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

#include <gtest/gtest.h>

#include "test_helpers.h"
#include "../../src/daemon/history_store.h"
#include "../../src/daemon/services.h"
#include "dafeng/types.h"

namespace dafeng {
namespace {

class HistoryStoreFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    data_root_ = dafeng::testing::MakeTempDir("dafeng-history-");
    ASSERT_FALSE(data_root_.empty());
    db_path_ = data_root_ / "history.db";
  }
  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(data_root_, ec);
  }
  std::filesystem::path data_root_;
  std::filesystem::path db_path_;
};

CommitEvent MakeEvent(const std::string& code, const std::string& text,
                      const std::string& ctx) {
  CommitEvent ev;
  ev.code = code;
  ev.committed_text = text;
  ev.context_before = ctx;
  return ev;
}

TEST_F(HistoryStoreFixture, CreateAndInsertRoundTrip) {
  auto store = MakeSqliteHistoryStore(db_path_, data_root_);
  ASSERT_NE(store, nullptr);

  EXPECT_EQ(store->Count(), 0u);
  EXPECT_TRUE(store->Insert(MakeEvent("ggll", "感", "今天")));
  EXPECT_TRUE(store->Insert(MakeEvent("wo", "我", "感")));
  EXPECT_EQ(store->Count(), 2u);

  auto entries = store->RecentEntries(10);
  ASSERT_EQ(entries.size(), 2u);
  // Newest first.
  EXPECT_EQ(entries[0].committed_text, "我");
  EXPECT_EQ(entries[1].committed_text, "感");
  EXPECT_GT(entries[0].ts_us, 0u);
}

TEST_F(HistoryStoreFixture, PrivacyGuardRejectsEscape) {
  // /tmp/escape is *not* under the data root.
  auto bogus = std::filesystem::path("/tmp/dafeng-escape-test.db");
  auto store = MakeSqliteHistoryStore(bogus, data_root_);
  EXPECT_EQ(store, nullptr);
  std::error_code ec;
  EXPECT_FALSE(std::filesystem::exists(bogus, ec));
}

TEST_F(HistoryStoreFixture, PrivacyGuardAllowsExactRoot) {
  auto store = MakeSqliteHistoryStore(data_root_ / "history.db", data_root_);
  EXPECT_NE(store, nullptr);
}

TEST_F(HistoryStoreFixture, PrivacyGuardAllowsNestedPath) {
  auto store = MakeSqliteHistoryStore(data_root_ / "subdir" / "history.db",
                                        data_root_);
  EXPECT_NE(store, nullptr);
}

#if !defined(_WIN32)
// POSIX file-mode test. Windows has ACL inheritance instead of unix-mode
// bits and history_store.cc skips the chmod() there — see the
// matching `#if !defined(_WIN32)` in src/daemon/history_store.cc.
#include <sys/stat.h>
TEST_F(HistoryStoreFixture, FileMode0600AfterOpen) {
  auto store = MakeSqliteHistoryStore(db_path_, data_root_);
  ASSERT_NE(store, nullptr);
  store->Insert(MakeEvent("a", "b", ""));

  struct stat st;
  ASSERT_EQ(::stat(db_path_.string().c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0600);
}
#endif

TEST_F(HistoryStoreFixture, RecentEntriesRespectsLimit) {
  auto store = MakeSqliteHistoryStore(db_path_, data_root_);
  ASSERT_NE(store, nullptr);
  for (int i = 0; i < 50; ++i) {
    EXPECT_TRUE(store->Insert(MakeEvent("c" + std::to_string(i), "x", "")));
  }
  EXPECT_EQ(store->RecentEntries(10).size(), 10u);
  EXPECT_EQ(store->RecentEntries(100).size(), 50u);
}

TEST_F(HistoryStoreFixture, PruneDeletesOldEntries) {
  auto store = MakeSqliteHistoryStore(db_path_, data_root_);
  ASSERT_NE(store, nullptr);
  store->Insert(MakeEvent("a", "x", ""));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  store->Insert(MakeEvent("b", "y", ""));

  // Prune anything older than `now`. With seconds(0) the cutoff is now,
  // and both rows have ts_us < now, so both get deleted.
  auto deleted = store->Prune(std::chrono::seconds(0));
  EXPECT_EQ(deleted, 2u);
  EXPECT_EQ(store->Count(), 0u);
}

TEST_F(HistoryStoreFixture, VacuumWorks) {
  auto store = MakeSqliteHistoryStore(db_path_, data_root_);
  ASSERT_NE(store, nullptr);
  for (int i = 0; i < 100; ++i) {
    store->Insert(MakeEvent("a", "b", ""));
  }
  store->Prune(std::chrono::seconds(0));
  EXPECT_TRUE(store->Vacuum());
}

TEST_F(HistoryStoreFixture, ReopenPreservesData) {
  {
    auto store = MakeSqliteHistoryStore(db_path_, data_root_);
    ASSERT_NE(store, nullptr);
    store->Insert(MakeEvent("a", "x", "y"));
  }
  {
    auto store = MakeSqliteHistoryStore(db_path_, data_root_);
    ASSERT_NE(store, nullptr);
    EXPECT_EQ(store->Count(), 1u);
    auto entries = store->RecentEntries(1);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].committed_text, "x");
    EXPECT_EQ(entries[0].context_before, "y");
  }
}

TEST_F(HistoryStoreFixture, MakeSqliteCommitLoggerInsertsRows) {
  auto store_unique = MakeSqliteHistoryStore(db_path_, data_root_);
  ASSERT_NE(store_unique, nullptr);
  std::shared_ptr<IHistoryStore> store(std::move(store_unique));

  auto logger = MakeSqliteCommitLogger(store);
  ASSERT_NE(logger, nullptr);
  for (int i = 0; i < 5; ++i) {
    logger->Record(MakeEvent("c", "x", ""));
  }
  EXPECT_EQ(store->Count(), 5u);
}

TEST_F(HistoryStoreFixture, MakeSqliteCommitLoggerNullStoreReturnsNull) {
  auto logger = MakeSqliteCommitLogger(nullptr);
  EXPECT_EQ(logger, nullptr);
}

}  // namespace
}  // namespace dafeng
