#pragma once

// Generates `learned_words.yaml` — a human-readable dump of what the
// new-word discovery layer found. NOT directly a RIME schema or userdb
// file — those formats need the user's installation_id / wubi codes that
// the daemon doesn't compute. Phase 3 packaging will ship a converter
// (or an external tool) that materializes this into Squirrel's userdb.
//
// What the file IS suitable for:
//   - Inspection: human-readable list of discovered phrases.
//   - Sync: lives in `userdata/`, so it goes through the Git pipeline
//     and round-trips to other devices.
//   - Future tooling: a small Python script can read this and write the
//     binary RIME userdb when the user explicitly opts in.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "new_word.h"

namespace dafeng {

class IUserDictGenerator {
 public:
  virtual ~IUserDictGenerator() = default;

  // Write a YAML file to `out_path` containing the discovered words.
  // tmp+rename for atomicity. Returns false on filesystem error.
  virtual bool Generate(const std::vector<DiscoveredWord>& words,
                        const std::filesystem::path& out_path) = 0;
};

std::unique_ptr<IUserDictGenerator> MakeYamlUserDictGenerator();

// Touch a sentinel file at `data_dir/redeploy_requested` to flag a
// pending RIME redeploy. The macOS Squirrel daemon doesn't poll this;
// it's read by tools/rime_redeploy.sh which is the user's manual trigger.
// Returns false on filesystem error.
bool RequestRimeRedeploy(const std::filesystem::path& data_dir);

}  // namespace dafeng
