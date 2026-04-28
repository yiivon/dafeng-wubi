#pragma once

// Wubi 86 phrase encoder. Reads `wubi86.dict.yaml` (or any RIME-format
// dict file) once, builds a char -> longest-code map, and applies the
// standard wubi 86 encoder rules to compute the canonical phrase code.
//
// Why this exists: when the user splits a phrase across multiple single-
// char commits (e.g. types `xu`->弹 + `pw`->窗 to mean 弹窗), the daemon
// can NOT just concatenate the user-typed shortcuts as the phrase code —
// the user wouldn't type that sequence later. The conventional wubi 86
// phrase code uses the FULL character codes:
//   2-char:  Aa + Ab + Ba + Bb     (first 2 keys of each char)
//   3-char:  Aa + Ba + Ca + Cb     (first key of c1/c2, first 2 of c3)
//   4+ char: Aa + Ba + Ca + Za     (first key of c1/c2/c3 and LAST char)
// where each lower-case letter denotes "the i-th key of that char's full
// wubi code".

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace dafeng {

class WubiCodec {
 public:
  // Returns true iff the dict was successfully parsed.
  // Safe to call again; re-parses from scratch.
  bool LoadFromDict(const std::filesystem::path& dict_yaml);

  // Number of distinct UTF-8 chars indexed.
  std::size_t Size() const { return char_to_full_code_.size(); }

  // Returns the longest code seen for `ch` (a single UTF-8 character),
  // or empty if never seen.
  std::string FullCode(const std::string& ch) const;

  // Apply the wubi 86 phrase encoder rules to a 2-or-more UTF-8-char
  // phrase. Returns "" if any contributing character has no known code,
  // or if the phrase has fewer than 2 characters.
  std::string EncodePhrase(const std::string& phrase) const;

 private:
  // longest code seen per char.
  std::unordered_map<std::string, std::string> char_to_full_code_;
};

}  // namespace dafeng
