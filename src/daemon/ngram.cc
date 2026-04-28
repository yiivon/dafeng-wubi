#include "ngram.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dafeng/logging.h"
#include "history_store.h"

namespace dafeng {

namespace {

// "DAFNGRAM" + 4 byte version. Current version 1 = simple bigram-only.
constexpr char kMagic[8] = {'D', 'A', 'F', 'N', 'G', 'R', 'A', 'M'};
constexpr uint32_t kVersion = 1;

// Read and write little-endian uint32 / uint16 (Mac and Win are LE on the
// supported targets — if we ever go BE this needs to byte-swap).
void Write32(std::ostream& os, uint32_t v) {
  uint8_t b[4] = {static_cast<uint8_t>(v & 0xFF),
                   static_cast<uint8_t>((v >> 8) & 0xFF),
                   static_cast<uint8_t>((v >> 16) & 0xFF),
                   static_cast<uint8_t>((v >> 24) & 0xFF)};
  os.write(reinterpret_cast<const char*>(b), 4);
}

void Write16(std::ostream& os, uint16_t v) {
  uint8_t b[2] = {static_cast<uint8_t>(v & 0xFF),
                   static_cast<uint8_t>((v >> 8) & 0xFF)};
  os.write(reinterpret_cast<const char*>(b), 2);
}

bool Read32(std::istream& is, uint32_t* out) {
  uint8_t b[4];
  if (!is.read(reinterpret_cast<char*>(b), 4)) return false;
  *out = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
         (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
  return true;
}

bool Read16(std::istream& is, uint16_t* out) {
  uint8_t b[2];
  if (!is.read(reinterpret_cast<char*>(b), 2)) return false;
  *out = static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8);
  return true;
}

class NgramTable final : public INgramTable {
 public:
  void IncrementBigram(const std::string& a, const std::string& b) override {
    if (a.empty() || b.empty()) return;
    std::lock_guard<std::mutex> lock(mu_);
    ++map_[a][b];
  }

  uint32_t BigramCount(const std::string& a,
                        const std::string& b) const override {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = map_.find(a);
    if (it == map_.end()) return 0;
    auto jt = it->second.find(b);
    return jt == it->second.end() ? 0 : jt->second;
  }

  std::vector<std::pair<std::string, uint32_t>> TopFollowers(
      const std::string& a, std::size_t k) const override {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = map_.find(a);
    if (it == map_.end()) return {};
    std::vector<std::pair<std::string, uint32_t>> entries(it->second.begin(),
                                                           it->second.end());
    if (entries.size() > k) {
      std::partial_sort(
          entries.begin(), entries.begin() + k, entries.end(),
          [](const auto& x, const auto& y) { return x.second > y.second; });
      entries.resize(k);
    } else {
      std::sort(entries.begin(), entries.end(),
                [](const auto& x, const auto& y) {
                  return x.second > y.second;
                });
    }
    return entries;
  }

  bool Save(const std::filesystem::path& path) const override {
    std::lock_guard<std::mutex> lock(mu_);
    auto tmp = path;
    tmp += ".tmp";
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(kMagic, sizeof(kMagic));
    Write32(f, kVersion);

    uint32_t pair_count = 0;
    for (const auto& kv : map_) pair_count += kv.second.size();
    Write32(f, pair_count);

    for (const auto& outer : map_) {
      const auto& a = outer.first;
      for (const auto& inner : outer.second) {
        const auto& b = inner.first;
        const uint32_t cnt = inner.second;
        if (a.size() > UINT16_MAX || b.size() > UINT16_MAX) continue;
        Write16(f, static_cast<uint16_t>(a.size()));
        f.write(a.data(), static_cast<std::streamsize>(a.size()));
        Write16(f, static_cast<uint16_t>(b.size()));
        f.write(b.data(), static_cast<std::streamsize>(b.size()));
        Write32(f, cnt);
      }
    }
    f.close();
    if (!f) return false;
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    return !ec;
  }

  bool Load(const std::filesystem::path& path) override {
    std::lock_guard<std::mutex> lock(mu_);
    map_.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[sizeof(kMagic)];
    if (!f.read(magic, sizeof(kMagic)) ||
        std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
      DAFENG_LOG_WARN("ngram: bad magic in %s", path.c_str());
      return false;
    }
    uint32_t version = 0;
    if (!Read32(f, &version) || version != kVersion) {
      DAFENG_LOG_WARN("ngram: unsupported version %u in %s", version,
                       path.c_str());
      return false;
    }
    uint32_t pair_count = 0;
    if (!Read32(f, &pair_count)) return false;
    for (uint32_t i = 0; i < pair_count; ++i) {
      uint16_t alen = 0, blen = 0;
      uint32_t cnt = 0;
      if (!Read16(f, &alen)) return false;
      std::string a(alen, '\0');
      if (alen > 0 && !f.read(a.data(), alen)) return false;
      if (!Read16(f, &blen)) return false;
      std::string b(blen, '\0');
      if (blen > 0 && !f.read(b.data(), blen)) return false;
      if (!Read32(f, &cnt)) return false;
      map_[a][b] = cnt;
    }
    return true;
  }

  std::size_t Size() const override {
    std::lock_guard<std::mutex> lock(mu_);
    std::size_t n = 0;
    for (const auto& kv : map_) n += kv.second.size();
    return n;
  }

 private:
  using Inner = std::unordered_map<std::string, uint32_t>;
  mutable std::mutex mu_;
  std::unordered_map<std::string, Inner> map_;
};

// Returns the number of bytes the next UTF-8 character occupies, or 0 on
// invalid input. Conservative: handles 1-4 byte sequences only.
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

}  // namespace

std::unique_ptr<INgramTable> MakeNgramTable() {
  return std::make_unique<NgramTable>();
}

uint64_t UpdateBigramsFromHistory(IHistoryStore& history, INgramTable& table,
                                   uint64_t max_entries) {
  // Walk in chronological order, chain consecutive commits within 2s as
  // one phrase, then emit bigrams across the chained sequence. For wubi
  // this is essential because each commit is a single character.
  constexpr uint64_t kChainGapUs = 2'000'000;
  uint64_t increments = 0;

  auto entries = history.RecentEntries(max_entries);
  std::sort(entries.begin(), entries.end(),
             [](const HistoryEntry& a, const HistoryEntry& b) {
               return a.ts_us < b.ts_us;
             });

  std::string prev_char;
  uint64_t prev_ts = 0;
  for (const auto& entry : entries) {
    if (!prev_char.empty() && (entry.ts_us - prev_ts) > kChainGapUs) {
      // Phrase boundary — drop the last char so we don't form bigrams
      // across unrelated entries.
      prev_char.clear();
    }
    const std::string& s = entry.committed_text;
    std::size_t i = 0;
    while (i < s.size()) {
      const auto n = Utf8Len(s, i);
      if (n == 0) break;
      std::string ch = s.substr(i, n);
      if (!prev_char.empty()) {
        table.IncrementBigram(prev_char, ch);
        ++increments;
      }
      prev_char = std::move(ch);
      i += n;
    }
    prev_ts = entry.ts_us;
  }
  return increments;
}

}  // namespace dafeng
