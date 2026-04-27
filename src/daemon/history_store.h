#pragma once

// SQLite-backed history store for committed input events. Strictly local:
// the DB file never leaves the user's data directory, never enters Git,
// never makes a network call (CLAUDE.md privacy boundary).

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "dafeng/types.h"

namespace dafeng {

struct HistoryEntry {
  uint64_t id = 0;
  uint64_t ts_us = 0;       // microseconds since unix epoch
  std::string code;
  std::string committed_text;
  std::string context_before;
};

class IHistoryStore {
 public:
  virtual ~IHistoryStore() = default;

  // Persist a single event. Returns false on storage error (caller should
  // not retry — this is the cold path, drop and move on).
  virtual bool Insert(const CommitEvent& ev) = 0;

  // Read the most recent N entries, newest first.
  virtual std::vector<HistoryEntry> RecentEntries(uint64_t limit) = 0;

  // Total row count. Used by stats and pruning policy.
  virtual uint64_t Count() = 0;

  // Delete entries older than `older_than`. Returns rows deleted.
  // Default retention from DESIGN §9.1 is 90 days.
  virtual uint64_t Prune(std::chrono::seconds older_than) = 0;

  // Compact DB after large prunes. Returns false on storage error.
  virtual bool Vacuum() = 0;
};

// Construct a SQLite-backed store at `db_path`. Returns nullptr if the
// path is rejected by the privacy guard (escape attempt) or if the DB
// can't be opened. The caller owns the lifetime; on destruction the DB
// is flushed and closed.
//
// Privacy contract:
//   - db_path must be under `data_root`. We refuse to open otherwise.
//   - The DB is created with mode 0600 (owner read/write only).
//   - Pragmas: journal_mode=WAL, synchronous=NORMAL, foreign_keys=ON.
std::unique_ptr<IHistoryStore> MakeSqliteHistoryStore(
    const std::filesystem::path& db_path,
    const std::filesystem::path& data_root);

}  // namespace dafeng
