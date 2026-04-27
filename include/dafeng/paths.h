#pragma once

#include <filesystem>
#include <string>

#include "dafeng/api.h"

namespace dafeng {

// Application data directory:
//   macOS:   ~/Library/Application Support/Dafeng/
//   Windows: %APPDATA%/Dafeng/
// May be overridden by the DAFENG_DATA_DIR environment variable for tests.
DAFENG_API std::filesystem::path GetDataDir();

// IPC endpoint address:
//   macOS:   <DataDir>/daemon.sock
//   Windows: \\.\pipe\dafeng-daemon
// May be overridden by the DAFENG_DAEMON_ADDR environment variable.
DAFENG_API std::string GetDaemonAddress();

// Ensures the data directory exists, creating it (and parents) if necessary.
// Returns false on filesystem error.
DAFENG_API bool EnsureDataDir();

}  // namespace dafeng
