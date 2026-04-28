// Unit tests for the wubi 86 phrase encoder. Builds a tiny synthetic
// dict in-memory and checks that EncodePhrase applies the right rule
// for each phrase length.

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "../../src/daemon/wubi_codec.h"

namespace dafeng {
namespace {

class WubiCodecFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    char tmpl[] = "/tmp/dafeng-wubi-codec-XXXXXX";
    char* dir = ::mkdtemp(tmpl);
    ASSERT_NE(dir, nullptr);
    dict_path_ = std::filesystem::path(dir) / "wubi86.dict.yaml";
  }
  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(dict_path_.parent_path(), ec);
  }

  void WriteDict(const std::string& tsv_body) {
    std::ofstream f(dict_path_);
    f << "# Test dict\n";
    f << "name: test\n";
    f << "...\n";
    f << tsv_body;
  }

  std::filesystem::path dict_path_;
};

TEST_F(WubiCodecFixture, LoadFromDictPicksLongestCodePerChar) {
  // Same char with multiple codes — codec should keep the longest.
  WriteDict(
      "这\tp\t1\n"
      "这\typ\t2\n"
      "这\typi\t3\n"
      "个\twh\t1\n"
      "个\twhj\t2\n");
  WubiCodec codec;
  ASSERT_TRUE(codec.LoadFromDict(dict_path_));
  EXPECT_EQ(codec.FullCode("这"), "ypi");
  EXPECT_EQ(codec.FullCode("个"), "whj");
  EXPECT_EQ(codec.FullCode("不存在的字"), "");
}

TEST_F(WubiCodecFixture, EncodeTwoCharPhrase) {
  // 弹 / 窗 mirror the real wubi86 dict entries.
  WriteDict(
      "弹\txujf\t1\n"
      "窗\tpwtq\t1\n");
  WubiCodec codec;
  ASSERT_TRUE(codec.LoadFromDict(dict_path_));
  // 2-char rule: AaAbBaBb -> first 2 keys of each char's full code.
  EXPECT_EQ(codec.EncodePhrase("弹窗"), "xupw");
}

TEST_F(WubiCodecFixture, EncodeTwoCharPhraseAccountsForShortChar) {
  // Char with only 1 key (rare but possible for mono-key shortcuts).
  WriteDict("这\typi\t1\n个\twh\t1\n");
  WubiCodec codec;
  ASSERT_TRUE(codec.LoadFromDict(dict_path_));
  // 这 first 2 = yp; 个 first 2 = wh -> ypwh
  EXPECT_EQ(codec.EncodePhrase("这个"), "ypwh");
}

TEST_F(WubiCodecFixture, EncodeThreeCharPhrase) {
  // Rule AaBaCaCb: 1+1+2.
  WriteDict(
      "中\tkhk\t1\n"
      "国\tlgyi\t1\n"
      "人\twwww\t1\n");
  WubiCodec codec;
  ASSERT_TRUE(codec.LoadFromDict(dict_path_));
  // k + l + ww = klww
  EXPECT_EQ(codec.EncodePhrase("中国人"), "klww");
}

TEST_F(WubiCodecFixture, EncodeFourCharPhrase) {
  // Rule AaBaCaZa: 1+1+1+1 (Z is the last char).
  WriteDict(
      "今\twyni\t1\n"
      "天\tgdi\t1\n"
      "我\ttrnt\t1\n"
      "们\twun\t1\n");
  WubiCodec codec;
  ASSERT_TRUE(codec.LoadFromDict(dict_path_));
  // w + g + t + w = wgtw
  EXPECT_EQ(codec.EncodePhrase("今天我们"), "wgtw");
}

TEST_F(WubiCodecFixture, EncodeFiveCharPhraseUsesFirstThreeAndLast) {
  WriteDict(
      "今\twyni\t1\n"
      "天\tgdi\t1\n"
      "我\ttrnt\t1\n"
      "们\twun\t1\n"
      "去\tfcu\t1\n");
  WubiCodec codec;
  ASSERT_TRUE(codec.LoadFromDict(dict_path_));
  // chars: 今(w), 天(g), 我(t), 们(skipped — not in first 3), 去(last) -> first key f
  // Encoding: w + g + t + f
  EXPECT_EQ(codec.EncodePhrase("今天我们去"), "wgtf");
}

TEST_F(WubiCodecFixture, EncodeReturnsEmptyWhenAnyCharMissing) {
  WriteDict("这\typi\t1\n");
  WubiCodec codec;
  ASSERT_TRUE(codec.LoadFromDict(dict_path_));
  EXPECT_EQ(codec.EncodePhrase("这@"), "");
}

TEST_F(WubiCodecFixture, EncodeReturnsEmptyForSingleChar) {
  WriteDict("这\typi\t1\n");
  WubiCodec codec;
  ASSERT_TRUE(codec.LoadFromDict(dict_path_));
  EXPECT_EQ(codec.EncodePhrase("这"), "");
}

TEST_F(WubiCodecFixture, MultiCharEntriesIgnoredWhenBuildingMap) {
  // Two-char entry should NOT pollute the per-char lookup.
  WriteDict("我们\twwwwun\t1\n");
  WubiCodec codec;
  ASSERT_TRUE(codec.LoadFromDict(dict_path_));
  EXPECT_EQ(codec.FullCode("我"), "");
  EXPECT_EQ(codec.FullCode("们"), "");
  EXPECT_EQ(codec.FullCode("我们"), "");  // not stored as a single char
}

}  // namespace
}  // namespace dafeng
