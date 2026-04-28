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
#include "history_store.h"
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
               "  --backend <name>     mock | deterministic | mlx | llama_cpp\n"
               "                       (default: deterministic)\n"
               "  --model-path <path>  model file (MLX safetensors / GGUF)\n"
               "  -h, --help           show this help\n");
}

enum class BackendKind { kMock, kDeterministic, kMLX, kLlamaCpp };

bool ParseBackend(const std::string& s, BackendKind* out) {
  if (s == "mock") { *out = BackendKind::kMock; return true; }
  if (s == "deterministic") { *out = BackendKind::kDeterministic; return true; }
  if (s == "mlx") { *out = BackendKind::kMLX; return true; }
  if (s == "llama_cpp" || s == "llamacpp") {
    *out = BackendKind::kLlamaCpp; return true;
  }
  return false;
}

const char* BackendName(BackendKind b) {
  switch (b) {
    case BackendKind::kMock: return "mock";
    case BackendKind::kDeterministic: return "deterministic";
    case BackendKind::kMLX: return "mlx";
    case BackendKind::kLlamaCpp: return "llama_cpp";
  }
  return "?";
}

bool ParseLogLevel(const std::string& s, dafeng::LogLevel* out) {
  if (s == "debug") { *out = dafeng::LogLevel::kDebug; return true; }
  if (s == "info") { *out = dafeng::LogLevel::kInfo; return true; }
  if (s == "warn") { *out = dafeng::LogLevel::kWarn; return true; }
  if (s == "error") { *out = dafeng::LogLevel::kError; return true; }
  return false;
}

}  // namespace

std::unique_ptr<dafeng::IRerankService> BuildReranker(BackendKind kind,
                                                      const std::string& model_path) {
  switch (kind) {
    case BackendKind::kMock:
      return dafeng::MakeMockReverseRerankService();
    case BackendKind::kDeterministic:
      return dafeng::MakeDeterministicRerankService();
    case BackendKind::kMLX: {
      dafeng::MLXRerankConfig cfg;
      cfg.model_path = model_path;
      auto svc = dafeng::MakeMLXRerankService(cfg);
      if (!svc) {
        DAFENG_LOG_ERROR(
            "MLX backend not available — was DAFENG_ENABLE_MLX=ON at build "
            "time? Falling back to deterministic.");
        return dafeng::MakeDeterministicRerankService();
      }
      return svc;
    }
    case BackendKind::kLlamaCpp: {
      dafeng::MLXRerankConfig cfg;  // shared config struct; model_path is GGUF
      cfg.model_path = model_path;
      cfg.warmup_iterations = 2;
      auto svc = dafeng::MakeLlamaCppRerankService(cfg);
      if (!svc) {
        DAFENG_LOG_ERROR(
            "llama.cpp backend not available — was DAFENG_ENABLE_LLAMA_CPP=ON "
            "at build time and is the GGUF model_path correct? Falling back "
            "to deterministic.");
        return dafeng::MakeDeterministicRerankService();
      }
      return svc;
    }
  }
  return dafeng::MakeDeterministicRerankService();
}

int main(int argc, char** argv) {
  std::string addr = dafeng::GetDaemonAddress();
  dafeng::LogLevel level = dafeng::LogLevel::kInfo;
  BackendKind backend = BackendKind::kDeterministic;
  std::string model_path;

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
    if (a == "--backend" && i + 1 < argc) {
      if (!ParseBackend(argv[++i], &backend)) {
        std::fprintf(stderr, "invalid backend: %s\n", argv[i]);
        return 1;
      }
      continue;
    }
    if (a == "--model-path" && i + 1 < argc) {
      model_path = argv[++i];
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

  auto reranker = BuildReranker(backend, model_path);

  // History store lives under the data dir. The privacy guard in
  // MakeSqliteHistoryStore refuses paths outside data_root.
  const auto data_root = dafeng::GetDataDir();
  const auto db_path = data_root / "history.db";
  std::shared_ptr<dafeng::IHistoryStore> history(
      dafeng::MakeSqliteHistoryStore(db_path, data_root));
  std::unique_ptr<dafeng::ICommitLogger> logger =
      history ? dafeng::MakeSqliteCommitLogger(history)
              : dafeng::MakeNullCommitLogger();

  dafeng::Server server(std::move(endpoint), std::move(reranker),
                        std::move(logger));
  g_server = &server;
  InstallSignalHandlers();

  DAFENG_LOG_INFO("dafeng-daemon ready on %s (backend=%s)", addr.c_str(),
                   BackendName(backend));
  server.Run();
  DAFENG_LOG_INFO("dafeng-daemon exiting");
  g_server = nullptr;
  return 0;
}
