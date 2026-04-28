// Windows path resolution.
//
//   GetDataDir()       → %APPDATA%\Dafeng\   (FOLDERID_RoamingAppData)
//   GetDaemonAddress() → \\.\pipe\dafeng-daemon-<sanitized-username>
//
// Per-user pipe naming avoids cross-user collisions on shared boxes.
// SHGetKnownFolderPath is preferred over the APPDATA env var because the
// env var can be missing or stale when launched from a Scheduled Task.

#include "dafeng/paths.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <lmcons.h>   // UNLEN — username buffer length
#include <shlobj.h>

#include <array>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <system_error>

namespace dafeng {

namespace {

std::string WideToUtf8(const wchar_t* w) {
  if (w == nullptr || *w == L'\0') return {};
  const int wlen = static_cast<int>(std::wcslen(w));
  const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, wlen, nullptr, 0,
                                      nullptr, nullptr);
  if (n <= 0) return {};
  std::string out(static_cast<std::size_t>(n), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, w, wlen, out.data(), n, nullptr, nullptr);
  return out;
}

std::filesystem::path RoamingAppData() {
  PWSTR raw = nullptr;
  HRESULT hr = ::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr,
                                      &raw);
  if (FAILED(hr) || raw == nullptr) {
    if (raw) ::CoTaskMemFree(raw);
    return {};
  }
  std::filesystem::path p(raw);
  ::CoTaskMemFree(raw);
  return p;
}

// Pipe name segments are constrained to a single path component (no slashes,
// no backslashes). Restrict to a conservative ASCII subset to avoid MUTF-8
// or quirky chars in the pipe namespace.
std::string SanitizeForPipeName(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    const unsigned char u = static_cast<unsigned char>(c);
    if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
        (u >= '0' && u <= '9') || u == '-' || u == '_' || u == '.') {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  if (out.empty()) out = "user";
  return out;
}

std::string CurrentUserName() {
  std::array<wchar_t, UNLEN + 1> buf{};
  DWORD size = static_cast<DWORD>(buf.size());
  if (::GetUserNameW(buf.data(), &size)) {
    return SanitizeForPipeName(WideToUtf8(buf.data()));
  }
  // Fall back to USERNAME env, then a fixed token.
  if (const char* env = std::getenv("USERNAME")) {
    if (env[0] != '\0') return SanitizeForPipeName(env);
  }
  return "user";
}

}  // namespace

std::filesystem::path GetDataDir() {
  if (const char* override_dir = std::getenv("DAFENG_DATA_DIR")) {
    if (override_dir[0] != '\0') return std::filesystem::path(override_dir);
  }
  auto base = RoamingAppData();
  if (!base.empty()) return base / "Dafeng";
  // Final fallback: working directory. Should rarely fire in practice.
  return std::filesystem::current_path() / ".dafeng";
}

std::string GetDaemonAddress() {
  if (const char* override_addr = std::getenv("DAFENG_DAEMON_ADDR")) {
    if (override_addr[0] != '\0') return override_addr;
  }
  return std::string("\\\\.\\pipe\\dafeng-daemon-") + CurrentUserName();
}

bool EnsureDataDir() {
  std::error_code ec;
  std::filesystem::create_directories(GetDataDir(), ec);
  return !ec;
}

}  // namespace dafeng
