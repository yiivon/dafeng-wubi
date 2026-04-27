#pragma once

// LearningRound stitches the Phase 2.4 pieces together:
//   1. update bigrams from recent history          (ngram)
//   2. discover candidate new words                (new_word)
//   3. write learned_words.yaml + ngram.bin        (user_dict)
//   4. flag a RIME redeploy                        (sentinel)
//   5. commit changes; optionally push             (git_sync)
//
// This is the cold-path orchestrator. It runs on demand (via
// `dafeng-cli learn`) and, in a future phase, on a schedule. It NEVER
// runs from the IME hot path. Each step is best-effort: failure of one
// step doesn't abort the others.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace dafeng {

class IHistoryStore;
class INgramTable;
class INewWordDiscovery;
class IUserDictGenerator;
class IGitSync;

struct LearningRoundConfig {
  std::filesystem::path data_dir;       // root for output files
  uint32_t min_new_word_freq = 3;       // see DiscoveryConfig
  uint64_t scan_max_entries = 5000;     // see DiscoveryConfig
  uint64_t ngram_scan_max_entries = 5000;
  bool include_bigrams = true;
  bool include_trigrams = true;
  bool commit_to_git = true;
  bool push_to_remote = false;          // CLAUDE.md privacy default
};

struct LearningRoundResult {
  bool ok = false;
  uint64_t entries_scanned = 0;
  uint64_t bigram_increments = 0;
  uint64_t words_discovered = 0;
  bool wrote_user_dict = false;
  bool wrote_ngram = false;
  bool requested_redeploy = false;
  bool committed = false;
  bool pushed = false;
  std::chrono::milliseconds elapsed{0};
};

LearningRoundResult RunLearningRound(IHistoryStore& history,
                                       INgramTable& ngram,
                                       INewWordDiscovery& discovery,
                                       IUserDictGenerator& user_dict,
                                       IGitSync* git_sync,
                                       const LearningRoundConfig& cfg);

}  // namespace dafeng
