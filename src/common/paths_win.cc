// Windows path resolution. Phase 2.1 ships a stub — Mac is the build target.
// Full implementation (named pipes, %APPDATA% via SHGetFolderPath) lands when
// the Windows port is wired up.

#include "dafeng/paths.h"

#include <cstdlib>
#include <system_error>

namespace dafeng {

std::filesystem::path GetDataDir() {
  if (const char* override_dir = std::getenv("DAFENG_DATA_DIR")) {
    if (override_dir[0] != '\0') return std::filesystem::path(override_dir);
  }
  if (const char* appdata = std::getenv("APPDATA")) {
    if (appdata[0] != '\0') {
      return std::filesystem::path(appdata) / "Dafeng";
    }
  }
  return std::filesystem::current_path() / ".dafeng";
}

std::string GetDaemonAddress() {
  if (const char* override_addr = std::getenv("DAFENG_DAEMON_ADDR")) {
    if (override_addr[0] != '\0') return override_addr;
  }
  return "\\\\.\\pipe\\dafeng-daemon";
}

bool EnsureDataDir() {
  std::error_code ec;
  std::filesystem::create_directories(GetDataDir(), ec);
  return !ec;
}

}  // namespace dafeng
