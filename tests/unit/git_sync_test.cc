// Git sync tests. Push isn't tested directly (would need a remote); we
// cover Init / CommitAll / HasChanges and assert the push-disabled
// default never reaches out.

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "test_helpers.h"
#include "../../src/daemon/git_sync.h"

namespace dafeng {
namespace {

class GitSyncFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    repo_ = dafeng::testing::MakeTempDir("dafeng-gitsync-");
    ASSERT_FALSE(repo_.empty());
  }
  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(repo_, ec);
  }

  void WriteFile(const std::string& name, const std::string& content) {
    std::ofstream f(repo_ / name);
    f << content;
  }

  GitSyncConfig MakeConfig() {
    GitSyncConfig cfg;
    cfg.repo_path = repo_.string();
    cfg.author_name = "test";
    cfg.author_email = "test@dafeng.local";
    return cfg;
  }

  bool RepoHasCommits() {
    std::error_code ec;
    auto head = repo_ / ".git" / "HEAD";
    if (!std::filesystem::exists(head, ec)) return false;
    auto refs = repo_ / ".git" / "refs" / "heads";
    if (!std::filesystem::exists(refs, ec)) return false;
    for (auto& e : std::filesystem::directory_iterator(refs, ec)) {
      (void)e;
      return true;  // any branch ref means at least one commit
    }
    return false;
  }

  std::filesystem::path repo_;
};

TEST_F(GitSyncFixture, InitCreatesRepoIdempotent) {
  auto sync = MakeGitSync(MakeConfig());
  ASSERT_NE(sync, nullptr);  // requires DAFENG_ENABLE_GIT_SYNC=ON
  EXPECT_TRUE(sync->Init());
  EXPECT_TRUE(std::filesystem::exists(repo_ / ".git"));
  // Calling again is a no-op.
  EXPECT_TRUE(sync->Init());
}

TEST_F(GitSyncFixture, CommitAllCreatesCommitFromWorkingTree) {
  auto sync = MakeGitSync(MakeConfig());
  ASSERT_NE(sync, nullptr);
  EXPECT_TRUE(sync->Init());
  WriteFile("a.txt", "hello\n");
  WriteFile("b.txt", "world\n");
  EXPECT_TRUE(sync->HasChanges());
  EXPECT_TRUE(sync->CommitAll("initial"));
  EXPECT_TRUE(RepoHasCommits());
  EXPECT_FALSE(sync->HasChanges());
}

TEST_F(GitSyncFixture, MultipleCommitsAccumulate) {
  auto sync = MakeGitSync(MakeConfig());
  ASSERT_NE(sync, nullptr);
  EXPECT_TRUE(sync->Init());
  WriteFile("a.txt", "1");
  EXPECT_TRUE(sync->CommitAll("c1"));
  WriteFile("a.txt", "2");
  EXPECT_TRUE(sync->CommitAll("c2"));
  WriteFile("b.txt", "new");
  EXPECT_TRUE(sync->CommitAll("c3"));
  // Three commits exist in the branch's history; we assert via the
  // ref-log file size as a proxy — at least 3 lines.
  std::ifstream log(repo_ / ".git" / "logs" / "HEAD");
  std::string line;
  int n = 0;
  while (std::getline(log, line)) ++n;
  EXPECT_EQ(n, 3);
}

TEST_F(GitSyncFixture, CommitAllWithNoChangesIsSuccess) {
  auto sync = MakeGitSync(MakeConfig());
  ASSERT_NE(sync, nullptr);
  EXPECT_TRUE(sync->Init());
  WriteFile("a.txt", "x");
  EXPECT_TRUE(sync->CommitAll("c1"));
  // No changes between commits.
  EXPECT_FALSE(sync->HasChanges());
  // CommitAll should still succeed (treats no-op as success per contract).
  EXPECT_TRUE(sync->CommitAll("noop"));
}

TEST_F(GitSyncFixture, PushDisabledByDefaultIsNoop) {
  // push_enabled = false (default). Even with a bogus push_url, Push()
  // must return true and not actually try to reach the network.
  auto cfg = MakeConfig();
  cfg.push_url = "https://this-host-must-not-exist.dafeng.invalid/repo.git";
  cfg.push_enabled = false;  // explicit
  auto sync = MakeGitSync(cfg);
  ASSERT_NE(sync, nullptr);
  EXPECT_TRUE(sync->Init());
  WriteFile("a.txt", "x");
  EXPECT_TRUE(sync->CommitAll("c1"));
  // Push must not block on DNS or fail noisily.
  auto t0 = std::chrono::steady_clock::now();
  EXPECT_TRUE(sync->Push());
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - t0)
                     .count();
  // No-op push must be effectively instant.
  EXPECT_LT(elapsed, 100);
}

TEST_F(GitSyncFixture, PushEnabledWithoutUrlIsAlsoNoop) {
  auto cfg = MakeConfig();
  cfg.push_enabled = true;
  cfg.push_url = "";  // not configured
  auto sync = MakeGitSync(cfg);
  ASSERT_NE(sync, nullptr);
  EXPECT_TRUE(sync->Init());
  WriteFile("a.txt", "x");
  EXPECT_TRUE(sync->CommitAll("c1"));
  EXPECT_TRUE(sync->Push());  // no-op, returns true
}

TEST_F(GitSyncFixture, EmptyRepoPathReturnsNullFactory) {
  GitSyncConfig cfg;
  cfg.repo_path = "";
  auto sync = MakeGitSync(cfg);
  EXPECT_EQ(sync, nullptr);
}

TEST_F(GitSyncFixture, HasChangesFalseOnFreshRepo) {
  auto sync = MakeGitSync(MakeConfig());
  ASSERT_NE(sync, nullptr);
  EXPECT_TRUE(sync->Init());
  EXPECT_FALSE(sync->HasChanges());
}

}  // namespace
}  // namespace dafeng
