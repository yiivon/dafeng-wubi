#pragma once

// Personal bigram counter. Mapped to file `userdata/personal_ngram.bin` —
// the file CAN be synced to the user's private dictionary repo because it
// holds only aggregate frequencies, no raw input. (history.db is what we
// keep strictly local — see CLAUDE.md.)
//
// "Char" here means a UTF-8 character (1-4 bytes). All keys are UTF-8.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dafeng {

class INgramTable {
 public:
  virtual ~INgramTable() = default;

  // Bump count(a -> b) by 1. Both args must be a single UTF-8 character.
  virtual void IncrementBigram(const std::string& a, const std::string& b) = 0;

  // Count of (a -> b). Zero if unseen.
  virtual uint32_t BigramCount(const std::string& a,
                                const std::string& b) const = 0;

  // Top-k followers of `a`, sorted by count desc. Empty if `a` has no
  // recorded follower.
  virtual std::vector<std::pair<std::string, uint32_t>> TopFollowers(
      const std::string& a, std::size_t k) const = 0;

  // Replace the on-disk file at `path` atomically (write to .tmp + rename).
  // Returns false on filesystem error.
  virtual bool Save(const std::filesystem::path& path) const = 0;

  // Replace in-memory state from `path`. Returns false if the file is
  // missing or malformed; in that case the table is reset to empty.
  virtual bool Load(const std::filesystem::path& path) = 0;

  // Total number of distinct (a, b) pairs.
  virtual std::size_t Size() const = 0;
};

std::unique_ptr<INgramTable> MakeNgramTable();

// Walk the last `lookback` worth of recent history entries and update
// `table` with one bigram per adjacent UTF-8 character pair within each
// committed_text. Returns the number of bigram increments applied.
class IHistoryStore;
uint64_t UpdateBigramsFromHistory(IHistoryStore& history, INgramTable& table,
                                   uint64_t max_entries);

}  // namespace dafeng
