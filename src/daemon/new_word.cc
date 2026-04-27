#include "new_word.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include "history_store.h"

namespace dafeng {

namespace {

// Same UTF-8 utility as ngram.cc — kept here as a private copy to avoid
// pulling ngram into new_word's surface.
std::size_t Utf8Len(const std::string& s, std::size_t pos) {
  if (pos >= s.size()) return 0;
  unsigned char c = static_cast<unsigned char>(s[pos]);
  std::size_t need;
  if ((c & 0x80) == 0) need = 1;
  else if ((c & 0xE0) == 0xC0) need = 2;
  else if ((c & 0xF0) == 0xE0) need = 3;
  else if ((c & 0xF8) == 0xF0) need = 4;
  else return 0;
  if (pos + need > s.size()) return 0;
  for (std::size_t i = 1; i < need; ++i) {
    unsigned char cc = static_cast<unsigned char>(s[pos + i]);
    if ((cc & 0xC0) != 0x80) return 0;
  }
  return need;
}

// Walk a UTF-8 string and emit the start offset of each character.
std::vector<std::size_t> CharOffsets(const std::string& s) {
  std::vector<std::size_t> out;
  out.reserve(s.size() / 2);
  std::size_t i = 0;
  while (i < s.size()) {
    auto n = Utf8Len(s, i);
    if (n == 0) break;
    out.push_back(i);
    i += n;
  }
  return out;
}

// Reject candidates that look like noise: single-char repeats, pure-ASCII
// (those are usually English typed in fallback mode, not new Chinese
// words), all-digits, or whitespace inside.
bool LooksLikeRealCandidate(const std::string& text) {
  if (text.empty()) return false;
  // Whitespace?
  for (unsigned char c : text) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return false;
  }
  // ASCII-only? Skip — wubi candidates are Chinese.
  bool all_ascii = true;
  for (unsigned char c : text) {
    if (c >= 0x80) {
      all_ascii = false;
      break;
    }
  }
  if (all_ascii) return false;
  // Repeat of a single character? "嗯嗯" / "呵呵" are real but cheap signal;
  // we keep them at the edge but reject 3-char repeats like "嗯嗯嗯".
  auto offs = CharOffsets(text);
  if (offs.size() >= 3) {
    bool all_same = true;
    auto first = text.substr(0, offs[1]);
    for (std::size_t i = 1; i < offs.size(); ++i) {
      const std::size_t end =
          (i + 1 < offs.size()) ? offs[i + 1] : text.size();
      if (text.substr(offs[i], end - offs[i]) != first) {
        all_same = false;
        break;
      }
    }
    if (all_same) return false;
  }
  return true;
}

class FrequencyDiscovery final : public INewWordDiscovery {
 public:
  std::vector<DiscoveredWord> Discover(IHistoryStore& history,
                                        const DiscoveryConfig& cfg) override {
    auto entries = history.RecentEntries(cfg.scan_max_entries);

    struct Counter {
      uint32_t count = 0;
      uint64_t first_us = std::numeric_limits<uint64_t>::max();
      uint64_t last_us = 0;
    };
    std::unordered_map<std::string, Counter> counts;

    for (const auto& e : entries) {
      const auto offs = CharOffsets(e.committed_text);
      // Bigrams: chars [i .. i+1], substring [offs[i], offs[i+2])
      if (cfg.include_bigrams && offs.size() >= 2) {
        for (std::size_t i = 0; i + 1 < offs.size(); ++i) {
          const std::size_t end =
              (i + 2 < offs.size()) ? offs[i + 2] : e.committed_text.size();
          auto sub = e.committed_text.substr(offs[i], end - offs[i]);
          auto& c = counts[sub];
          ++c.count;
          c.first_us = std::min(c.first_us, e.ts_us);
          c.last_us = std::max(c.last_us, e.ts_us);
        }
      }
      // Trigrams: chars [i .. i+2], substring [offs[i], offs[i+3])
      if (cfg.include_trigrams && offs.size() >= 3) {
        for (std::size_t i = 0; i + 2 < offs.size(); ++i) {
          const std::size_t end =
              (i + 3 < offs.size()) ? offs[i + 3] : e.committed_text.size();
          auto sub = e.committed_text.substr(offs[i], end - offs[i]);
          auto& c = counts[sub];
          ++c.count;
          c.first_us = std::min(c.first_us, e.ts_us);
          c.last_us = std::max(c.last_us, e.ts_us);
        }
      }
    }

    std::vector<DiscoveredWord> out;
    out.reserve(counts.size());
    for (const auto& kv : counts) {
      if (kv.second.count < cfg.min_frequency) continue;
      if (!LooksLikeRealCandidate(kv.first)) continue;
      DiscoveredWord w;
      w.text = kv.first;
      w.frequency = kv.second.count;
      w.first_seen_us = kv.second.first_us;
      w.last_seen_us = kv.second.last_us;
      out.push_back(std::move(w));
    }
    // Highest-frequency first; deterministic tiebreak by text.
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
      if (a.frequency != b.frequency) return a.frequency > b.frequency;
      return a.text < b.text;
    });
    return out;
  }
};

}  // namespace

std::unique_ptr<INewWordDiscovery> MakeFrequencyNewWordDiscovery() {
  return std::make_unique<FrequencyDiscovery>();
}

}  // namespace dafeng
