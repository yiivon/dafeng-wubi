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

// printf-format checking is GCC/Clang only; MSVC has no equivalent at the
// function-decl level (the closest is sal.h's _Printf_format_string_, but
// that goes on a parameter and only fires inside MS analysis tools).
#if defined(__GNUC__) || defined(__clang__)
#define DAFENG_PRINTF_LIKE(fmt_idx, vararg_idx) \
  __attribute__((format(printf, fmt_idx, vararg_idx)))
#else
#define DAFENG_PRINTF_LIKE(fmt_idx, vararg_idx)
#endif

DAFENG_API void Log(LogLevel level, const char* file, int line, const char* fmt,
                    ...) DAFENG_PRINTF_LIKE(4, 5);

}  // namespace dafeng

#define DAFENG_LOG_DEBUG(...) \
  ::dafeng::Log(::dafeng::LogLevel::kDebug, __FILE__, __LINE__, __VA_ARGS__)
#define DAFENG_LOG_INFO(...) \
  ::dafeng::Log(::dafeng::LogLevel::kInfo, __FILE__, __LINE__, __VA_ARGS__)
#define DAFENG_LOG_WARN(...) \
  ::dafeng::Log(::dafeng::LogLevel::kWarn, __FILE__, __LINE__, __VA_ARGS__)
#define DAFENG_LOG_ERROR(...) \
  ::dafeng::Log(::dafeng::LogLevel::kError, __FILE__, __LINE__, __VA_ARGS__)
