// Auto-learn runner tests. We drive the runner via TickForTesting() so
// the suite never sleeps for a real poll interval; redeploy + notify
// are stubbed so the host's RIME isn't touched.

#include "auto_learn.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "test_helpers.h"
#include "../../src/daemon/history_store.h"
#include "dafeng/types.h"

namespace dafeng {
namespace {

class AutoLearnFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    data_dir_ = dafeng::testing::MakeTempDir("dafeng-autolearn-");
    ASSERT_FALSE(data_dir_.empty());
    history_ = MakeSqliteHistoryStore(data_dir_ / "history.db", data_dir_);
    ASSERT_NE(history_, nullptr);
    rime_dir_ = data_dir_ / "rime";
    std::filesystem::create_directories(rime_dir_);
  }
  void TearDown() override {
    history_.reset();
    std::error_code ec;
    std::filesystem::remove_all(data_dir_, ec);
  }

  // Build a runner pre-wired with stubs that record their calls.
  std::unique_ptr<AutoLearnRunner> Make(AutoLearnRunner::Config cfg) {
    cfg.data_dir = data_dir_;
    cfg.rime_user_dir = rime_dir_;
    return std::make_unique<AutoLearnRunner>(
        history_,
        cfg,
        [this](const std::filesystem::path&) {
          redeploy_calls_.fetch_add(1);
          return true;
        },
        [this](uint64_t words) {
          notify_calls_.fetch_add(1);
          last_notify_words_.store(words);
        });
  }

  // IHistoryStore::Insert always stamps with now() — there's no
  // public test-only override. So we exercise idle/freshness logic
  // by varying the runner's idle_threshold relative to "now".
  void InsertCommitNow(const std::string& code, const std::string& text) {
    CommitEvent e;
    e.code = code;
    e.committed_text = text;
    history_->Insert(e);
  }

  std::filesystem::path data_dir_;
  std::filesystem::path rime_dir_;
  std::shared_ptr<IHistoryStore> history_;
  std::atomic<uint64_t> redeploy_calls_{0};
  std::atomic<uint64_t> notify_calls_{0};
  std::atomic<uint64_t> last_notify_words_{0};
};

TEST_F(AutoLearnFixture, NoEntriesMeansNoRound) {
  AutoLearnRunner::Config cfg;
  cfg.idle_threshold = std::chrono::seconds(0);
  cfg.min_round_interval = std::chrono::seconds(0);
  auto runner = Make(cfg);
  runner->TickForTesting();
  EXPECT_EQ(runner->total_runs(), 0u);
  EXPECT_EQ(redeploy_calls_.load(), 0u);
}

TEST_F(AutoLearnFixture, IdleThresholdBlocksWhenUserIsBusy) {
  // Commit just inserted (now()) + 30 s threshold ⇒ "user is busy".
  InsertCommitNow("a", "啊");
  AutoLearnRunner::Config cfg;
  cfg.idle_threshold = std::chrono::seconds(30);
  cfg.min_round_interval = std::chrono::seconds(0);
  auto runner = Make(cfg);
  runner->TickForTesting();
  EXPECT_EQ(runner->total_runs(), 0u);
}

TEST_F(AutoLearnFixture, RoundFiresWhenIdleGateDisabled) {
  // idle_threshold=0 ⇒ idle gate off; round fires on a fresh commit.
  // min_new_word_freq=99 ⇒ no words discovered; exercises the
  // round-completed-but-nothing-to-deploy path.
  InsertCommitNow("a", "啊");
  AutoLearnRunner::Config cfg;
  cfg.idle_threshold = std::chrono::seconds(0);
  cfg.min_round_interval = std::chrono::seconds(0);
  cfg.min_new_word_freq = 99;
  auto runner = Make(cfg);
  runner->TickForTesting();
  EXPECT_EQ(runner->total_runs(), 1u);
  EXPECT_EQ(redeploy_calls_.load(), 0u);
  EXPECT_EQ(notify_calls_.load(), 0u);
}

TEST_F(AutoLearnFixture, MinIntervalBlocksRapidSecondTick) {
  InsertCommitNow("a", "啊");
  AutoLearnRunner::Config cfg;
  cfg.idle_threshold = std::chrono::seconds(0);
  cfg.min_round_interval = std::chrono::seconds(3600);  // 1h floor
  cfg.min_new_word_freq = 99;
  auto runner = Make(cfg);
  runner->TickForTesting();        // first tick fires
  // Second tick within 1h should be gated by min_round_interval.
  // We add a fresh entry so the "new entries" check passes — only the
  // interval gate should stop us.
  InsertCommitNow("b", "吧");
  runner->TickForTesting();
  EXPECT_EQ(runner->total_runs(), 1u);  // still 1, second was gated
}

TEST_F(AutoLearnFixture, NoNewEntriesGatesRoundEvenIfEverythingElseAllows) {
  InsertCommitNow("a", "啊");
  AutoLearnRunner::Config cfg;
  cfg.idle_threshold = std::chrono::seconds(0);
  cfg.min_round_interval = std::chrono::seconds(0);
  cfg.min_new_word_freq = 99;
  auto runner = Make(cfg);
  runner->TickForTesting();
  EXPECT_EQ(runner->total_runs(), 1u);
  // No new commits inserted; second tick should no-op.
  runner->TickForTesting();
  EXPECT_EQ(runner->total_runs(), 1u);
}

TEST_F(AutoLearnFixture, StartStopIsIdempotentAndSafe) {
  AutoLearnRunner::Config cfg;
  cfg.poll_interval = std::chrono::seconds(60);
  cfg.idle_threshold = std::chrono::seconds(0);
  cfg.min_round_interval = std::chrono::seconds(0);
  auto runner = Make(cfg);
  runner->Start();
  runner->Start();   // idempotent
  // Sleep briefly so the loop thread enters its wait.
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  runner->Stop();
  runner->Stop();    // idempotent
}

}  // namespace
}  // namespace dafeng
