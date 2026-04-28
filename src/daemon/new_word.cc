#include "new_word.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

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
      std::string code;  // best (longest) attributable code seen
    };
    std::unordered_map<std::string, Counter> counts;

    // A token is one (char, source-code) pair coming from a SINGLE-char
    // commit. Multi-char commits don't contribute code-attributable
    // tokens — they're already-known phrases for which the user picked a
    // candidate by prefix code, so per-char code attribution is lossy.
    struct Token {
      std::string ch;
      std::string code;  // empty if from a multi-char entry
      uint64_t ts;
    };

    auto emit_token_ngrams = [&](const std::vector<Token>& toks) {
      // Emit bigrams + trigrams over a chained run of tokens. Code is
      // concatenated only when EVERY contributing token has a non-empty
      // code (i.e., all came from single-char commits).
      auto emit = [&](std::size_t i, std::size_t span) {
        std::string text;
        std::string code;
        bool code_ok = true;
        uint64_t ts0 = toks[i].ts;
        for (std::size_t k = 0; k < span; ++k) {
          text += toks[i + k].ch;
          if (toks[i + k].code.empty()) {
            code_ok = false;
          } else {
            code += toks[i + k].code;
          }
        }
        auto& c = counts[text];
        ++c.count;
        c.first_us = std::min(c.first_us, ts0);
        c.last_us = std::max(c.last_us, ts0);
        if (code_ok) {
          // Pick the longest seen code attribution; usually they'll all
          // be the same length, but be defensive.
          if (code.size() > c.code.size()) c.code = std::move(code);
        }
      };
      if (cfg.include_bigrams) {
        for (std::size_t i = 0; i + 1 < toks.size(); ++i) emit(i, 2);
      }
      if (cfg.include_trigrams) {
        for (std::size_t i = 0; i + 2 < toks.size(); ++i) emit(i, 3);
      }
    };

    auto entry_to_tokens = [](const HistoryEntry& e) {
      std::vector<Token> tks;
      const auto offs = CharOffsets(e.committed_text);
      // If the entry produced exactly one UTF-8 character, attribute the
      // commit code to that char. Multi-char entries get empty code per
      // token (we still emit text so chaining works, but code-attributable
      // bigrams won't be claimed across them).
      const bool single_char = offs.size() == 1;
      for (std::size_t i = 0; i < offs.size(); ++i) {
        const std::size_t end =
            (i + 1 < offs.size()) ? offs[i + 1] : e.committed_text.size();
        Token t;
        t.ch = e.committed_text.substr(offs[i], end - offs[i]);
        t.code = single_char ? e.code : std::string{};
        t.ts = e.ts_us;
        tks.push_back(std::move(t));
      }
      return tks;
    };

    if (cfg.chain_max_gap_us == 0) {
      for (const auto& e : entries) {
        auto tks = entry_to_tokens(e);
        emit_token_ngrams(tks);
      }
    } else {
      std::vector<HistoryEntry> chrono(entries.begin(), entries.end());
      std::sort(chrono.begin(), chrono.end(),
                 [](const auto& a, const auto& b) { return a.ts_us < b.ts_us; });

      std::vector<Token> run;
      uint64_t prev_ts = 0;
      for (const auto& e : chrono) {
        if (!run.empty() && (e.ts_us - prev_ts) > cfg.chain_max_gap_us) {
          emit_token_ngrams(run);
          run.clear();
        }
        for (auto& t : entry_to_tokens(e)) run.push_back(std::move(t));
        prev_ts = e.ts_us;
      }
      if (!run.empty()) emit_token_ngrams(run);
    }

    std::vector<DiscoveredWord> out;
    out.reserve(counts.size());
    for (const auto& kv : counts) {
      if (kv.second.count < cfg.min_frequency) continue;
      if (!LooksLikeRealCandidate(kv.first)) continue;
      DiscoveredWord w;
      w.text = kv.first;
      w.code = kv.second.code;
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
