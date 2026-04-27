#include "dafeng/logging.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

namespace dafeng {

namespace {

std::atomic<LogLevel> g_log_level{LogLevel::kInfo};
std::atomic<bool> g_log_enabled{true};
std::mutex g_log_mutex;

const char* LevelTag(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug: return "D";
    case LogLevel::kInfo:  return "I";
    case LogLevel::kWarn:  return "W";
    case LogLevel::kError: return "E";
  }
  return "?";
}

const char* BaseName(const char* path) {
  const char* last = path;
  for (const char* p = path; *p; ++p) {
    if (*p == '/' || *p == '\\') last = p + 1;
  }
  return last;
}

}  // namespace

void SetLogLevel(LogLevel level) { g_log_level.store(level); }

LogLevel GetLogLevel() { return g_log_level.load(); }

void SetLogEnabled(bool enabled) { g_log_enabled.store(enabled); }

void Log(LogLevel level, const char* file, int line, const char* fmt, ...) {
  if (!g_log_enabled.load()) return;
  if (static_cast<int>(level) < static_cast<int>(g_log_level.load())) return;

  using clock = std::chrono::system_clock;
  auto now = clock::now();
  auto t = clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;
  std::tm tm_buf{};
#if defined(_WIN32)
  localtime_s(&tm_buf, &t);
#else
  localtime_r(&t, &tm_buf);
#endif

  char ts[32];
  std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03d", tm_buf.tm_hour,
                tm_buf.tm_min, tm_buf.tm_sec, static_cast<int>(ms.count()));

  std::lock_guard<std::mutex> lock(g_log_mutex);
  std::fprintf(stderr, "[%s %s %s:%d] ", ts, LevelTag(level), BaseName(file),
               line);
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
  std::fputc('\n', stderr);
}

}  // namespace dafeng
