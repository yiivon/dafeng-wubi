#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "../../src/daemon/history_store.h"
#include "../../src/daemon/new_word.h"
#include "dafeng/types.h"

namespace dafeng {
namespace {

class NewWordFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    char tmpl[] = "/tmp/dafeng-newword-XXXXXX";
    char* dir = ::mkdtemp(tmpl);
    ASSERT_NE(dir, nullptr);
    dir_ = dir;
    store_ = MakeSqliteHistoryStore(dir_ / "h.db", dir_);
    ASSERT_NE(store_, nullptr);
  }
  void TearDown() override {
    store_.reset();
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  void Insert(const std::string& text) {
    CommitEvent ev;
    ev.committed_text = text;
    store_->Insert(ev);
  }

  std::filesystem::path dir_;
  std::unique_ptr<IHistoryStore> store_;
};

TEST_F(NewWordFixture, EmptyHistoryYieldsNoCandidates) {
  auto disc = MakeFrequencyNewWordDiscovery();
  DiscoveryConfig cfg;
  EXPECT_TRUE(disc->Discover(*store_, cfg).empty());
}

TEST_F(NewWordFixture, BigramsThatHitThresholdAreDiscovered) {
  // "今天" appears 3 times across separate commits.
  Insert("今天天气真好");
  Insert("我今天上班");
  Insert("今天我们吃饭");
  auto disc = MakeFrequencyNewWordDiscovery();
  DiscoveryConfig cfg;
  cfg.min_frequency = 3;
  cfg.include_trigrams = false;
  auto out = disc->Discover(*store_, cfg);

  // "今天" must be in the output with frequency >= 3.
  bool found = false;
  for (const auto& w : out) {
    if (w.text == "今天") {
      found = true;
      EXPECT_GE(w.frequency, 3u);
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(NewWordFixture, TrigramsAreDiscoveredWhenEnabled) {
  Insert("自然语言处理");
  Insert("自然语言模型");
  Insert("自然语言");
  auto disc = MakeFrequencyNewWordDiscovery();
  DiscoveryConfig cfg;
  cfg.min_frequency = 3;
  cfg.include_bigrams = false;
  auto out = disc->Discover(*store_, cfg);

  bool found = false;
  for (const auto& w : out) {
    if (w.text == "自然语") {
      found = true;
      EXPECT_GE(w.frequency, 3u);
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(NewWordFixture, BelowThresholdIsRejected) {
  Insert("一二三");
  Insert("四五六");
  auto disc = MakeFrequencyNewWordDiscovery();
  DiscoveryConfig cfg;
  cfg.min_frequency = 3;
  EXPECT_TRUE(disc->Discover(*store_, cfg).empty());
}

TEST_F(NewWordFixture, AsciiOnlyCandidatesAreFiltered) {
  // Pure ASCII is the user typing English in passthrough mode — not a
  // new Chinese word.
  Insert("abcdef");
  Insert("abcdef");
  Insert("abcdef");
  auto disc = MakeFrequencyNewWordDiscovery();
  DiscoveryConfig cfg;
  cfg.min_frequency = 2;
  auto out = disc->Discover(*store_, cfg);
  for (const auto& w : out) {
    // Nothing in `out` should be ASCII-only.
    bool has_nonascii = false;
    for (unsigned char c : w.text) {
      if (c >= 0x80) { has_nonascii = true; break; }
    }
    EXPECT_TRUE(has_nonascii) << "ascii leaked: " << w.text;
  }
}

TEST_F(NewWordFixture, AllSameCharRepeatsAtTrigramAreFiltered) {
  Insert("呵呵呵呵呵");
  Insert("呵呵呵呵");
  Insert("呵呵呵呵呵呵");
  auto disc = MakeFrequencyNewWordDiscovery();
  DiscoveryConfig cfg;
  cfg.min_frequency = 2;
  cfg.include_bigrams = true;
  cfg.include_trigrams = true;
  auto out = disc->Discover(*store_, cfg);
  // The trigram "呵呵呵" should be filtered as repeat noise even if frequent.
  for (const auto& w : out) {
    EXPECT_NE(w.text, "呵呵呵");
  }
}

TEST_F(NewWordFixture, OutputIsSortedByFrequencyDesc) {
  Insert("我们我们我们我们");  // 4x of "我们"
  Insert("今天今天");           // 2x of "今天"
  auto disc = MakeFrequencyNewWordDiscovery();
  DiscoveryConfig cfg;
  cfg.min_frequency = 2;
  auto out = disc->Discover(*store_, cfg);
  ASSERT_GE(out.size(), 2u);
  for (std::size_t i = 1; i < out.size(); ++i) {
    EXPECT_GE(out[i - 1].frequency, out[i].frequency);
  }
}

TEST_F(NewWordFixture, FirstAndLastSeenTimestampsArePopulated) {
  Insert("今天");
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  Insert("今天");
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  Insert("今天");
  auto disc = MakeFrequencyNewWordDiscovery();
  DiscoveryConfig cfg;
  cfg.min_frequency = 3;
  auto out = disc->Discover(*store_, cfg);
  ASSERT_FALSE(out.empty());
  for (const auto& w : out) {
    if (w.text == "今天") {
      EXPECT_GT(w.first_seen_us, 0u);
      EXPECT_GE(w.last_seen_us, w.first_seen_us);
      return;
    }
  }
  FAIL() << "expected 今天 in discovery output";
}

TEST_F(NewWordFixture, ConfigDisablesBigramsAndTrigrams) {
  Insert("今天今天今天");
  auto disc = MakeFrequencyNewWordDiscovery();
  DiscoveryConfig cfg;
  cfg.min_frequency = 1;
  cfg.include_bigrams = false;
  cfg.include_trigrams = false;
  auto out = disc->Discover(*store_, cfg);
  EXPECT_TRUE(out.empty());
}

}  // namespace
}  // namespace dafeng
