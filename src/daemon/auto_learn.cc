#include "auto_learn.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

#include "dafeng/logging.h"
#include "git_sync.h"
#include "history_store.h"
#include "learning_round.h"
#include "new_word.h"
#include "ngram.h"
#include "user_dict.h"

namespace dafeng {

namespace {

// Latest commit's wallclock timestamp from history.db. nullopt if empty.
std::optional<std::chrono::system_clock::time_point>
LatestCommitTimestamp(IHistoryStore& history) {
  auto recent = history.RecentEntries(/*limit=*/1);
  if (recent.empty()) return std::nullopt;
  return std::chrono::system_clock::time_point(
      std::chrono::microseconds(recent.front().ts_us));
}

// Try a list of canonical Squirrel install locations. First existing
// executable wins. Empty string = none found.
std::string FindRimeDeployer() {
  const char* candidates[] = {
      "/Library/Input Methods/Squirrel.app/Contents/MacOS/rime_deployer",
      "/Applications/Squirrel.app/Contents/MacOS/rime_deployer",
  };
  for (const char* c : candidates) {
    if (std::filesystem::exists(c)) return c;
  }
  return {};
}

// sh-quote: wraps in single quotes, escapes embedded single quotes.
// std::system() runs through `sh -c`, so this matches POSIX rules.
std::string ShellQuote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out.push_back(c);
  }
  out += "'";
  return out;
}

}  // namespace

AutoLearnRunner::AutoLearnRunner(std::shared_ptr<IHistoryStore> history,
                                 Config cfg, RedeployFn redeploy,
                                 NotifyFn notify)
    : cfg_(std::move(cfg)),
      history_(std::move(history)),
      redeploy_(std::move(redeploy)),
      notify_(std::move(notify)) {
  if (!redeploy_) redeploy_ = DefaultRimeRedeploy();
  if (!notify_) notify_ = DefaultLearnNotify();
}

AutoLearnRunner::~AutoLearnRunner() { Stop(); }

void AutoLearnRunner::Start() {
  std::lock_guard<std::mutex> lock(mu_);
  if (started_) return;
  // Seed last_round_at_ to "now" so we wait at least one
  // min_round_interval before firing for the first time. Avoids a
  // noisy auto-learn round immediately on daemon boot.
  last_round_at_ = std::chrono::steady_clock::now();
  if (history_) last_round_entry_count_ = history_->Count();
  stop_ = false;
  started_ = true;
  thread_ = std::thread([this] { Loop(); });
}

void AutoLearnRunner::Stop() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!started_) return;
    stop_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  started_ = false;
}

void AutoLearnRunner::TickForTesting() {
  // Tests don't Start() the runner; only one thread (the test) ever
  // calls this. Safe to mutate last_round_* without locking.
  if (!ShouldRunLocked()) return;
  RunOneRoundLocked();
}

void AutoLearnRunner::Loop() {
  while (true) {
    {
      // Wait for poll_interval OR a stop signal.
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait_for(lock, cfg_.poll_interval,
                   [this] { return stop_.load(); });
      if (stop_) return;
    }
    // last_round_* are only mutated by this thread, so reading +
    // writing them outside the lock is safe. We never race ourselves.
    if (!ShouldRunLocked()) continue;
    RunOneRoundLocked();
  }
}

bool AutoLearnRunner::ShouldRunLocked() {
  if (!history_) return false;

  // 1. Must have new entries since last round, otherwise nothing to learn.
  uint64_t now_count = history_->Count();
  if (now_count <= last_round_entry_count_) return false;

  // 2. Min interval between rounds. Floor at 0 so tests with
  // min_round_interval=0 always pass this gate.
  if (cfg_.min_round_interval.count() > 0) {
    auto since = std::chrono::steady_clock::now() - last_round_at_;
    if (since < cfg_.min_round_interval) return false;
  }

  // 3. User must be idle: latest commit's wall-clock age >= threshold.
  // Closest signal we have to "user is busy"; the IME doesn't push us
  // a separate keystroke event.
  if (cfg_.idle_threshold.count() > 0) {
    auto latest = LatestCommitTimestamp(*history_);
    if (latest.has_value()) {
      auto wall_now = std::chrono::system_clock::now();
      auto idle = wall_now - *latest;
      if (idle < cfg_.idle_threshold) return false;
    }
  }
  return true;
}

void AutoLearnRunner::RunOneRoundLocked() {
  // Build short-lived helpers per round. All cheap.
  auto ngram = MakeNgramTable();
  auto disc = MakeFrequencyNewWordDiscovery();
  auto gen = MakeYamlUserDictGenerator();
  GitSyncConfig gcfg;
  gcfg.repo_path = (cfg_.data_dir / "userdata").string();
  auto git = MakeGitSync(gcfg);  // nullptr in stub builds; LearningRound copes.

  LearningRoundConfig lcfg;
  lcfg.data_dir = cfg_.data_dir;
  lcfg.rime_user_dir = cfg_.rime_user_dir;
  lcfg.wubi_dict_path = cfg_.wubi_dict_path;
  lcfg.min_new_word_freq = cfg_.min_new_word_freq;
  lcfg.commit_to_git = cfg_.commit_to_git;
  lcfg.push_to_remote = cfg_.push_to_remote;

  LearningRoundResult result;
  try {
    result = RunLearningRound(*history_, *ngram, *disc, *gen, git.get(), lcfg);
  } catch (...) {
    // Defensive: a learning round must never bring the daemon down.
    DAFENG_LOG_WARN("auto-learn: round threw; skipping this tick.");
    last_round_at_ = std::chrono::steady_clock::now();
    return;
  }

  total_runs_.fetch_add(1);
  last_round_at_ = std::chrono::steady_clock::now();
  if (history_) last_round_entry_count_ = history_->Count();

  if (!result.ok) {
    DAFENG_LOG_WARN("auto-learn: round did not complete cleanly.");
    return;
  }
  DAFENG_LOG_INFO(
      "auto-learn: scanned=%llu bigrams=%llu words=%llu wrote_dict=%d "
      "redeploy_flagged=%d",
      static_cast<unsigned long long>(result.entries_scanned),
      static_cast<unsigned long long>(result.bigram_increments),
      static_cast<unsigned long long>(result.words_discovered),
      result.wrote_user_dict ? 1 : 0,
      result.requested_redeploy ? 1 : 0);

  if (result.words_discovered == 0 || !result.wrote_user_dict) {
    return;  // nothing to push to RIME this tick.
  }
  total_words_learned_.fetch_add(result.words_discovered);

  bool deployed = false;
  if (redeploy_) deployed = redeploy_(cfg_.rime_user_dir);
  if (deployed && notify_) notify_(result.words_discovered);
}

AutoLearnRunner::RedeployFn DefaultRimeRedeploy() {
  return [](const std::filesystem::path& rime_dir) -> bool {
    const std::string deployer = FindRimeDeployer();
    if (deployer.empty()) {
      DAFENG_LOG_WARN(
          "auto-learn: rime_deployer not found in standard locations; "
          "the user's RIME runtime won't reload until they click "
          "'重新部署' manually.");
      return false;
    }
    // Suppress deployer's own stdout/stderr so the daemon log stays clean.
    const std::string cmd = ShellQuote(deployer) + " --build " +
                             ShellQuote(rime_dir.string()) +
                             " >/dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
      DAFENG_LOG_WARN("auto-learn: rime_deployer exit=%d", rc);
      return false;
    }
    DAFENG_LOG_INFO("auto-learn: rebuilt %s/build/*.bin",
                    rime_dir.string().c_str());
    return true;
  };
}

AutoLearnRunner::NotifyFn DefaultLearnNotify() {
  return [](uint64_t words) {
    if (words == 0) return;
    // osascript will silently fail in headless contexts (Scheduled
    // Tasks, ssh sessions). Always best-effort.
    char cmd[512];
    std::snprintf(cmd, sizeof(cmd),
                  "osascript -e "
                  "'display notification \"已自动学到 %llu 个新词，下次切换"
                  "输入法时生效。\" with title \"大风五笔\"' "
                  ">/dev/null 2>&1",
                  static_cast<unsigned long long>(words));
    (void)std::system(cmd);
  };
}

}  // namespace dafeng
