#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "../../src/daemon/new_word.h"
#include "../../src/daemon/user_dict.h"

namespace dafeng {
namespace {

class UserDictFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    char tmpl[] = "/tmp/dafeng-userdict-XXXXXX";
    char* dir = ::mkdtemp(tmpl);
    ASSERT_NE(dir, nullptr);
    dir_ = dir;
  }
  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  static std::string ReadAll(const std::filesystem::path& p) {
    std::ifstream f(p);
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
  }

  std::filesystem::path dir_;
};

DiscoveredWord MakeWord(const std::string& text, uint32_t freq) {
  DiscoveredWord w;
  w.text = text;
  w.frequency = freq;
  w.first_seen_us = 1714186800000000ull;
  w.last_seen_us = 1714271800000000ull;
  return w;
}

TEST_F(UserDictFixture, GeneratesValidYamlWithEntries) {
  auto gen = MakeYamlUserDictGenerator();
  auto path = dir_ / "learned_words.yaml";
  std::vector<DiscoveredWord> words = {MakeWord("今天", 15),
                                         MakeWord("我们", 12)};
  ASSERT_TRUE(gen->Generate(words, path));
  auto contents = ReadAll(path);

  EXPECT_NE(contents.find("schema_id: dafeng_learned"), std::string::npos);
  EXPECT_NE(contents.find("text: \"今天\""), std::string::npos);
  EXPECT_NE(contents.find("frequency: 15"), std::string::npos);
  EXPECT_NE(contents.find("text: \"我们\""), std::string::npos);
  EXPECT_NE(contents.find("frequency: 12"), std::string::npos);
}

TEST_F(UserDictFixture, EmptyWordListProducesValidEmptyYaml) {
  auto gen = MakeYamlUserDictGenerator();
  auto path = dir_ / "empty.yaml";
  ASSERT_TRUE(gen->Generate({}, path));
  auto contents = ReadAll(path);
  EXPECT_NE(contents.find("Total entries: 0"), std::string::npos);
  EXPECT_NE(contents.find("words:"), std::string::npos);
}

TEST_F(UserDictFixture, AtomicViaTempThenRename) {
  auto gen = MakeYamlUserDictGenerator();
  auto path = dir_ / "atomic.yaml";
  std::vector<DiscoveredWord> words = {MakeWord("好", 5)};
  ASSERT_TRUE(gen->Generate(words, path));
  std::error_code ec;
  EXPECT_TRUE(std::filesystem::exists(path, ec));
  EXPECT_FALSE(std::filesystem::exists(path.string() + ".tmp", ec));
}

TEST_F(UserDictFixture, EscapesQuotesAndBackslashes) {
  auto gen = MakeYamlUserDictGenerator();
  auto path = dir_ / "esc.yaml";
  std::vector<DiscoveredWord> words = {MakeWord("a\"b\\c", 1)};
  ASSERT_TRUE(gen->Generate(words, path));
  auto contents = ReadAll(path);
  EXPECT_NE(contents.find("\"a\\\"b\\\\c\""), std::string::npos);
}

TEST_F(UserDictFixture, RegenerationOverwrites) {
  auto gen = MakeYamlUserDictGenerator();
  auto path = dir_ / "regen.yaml";
  ASSERT_TRUE(gen->Generate({MakeWord("一", 1)}, path));
  ASSERT_TRUE(gen->Generate({MakeWord("二", 2)}, path));
  auto contents = ReadAll(path);
  EXPECT_NE(contents.find("text: \"二\""), std::string::npos);
  EXPECT_EQ(contents.find("text: \"一\""), std::string::npos);
}

TEST_F(UserDictFixture, RequestRimeRedeployTouchesSentinel) {
  EXPECT_TRUE(RequestRimeRedeploy(dir_));
  std::error_code ec;
  EXPECT_TRUE(std::filesystem::exists(dir_ / "redeploy_requested", ec));
}

TEST_F(UserDictFixture, RequestRimeRedeployIsIdempotent) {
  EXPECT_TRUE(RequestRimeRedeploy(dir_));
  EXPECT_TRUE(RequestRimeRedeploy(dir_));
  EXPECT_TRUE(RequestRimeRedeploy(dir_));
  // Sentinel still exists, just rewritten with newer timestamp.
  std::error_code ec;
  EXPECT_TRUE(std::filesystem::exists(dir_ / "redeploy_requested", ec));
}

}  // namespace
}  // namespace dafeng
