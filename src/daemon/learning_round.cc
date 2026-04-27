#include "learning_round.h"

#include <chrono>

#include "dafeng/logging.h"
#include "git_sync.h"
#include "history_store.h"
#include "new_word.h"
#include "ngram.h"
#include "user_dict.h"

namespace dafeng {

LearningRoundResult RunLearningRound(IHistoryStore& history,
                                       INgramTable& ngram,
                                       INewWordDiscovery& discovery,
                                       IUserDictGenerator& user_dict,
                                       IGitSync* git_sync,
                                       const LearningRoundConfig& cfg) {
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();
  LearningRoundResult result;

  result.entries_scanned = history.Count();

  // Step 1: bigrams.
  result.bigram_increments =
      UpdateBigramsFromHistory(history, ngram, cfg.ngram_scan_max_entries);

  // Step 2: new-word discovery.
  DiscoveryConfig disc_cfg;
  disc_cfg.min_frequency = cfg.min_new_word_freq;
  disc_cfg.scan_max_entries = cfg.scan_max_entries;
  disc_cfg.include_bigrams = cfg.include_bigrams;
  disc_cfg.include_trigrams = cfg.include_trigrams;
  auto words = discovery.Discover(history, disc_cfg);
  result.words_discovered = words.size();

  // Userdata sub-directory holds everything we sync.
  const auto userdata = cfg.data_dir / "userdata";
  std::error_code ec;
  std::filesystem::create_directories(userdata, ec);
  if (ec) {
    DAFENG_LOG_WARN("learning_round: mkdir userdata failed: %s",
                     ec.message().c_str());
  }

  // Step 3: write user_dict + ngram. Each is best-effort.
  result.wrote_user_dict =
      user_dict.Generate(words, userdata / "learned_words.yaml");
  result.wrote_ngram = ngram.Save(userdata / "personal_ngram.bin");

  // Step 4: redeploy sentinel.
  result.requested_redeploy = RequestRimeRedeploy(cfg.data_dir);

  // Step 5: git commit (+ push if both opt-ins are set).
  if (cfg.commit_to_git && git_sync != nullptr) {
    if (git_sync->Init() &&
        git_sync->CommitAll("dafeng-daemon: learning round")) {
      result.committed = true;
      if (cfg.push_to_remote) {
        result.pushed = git_sync->Push();
      }
    }
  }

  result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      clock::now() - t0);
  result.ok = result.wrote_user_dict && result.wrote_ngram;

  DAFENG_LOG_INFO(
      "learning_round: scanned=%llu bigrams+=%llu words=%llu commit=%d "
      "push=%d elapsed=%lldms",
      static_cast<unsigned long long>(result.entries_scanned),
      static_cast<unsigned long long>(result.bigram_increments),
      static_cast<unsigned long long>(result.words_discovered),
      result.committed ? 1 : 0, result.pushed ? 1 : 0,
      static_cast<long long>(result.elapsed.count()));
  return result;
}

}  // namespace dafeng
