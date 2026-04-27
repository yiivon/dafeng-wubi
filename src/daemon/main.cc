// dafeng-daemon entry point. Phase 2.1 ships a single binary that runs in
// the foreground; daemonization (launchd plist on macOS, scheduled task on
// Windows) is Phase 3 packaging work.

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "dafeng/endpoint.h"
#include "dafeng/logging.h"
#include "dafeng/paths.h"
#include "server.h"
#include "services.h"

namespace {

dafeng::Server* g_server = nullptr;

void HandleSignal(int /*sig*/) {
  // Async-signal-safe: only writes a byte through a pipe.
  if (g_server != nullptr) g_server->Shutdown();
}

void InstallSignalHandlers() {
  struct sigaction sa {};
  sa.sa_handler = HandleSignal;
  // Macro on macOS — must not be qualified with ::.
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  std::signal(SIGPIPE, SIG_IGN);  // we already check write() return values
}

void PrintUsage() {
  std::fprintf(stderr,
               "Usage: dafeng-daemon [options]\n"
               "  --foreground         run in foreground (default)\n"
               "  --addr <path>        socket path / pipe name\n"
               "  --log-level <lvl>    debug | info | warn | error\n"
               "  -h, --help           show this help\n");
}

bool ParseLogLevel(const std::string& s, dafeng::LogLevel* out) {
  if (s == "debug") { *out = dafeng::LogLevel::kDebug; return true; }
  if (s == "info") { *out = dafeng::LogLevel::kInfo; return true; }
  if (s == "warn") { *out = dafeng::LogLevel::kWarn; return true; }
  if (s == "error") { *out = dafeng::LogLevel::kError; return true; }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  std::string addr = dafeng::GetDaemonAddress();
  dafeng::LogLevel level = dafeng::LogLevel::kInfo;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      PrintUsage();
      return 0;
    }
    if (a == "--foreground") continue;
    if (a == "--addr" && i + 1 < argc) {
      addr = argv[++i];
      continue;
    }
    if (a == "--log-level" && i + 1 < argc) {
      if (!ParseLogLevel(argv[++i], &level)) {
        std::fprintf(stderr, "invalid log level: %s\n", argv[i]);
        return 1;
      }
      continue;
    }
    std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
    PrintUsage();
    return 1;
  }

  dafeng::SetLogLevel(level);

  if (!dafeng::EnsureDataDir()) {
    DAFENG_LOG_WARN("could not create data directory");
  }

  auto endpoint = dafeng::ListenEndpoint(addr);
  if (!endpoint) {
    DAFENG_LOG_ERROR("listen failed at %s", addr.c_str());
    return 2;
  }

  dafeng::Server server(std::move(endpoint),
                        dafeng::MakeMockReverseRerankService(),
                        dafeng::MakeNullCommitLogger());
  g_server = &server;
  InstallSignalHandlers();

  DAFENG_LOG_INFO("dafeng-daemon ready on %s", addr.c_str());
  server.Run();
  DAFENG_LOG_INFO("dafeng-daemon exiting");
  g_server = nullptr;
  return 0;
}
