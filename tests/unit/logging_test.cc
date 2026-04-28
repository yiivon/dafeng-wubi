// Logging is a footgun if it's silently dropping or noisily flooding. This
// suite locks down the level filter and the mute switch, which are the
// behaviors the IME-side caller depends on (sandboxed processes have no
// useful stderr).

#include "dafeng/logging.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "test_helpers.h"

namespace dafeng {
namespace {

class LoggingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Restore defaults at the start of each test in case a prior test mutated
    // process-wide state.
    SetLogEnabled(true);
    SetLogLevel(LogLevel::kInfo);
  }
  void TearDown() override {
    SetLogEnabled(true);
    SetLogLevel(LogLevel::kInfo);
  }

  // Capture stderr writes for the duration of `fn`. We `freopen` stderr
  // onto a temp file (cross-platform), then re-`freopen` back to the
  // original FD-2 stream after `fn` finishes. POSIX `dup`/`dup2` would
  // be cleaner but doesn't exist on MSVC under those names.
  static std::string CaptureStderr(const std::function<void()>& fn) {
    auto tmp = dafeng::testing::MakeTempPath("dafeng-log-", ".txt");
    std::fflush(stderr);
#if defined(_WIN32)
    // freopen with NULL filename is a Windows extension to "rebind"
    // stderr; we just freopen forward to a path then back to "CONOUT$"
    // (the console). For a unit test that doesn't care about the visual
    // output afterwards, the simpler approach is to redirect once,
    // capture, and let the test process exit; freopen is good enough.
    if (std::freopen(tmp.string().c_str(), "w", stderr) == nullptr) return {};
#else
    if (std::freopen(tmp.string().c_str(), "w", stderr) == nullptr) return {};
#endif
    fn();
    std::fflush(stderr);
    // Best-effort restore — on Windows we don't reopen onto a tty, the
    // remaining test code uses GoogleTest's own stdout/stderr handling
    // anyway. On POSIX we restore via /dev/tty / /dev/stderr.
#if defined(_WIN32)
    std::freopen("nul", "w", stderr);
#else
    std::freopen("/dev/tty", "w", stderr);
#endif
    std::ifstream in(tmp, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    in.close();
    dafeng::testing::RemoveAll(tmp);
    return oss.str();
  }
};

TEST_F(LoggingTest, GetLogLevelRoundTrip) {
  SetLogLevel(LogLevel::kWarn);
  EXPECT_EQ(GetLogLevel(), LogLevel::kWarn);
  SetLogLevel(LogLevel::kError);
  EXPECT_EQ(GetLogLevel(), LogLevel::kError);
}

TEST_F(LoggingTest, BelowLevelIsFilteredOut) {
  SetLogLevel(LogLevel::kWarn);
  std::string out = CaptureStderr([] {
    DAFENG_LOG_DEBUG("dropped");
    DAFENG_LOG_INFO("dropped");
  });
  EXPECT_EQ(out, "");
}

TEST_F(LoggingTest, AtOrAboveLevelEmitsLine) {
  SetLogLevel(LogLevel::kWarn);
  std::string out = CaptureStderr([] {
    DAFENG_LOG_WARN("kept-w");
    DAFENG_LOG_ERROR("kept-e");
  });
  EXPECT_NE(out.find("kept-w"), std::string::npos);
  EXPECT_NE(out.find("kept-e"), std::string::npos);
}

TEST_F(LoggingTest, MuteSuppressesAllLevels) {
  SetLogLevel(LogLevel::kDebug);
  SetLogEnabled(false);
  std::string out = CaptureStderr([] {
    DAFENG_LOG_DEBUG("nope");
    DAFENG_LOG_ERROR("nope");
  });
  EXPECT_EQ(out, "");
}

TEST_F(LoggingTest, FormatArgumentsArePrinted) {
  SetLogLevel(LogLevel::kInfo);
  std::string out = CaptureStderr([] {
    DAFENG_LOG_INFO("answer is %d, name is %s", 42, "kevin");
  });
  EXPECT_NE(out.find("answer is 42"), std::string::npos);
  EXPECT_NE(out.find("name is kevin"), std::string::npos);
}

TEST_F(LoggingTest, IncludesFileNameAndLevelTag) {
  SetLogLevel(LogLevel::kInfo);
  std::string out = CaptureStderr([] { DAFENG_LOG_INFO("x"); });
  // Level tag is one of D/I/W/E enclosed in [...]
  EXPECT_NE(out.find(" I "), std::string::npos);
  EXPECT_NE(out.find("logging_test.cc"), std::string::npos);
}

}  // namespace
}  // namespace dafeng
