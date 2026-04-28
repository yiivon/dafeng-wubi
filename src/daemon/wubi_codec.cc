#include "wubi_codec.h"

#include <fstream>
#include <string>
#include <vector>

#include "dafeng/logging.h"

namespace dafeng {

namespace {

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
  return need;
}

std::vector<std::string> SplitUtf8Chars(const std::string& s) {
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < s.size()) {
    auto n = Utf8Len(s, i);
    if (n == 0) break;
    out.push_back(s.substr(i, n));
    i += n;
  }
  return out;
}

}  // namespace

bool WubiCodec::LoadFromDict(const std::filesystem::path& dict_yaml) {
  char_to_full_code_.clear();
  std::ifstream f(dict_yaml);
  if (!f) {
    DAFENG_LOG_WARN("wubi_codec: cannot open %s", dict_yaml.c_str());
    return false;
  }

  // RIME dict format: YAML header, then a `...` line, then TSV lines.
  // We don't need YAML parsing — just walk lines and treat any line
  // starting with non-comment, non-YAML-marker, containing a TAB as a
  // candidate entry. Skip 0-tab and non-text lines.
  std::string line;
  bool past_header = false;
  std::size_t kept = 0;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (!past_header) {
      // Header ends at the second `...` or `---` separator. We just look
      // for the first line that has a TAB — that's data.
      if (line.find('\t') == std::string::npos) continue;
      past_header = true;
    }
    auto tab1 = line.find('\t');
    if (tab1 == std::string::npos) continue;
    auto tab2 = line.find('\t', tab1 + 1);
    std::string text = line.substr(0, tab1);
    std::string code = (tab2 == std::string::npos)
                            ? line.substr(tab1 + 1)
                            : line.substr(tab1 + 1, tab2 - tab1 - 1);
    if (text.empty() || code.empty()) continue;

    // Only single-character entries contribute to the per-char code map.
    auto chars = SplitUtf8Chars(text);
    if (chars.size() != 1) continue;

    auto& cur = char_to_full_code_[chars[0]];
    if (code.size() > cur.size()) cur = std::move(code);
    ++kept;
  }
  DAFENG_LOG_INFO("wubi_codec: loaded %zu char codes from %zu single-char entries",
                   char_to_full_code_.size(), kept);
  // Success iff the file was readable. An empty index (e.g. only multi-
  // char entries) is a degenerate but valid load.
  return true;
}

std::string WubiCodec::FullCode(const std::string& ch) const {
  auto it = char_to_full_code_.find(ch);
  if (it == char_to_full_code_.end()) return {};
  return it->second;
}

std::string WubiCodec::EncodePhrase(const std::string& phrase) const {
  auto chars = SplitUtf8Chars(phrase);
  if (chars.size() < 2) return {};

  // Look up each char's full code.
  std::vector<std::string> codes;
  codes.reserve(chars.size());
  for (const auto& c : chars) {
    auto it = char_to_full_code_.find(c);
    if (it == char_to_full_code_.end() || it->second.empty()) return {};
    codes.push_back(it->second);
  }

  auto take = [](const std::string& s, std::size_t n) {
    return s.size() >= n ? s.substr(0, n) : s;
  };

  std::string out;
  if (chars.size() == 2) {
    // AaAbBaBb — first 2 keys of each.
    out = take(codes[0], 2) + take(codes[1], 2);
  } else if (chars.size() == 3) {
    // AaBaCaCb — first key of c1, c2; first 2 of c3.
    out = take(codes[0], 1) + take(codes[1], 1) + take(codes[2], 2);
  } else {
    // 4+ chars: AaBaCaZa — first key of c1, c2, c3, and the LAST char.
    out = take(codes[0], 1) + take(codes[1], 1) + take(codes[2], 1) +
          take(codes.back(), 1);
  }
  return out;
}

}  // namespace dafeng
