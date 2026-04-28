#pragma once

// AutoLearnRunner — background thread that periodically runs the
// LearningRound and re-deploys RIME without the user having to lift a
// finger. Solves the bad UX where users had to remember to run
// `dafeng-cli learn` and then click "重新部署" in the menu bar after
// every batch of split-typed phrases.
//
// Lifecycle (when --auto-learn is on, the daemon's default):
//
//   every poll_interval (default 60s):
//     if (entries since last round > 0
//         AND time since last round >= min_round_interval (default 300s)
//         AND user has been idle for >= idle_threshold (default 30s)):
//       run LearningRound
//       if it discovered new words:
//         spawn rime_deployer to rebuild ~/Library/Rime/build/*.bin
//         emit a desktop notification ("学到 N 个新词")
//
// Idle is measured by the wall-clock distance between now() and the
// most recent commit_event in history.db — if the user is mid-typing
// the latest commit is recent, so we wait. This keeps the rime_deployer
// subprocess (~1s wallclock) from competing with the IME hot path.
//
// All knobs (intervals, freq threshold) are configurable so tests can
// inject sub-second values. Default values are documented inline.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace dafeng {

class IHistoryStore;

class AutoLearnRunner {
 public:
  struct Config {
    std::filesystem::path data_dir;        // root of dafeng userdata
    std::filesystem::path rime_user_dir;   // ~/Library/Rime
    std::filesystem::path wubi_dict_path;  // path to wubi86.dict.yaml

    std::chrono::seconds idle_threshold{30};      // user-quiet before learn
    std::chrono::seconds poll_interval{60};       // wakeup cadence
    std::chrono::seconds min_round_interval{300}; // floor on round frequency

    uint32_t min_new_word_freq = 2;
    bool commit_to_git = true;
    bool push_to_remote = false;             // CLAUDE.md privacy default
  };

  // Hook for spawning RIME's deployer. Default impl in
  // auto_learn.cc forks a `rime_deployer --build <rime_user_dir>`
  // subprocess. Tests inject a stub that just records the call.
  using RedeployFn = std::function<bool(const std::filesystem::path&)>;

  // Hook for telling the user about a successful round. Default impl
  // shells out to osascript to emit a macOS desktop notification.
  using NotifyFn = std::function<void(uint64_t /*words_learned*/)>;

  AutoLearnRunner(std::shared_ptr<IHistoryStore> history, Config cfg,
                  RedeployFn redeploy = {}, NotifyFn notify = {});
  ~AutoLearnRunner();

  AutoLearnRunner(const AutoLearnRunner&) = delete;
  AutoLearnRunner& operator=(const AutoLearnRunner&) = delete;

  // Spawn the watcher thread. Idempotent.
  void Start();
  // Signal the thread to exit and join. Idempotent.
  void Stop();

  // Run one evaluation pass synchronously on the calling thread.
  // Used in unit tests to drive the watcher without sleeping.
  void TickForTesting();

  // Telemetry for `dafeng-cli stats` / Inspector.
  uint64_t total_runs() const { return total_runs_.load(); }
  uint64_t total_words_learned() const { return total_words_learned_.load(); }

 private:
  void Loop();
  bool ShouldRunLocked();   // mu_ held
  void RunOneRoundLocked(); // mu_ held during eligibility, dropped during work

  Config cfg_;
  std::shared_ptr<IHistoryStore> history_;
  RedeployFn redeploy_;
  NotifyFn notify_;

  std::thread thread_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::atomic<bool> stop_{false};
  bool started_ = false;

  uint64_t last_round_entry_count_ = 0;
  std::chrono::steady_clock::time_point last_round_at_{};

  std::atomic<uint64_t> total_runs_{0};
  std::atomic<uint64_t> total_words_learned_{0};
};

// Default: spawn `rime_deployer --build <rime_user_dir>` from the
// canonical Squirrel install location. Returns false if the deployer
// can't be found or the subprocess returns non-zero.
AutoLearnRunner::RedeployFn DefaultRimeRedeploy();

// Default: macOS desktop notification via osascript. No-op on
// platforms without osascript.
AutoLearnRunner::NotifyFn DefaultLearnNotify();

}  // namespace dafeng
