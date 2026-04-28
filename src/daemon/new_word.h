#pragma once

// New-word discovery from input history. Phase 2.4 ships a
// frequency-statistical pass: any 2- or 3-char sequence that the user has
// committed at least N times within the scanned window becomes a
// candidate. DESIGN §4.4 calls for an LLM "reasonableness" filter on top
// of this; that's Phase 2.4.D follow-up — wired through a hook here so
// the discovery layer doesn't depend on any model.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dafeng {

class IHistoryStore;

struct DiscoveredWord {
  std::string text;            // 2 or 3 UTF-8 characters
  // Concatenated code from the contributing single-char commits
  // (e.g. "xu" + "pw" = "xupw" for 弹窗). Empty string when the
  // discovered phrase came from a multi-char entry whose code can't be
  // attributed per-char (those don't help RIME's table-translator anyway).
  std::string code;
  uint32_t frequency = 0;
  uint64_t first_seen_us = 0;
  uint64_t last_seen_us = 0;
};

struct DiscoveryConfig {
  uint32_t min_frequency = 3;        // appearance threshold
  uint64_t scan_max_entries = 5000;  // cap so runs are bounded
  bool include_bigrams = true;
  bool include_trigrams = true;

  // For char-by-char input methods (wubi commits one char per event),
  // chain consecutive commits into a phrase if the gap between them is
  // <= this threshold. 0 disables chaining (per-entry discovery only).
  // 2 seconds is comfortable for sustained typing without crossing
  // a true sentence boundary.
  uint64_t chain_max_gap_us = 2'000'000;
};

class INewWordDiscovery {
 public:
  virtual ~INewWordDiscovery() = default;
  virtual std::vector<DiscoveredWord> Discover(IHistoryStore& history,
                                                const DiscoveryConfig& cfg) = 0;
};

std::unique_ptr<INewWordDiscovery> MakeFrequencyNewWordDiscovery();

}  // namespace dafeng
