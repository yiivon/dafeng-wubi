// Logging is a footgun if it's silently dropping or noisily flooding. This
// suite locks down the level filter and the mute switch, which are the
// behaviors the IME-side caller depends on (sandboxed processes have no
// useful stderr).

#include "dafeng/logging.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include <gtest/gtest.h>

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

  // Capture stderr writes for the duration of `fn`.
  static std::string CaptureStderr(const std::function<void()>& fn) {
    std::fflush(stderr);
    int saved = ::dup(fileno(stderr));
    char tmpl[] = "/tmp/dafeng-log-XXXXXX";
    int fd = ::mkstemp(tmpl);
    ::dup2(fd, fileno(stderr));
    fn();
    std::fflush(stderr);
    ::dup2(saved, fileno(stderr));
    ::close(saved);
    ::lseek(fd, 0, SEEK_SET);
    std::string out;
    char buf[1024];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
      out.append(buf, static_cast<size_t>(n));
    }
    ::close(fd);
    ::unlink(tmpl);
    return out;
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
