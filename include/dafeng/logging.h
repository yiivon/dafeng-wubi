#pragma once

#include "dafeng/api.h"

namespace dafeng {

enum class LogLevel { kDebug = 0, kInfo = 1, kWarn = 2, kError = 3 };

DAFENG_API void SetLogLevel(LogLevel level);
DAFENG_API LogLevel GetLogLevel();

// Set whether the IME-side client should suppress logging. The daemon prints
// freely; the IME process is in a sandbox and stderr may be inaccessible, so
// callers can mute logging on that side without conditional code at every
// callsite.
DAFENG_API void SetLogEnabled(bool enabled);

DAFENG_API void Log(LogLevel level, const char* file, int line, const char* fmt,
                    ...) __attribute__((format(printf, 4, 5)));

}  // namespace dafeng

#define DAFENG_LOG_DEBUG(...) \
  ::dafeng::Log(::dafeng::LogLevel::kDebug, __FILE__, __LINE__, __VA_ARGS__)
#define DAFENG_LOG_INFO(...) \
  ::dafeng::Log(::dafeng::LogLevel::kInfo, __FILE__, __LINE__, __VA_ARGS__)
#define DAFENG_LOG_WARN(...) \
  ::dafeng::Log(::dafeng::LogLevel::kWarn, __FILE__, __LINE__, __VA_ARGS__)
#define DAFENG_LOG_ERROR(...) \
  ::dafeng::Log(::dafeng::LogLevel::kError, __FILE__, __LINE__, __VA_ARGS__)
