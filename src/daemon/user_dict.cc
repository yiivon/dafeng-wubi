#include "user_dict.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#include "dafeng/logging.h"

namespace dafeng {

namespace {

std::string EscapeYamlString(const std::string& s) {
  // YAML double-quoted strings: escape \ and " and control characters.
  std::string out;
  out.reserve(s.size() + 2);
  out.push_back('"');
  for (char c : s) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
      out.push_back(c);
    } else if (c == '\n') {
      out.append("\\n");
    } else if (c == '\r') {
      out.append("\\r");
    } else if (c == '\t') {
      out.append("\\t");
    } else if (static_cast<unsigned char>(c) < 0x20) {
      // Other control characters: hex escape.
      std::ostringstream oss;
      oss << "\\x" << std::setfill('0') << std::setw(2) << std::hex
          << static_cast<int>(static_cast<unsigned char>(c));
      out.append(oss.str());
    } else {
      out.push_back(c);
    }
  }
  out.push_back('"');
  return out;
}

std::string IsoTimestamp() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto t = system_clock::to_time_t(now);
  std::tm tm_buf{};
  ::localtime_r(&t, &tm_buf);
  std::ostringstream oss;
  oss << std::put_time(&tm_buf, "%FT%T%z");
  return oss.str();
}

class YamlGenerator final : public IUserDictGenerator {
 public:
  bool Generate(const std::vector<DiscoveredWord>& words,
                 const std::filesystem::path& out_path) override {
    auto tmp = out_path;
    tmp += ".tmp";
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
      DAFENG_LOG_WARN("user_dict: open(%s) failed", tmp.c_str());
      return false;
    }

    f << "# Dafeng learned words.\n";
    f << "# Generated at: " << IsoTimestamp() << "\n";
    f << "# Total entries: " << words.size() << "\n";
    f << "# DO NOT EDIT BY HAND. Regenerated each learning round.\n";
    f << "---\n";
    f << "schema:\n";
    f << "  schema_id: dafeng_learned\n";
    f << "  version: \"1\"\n\n";
    f << "words:\n";
    for (const auto& w : words) {
      f << "  - text: " << EscapeYamlString(w.text) << "\n";
      f << "    frequency: " << w.frequency << "\n";
      f << "    first_seen_us: " << w.first_seen_us << "\n";
      f << "    last_seen_us: " << w.last_seen_us << "\n";
    }
    f.close();
    if (!f) {
      DAFENG_LOG_WARN("user_dict: write/close failed");
      return false;
    }

    std::error_code ec;
    std::filesystem::rename(tmp, out_path, ec);
    if (ec) {
      DAFENG_LOG_WARN("user_dict: rename failed: %s", ec.message().c_str());
      return false;
    }
    return true;
  }
};

}  // namespace

std::unique_ptr<IUserDictGenerator> MakeYamlUserDictGenerator() {
  return std::make_unique<YamlGenerator>();
}

bool RequestRimeRedeploy(const std::filesystem::path& data_dir) {
  std::error_code ec;
  std::filesystem::create_directories(data_dir, ec);
  if (ec) return false;
  const auto path = data_dir / "redeploy_requested";
  std::ofstream f(path, std::ios::trunc);
  if (!f) return false;
  f << IsoTimestamp() << "\n";
  return f.good();
}

}  // namespace dafeng
