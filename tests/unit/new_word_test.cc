#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "test_helpers.h"
#include "../../src/daemon/history_store.h"
#include "../../src/daemon/new_word.h"
#include "dafeng/types.h"

namespace dafeng {
namespace {

class NewWordFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = dafeng::testing::MakeTempDir("dafeng-newword-");
    ASSERT_FALSE(dir_.empty());
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

  void InsertWithCode(const std::string& text, const std::string& code) {
    CommitEvent ev;
    ev.code = code;
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

TEST_F(NewWordFixture, CodeAttributionAcrossSplitTypedSingles) {
  // Mimic Kevin's "弹窗" scenario: user types xu -> 弹, then immediately
  // pw -> 窗, two SEPARATE single-char commits within the chain window.
  // Discovery should produce (text="弹窗", code="xupw").
  InsertWithCode("弹", "xu");
  InsertWithCode("窗", "pw");
  InsertWithCode("弹", "xu");  // do it twice so freq=2
  InsertWithCode("窗", "pw");

  auto disc = MakeFrequencyNewWordDiscovery();
  DiscoveryConfig cfg;
  cfg.min_frequency = 2;
  auto out = disc->Discover(*store_, cfg);

  bool found = false;
  for (const auto& w : out) {
    if (w.text == "弹窗") {
      EXPECT_EQ(w.code, "xupw") << "code attribution failed";
      EXPECT_GE(w.frequency, 2u);
      found = true;
    }
  }
  EXPECT_TRUE(found) << "弹窗 (xupw) was not discovered";
}

TEST_F(NewWordFixture, MultiCharEntryDoesNotPolluteCodeAttribution) {
  // A multi-char entry has no per-char code attribution. Bigrams that
  // contain any token from such an entry should land with code="".
  InsertWithCode("感谢", "ndpy");  // already-known phrase, multi-char
  InsertWithCode("你", "wq");
  InsertWithCode("们", "wu");

  auto disc = MakeFrequencyNewWordDiscovery();
  DiscoveryConfig cfg;
  cfg.min_frequency = 1;
  auto out = disc->Discover(*store_, cfg);

  // 你们 should be discovered with code=wqwu (both single-char entries).
  // 谢你 (cross-boundary from multi-char entry) should have code="".
  bool ni_men_found = false;
  for (const auto& w : out) {
    if (w.text == "你们") {
      ni_men_found = true;
      EXPECT_EQ(w.code, "wqwu");
    } else if (w.text == "谢你") {
      EXPECT_EQ(w.code, "") << "code from a multi-char entry leaked";
    }
  }
  EXPECT_TRUE(ni_men_found);
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
