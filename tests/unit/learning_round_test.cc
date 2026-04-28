// End-to-end unit test for the learning round orchestrator. Asserts that
// each piece (history -> ngram + discovery -> yaml + ngram persistence ->
// redeploy sentinel -> git commit) actually fires and produces the expected
// artifacts on disk.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include <gtest/gtest.h>

#include "test_helpers.h"
#include "../../src/daemon/git_sync.h"
#include "../../src/daemon/history_store.h"
#include "../../src/daemon/learning_round.h"
#include "../../src/daemon/new_word.h"
#include "../../src/daemon/ngram.h"
#include "../../src/daemon/user_dict.h"
#include "dafeng/types.h"

namespace dafeng {
namespace {

class LearningRoundFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    data_dir_ = dafeng::testing::MakeTempDir("dafeng-learn-");
    ASSERT_FALSE(data_dir_.empty());
    std::filesystem::create_directories(data_dir_ / "userdata");
    history_ = MakeSqliteHistoryStore(data_dir_ / "history.db", data_dir_);
    ASSERT_NE(history_, nullptr);
  }
  void TearDown() override {
    history_.reset();
    std::error_code ec;
    std::filesystem::remove_all(data_dir_, ec);
  }

  void Insert(const std::string& text) {
    CommitEvent ev;
    ev.committed_text = text;
    history_->Insert(ev);
  }

  std::filesystem::path data_dir_;
  std::unique_ptr<IHistoryStore> history_;
};

TEST_F(LearningRoundFixture, EmptyHistoryStillWritesArtifacts) {
  auto ngram = MakeNgramTable();
  auto disc = MakeFrequencyNewWordDiscovery();
  auto gen = MakeYamlUserDictGenerator();

  LearningRoundConfig cfg;
  cfg.data_dir = data_dir_;
  cfg.commit_to_git = false;
  auto r = RunLearningRound(*history_, *ngram, *disc, *gen, nullptr, cfg);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.bigram_increments, 0u);
  EXPECT_EQ(r.words_discovered, 0u);
  EXPECT_TRUE(r.wrote_user_dict);
  EXPECT_TRUE(r.wrote_ngram);
  EXPECT_TRUE(r.requested_redeploy);

  std::error_code ec;
  EXPECT_TRUE(std::filesystem::exists(
      data_dir_ / "userdata" / "learned_words.yaml", ec));
  EXPECT_TRUE(std::filesystem::exists(
      data_dir_ / "userdata" / "personal_ngram.bin", ec));
  EXPECT_TRUE(
      std::filesystem::exists(data_dir_ / "redeploy_requested", ec));
}

TEST_F(LearningRoundFixture, EndToEndProducesUsefulDiscoveries) {
  Insert("今天我们吃饭");
  Insert("今天天气真好");
  Insert("我们今天去");
  Insert("今天我们去吃饭");

  auto ngram = MakeNgramTable();
  auto disc = MakeFrequencyNewWordDiscovery();
  auto gen = MakeYamlUserDictGenerator();

  LearningRoundConfig cfg;
  cfg.data_dir = data_dir_;
  cfg.min_new_word_freq = 2;
  cfg.commit_to_git = false;
  auto r = RunLearningRound(*history_, *ngram, *disc, *gen, nullptr, cfg);

  EXPECT_TRUE(r.ok);
  EXPECT_GT(r.bigram_increments, 0u);
  EXPECT_GT(r.words_discovered, 0u);

  // Verify the yaml mentions one of the high-frequency phrases.
  std::ifstream f(data_dir_ / "userdata" / "learned_words.yaml");
  std::ostringstream oss;
  oss << f.rdbuf();
  auto contents = oss.str();
  EXPECT_NE(contents.find("今天"), std::string::npos);
}

TEST_F(LearningRoundFixture, GitCommitFiresWhenEnabled) {
  Insert("一二三");

  auto ngram = MakeNgramTable();
  auto disc = MakeFrequencyNewWordDiscovery();
  auto gen = MakeYamlUserDictGenerator();

  GitSyncConfig gcfg;
  gcfg.repo_path = (data_dir_ / "userdata").string();
  auto git = MakeGitSync(gcfg);
  ASSERT_NE(git, nullptr);

  LearningRoundConfig cfg;
  cfg.data_dir = data_dir_;
  cfg.commit_to_git = true;
  cfg.push_to_remote = false;  // explicit
  auto r = RunLearningRound(*history_, *ngram, *disc, *gen, git.get(), cfg);

  EXPECT_TRUE(r.committed);
  EXPECT_FALSE(r.pushed);  // push opt-in
  std::error_code ec;
  EXPECT_TRUE(std::filesystem::exists(
      data_dir_ / "userdata" / ".git", ec));
}

TEST_F(LearningRoundFixture, PushNotAttemptedWhenDisabled) {
  Insert("一二三");

  auto ngram = MakeNgramTable();
  auto disc = MakeFrequencyNewWordDiscovery();
  auto gen = MakeYamlUserDictGenerator();

  GitSyncConfig gcfg;
  gcfg.repo_path = (data_dir_ / "userdata").string();
  // Even with a bogus URL, push must not fire because cfg.push_to_remote=false.
  gcfg.push_url = "https://this-must-not-be-reached.dafeng.invalid/r.git";
  auto git = MakeGitSync(gcfg);

  LearningRoundConfig cfg;
  cfg.data_dir = data_dir_;
  cfg.push_to_remote = false;
  auto t0 = std::chrono::steady_clock::now();
  auto r = RunLearningRound(*history_, *ngram, *disc, *gen, git.get(), cfg);
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - t0)
                     .count();
  EXPECT_FALSE(r.pushed);
  EXPECT_LT(elapsed, 1000) << "push must not block on DNS/network";
}

}  // namespace
}  // namespace dafeng
