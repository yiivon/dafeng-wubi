#pragma once

// Symbol-visibility macros. Set DAFENG_BUILDING_SHARED in the target that
// produces a shared library; consumers leave it undefined.

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(DAFENG_BUILDING_SHARED)
#    define DAFENG_API __declspec(dllexport)
#  elif defined(DAFENG_USING_SHARED)
#    define DAFENG_API __declspec(dllimport)
#  else
#    define DAFENG_API
#  endif
#  define DAFENG_LOCAL
#else
#  if defined(DAFENG_BUILDING_SHARED)
#    define DAFENG_API __attribute__((visibility("default")))
#  else
#    define DAFENG_API
#  endif
#  define DAFENG_LOCAL __attribute__((visibility("hidden")))
#endif
