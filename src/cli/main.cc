// dafeng-cli — debugging / smoke-test command-line client. Not shipped with
// the IME; lives in the dev tree. Subcommands:
//   ping          send Ping, expect Pong, print round-trip time
//   rerank        send a RerankRequest, print the permutation
//   commit        send a CommitEvent (fire-and-forget)
//   help          print usage

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "dafeng/client.h"
#include "dafeng/logging.h"
#include "dafeng/paths.h"
// Daemon-internal headers for `learn` subcommand. Linked via dafeng::daemon_core.
#include "../daemon/git_sync.h"
#include "../daemon/history_store.h"
#include "../daemon/learning_round.h"
#include "../daemon/new_word.h"
#include "../daemon/ngram.h"
#include "../daemon/services.h"
#include "../daemon/user_dict.h"
#include "../daemon/wubi_codec.h"

namespace {

struct CommonArgs {
  std::string addr;
  int timeout_ms = 1000;
};

void PrintUsage() {
  std::fprintf(stderr,
               "Usage: dafeng-cli <command> [options]\n"
               "\n"
               "Commands:\n"
               "  ping                              probe the daemon\n"
               "  rerank --code C [--ctx X] --cands a,b,c [--app id]\n"
               "  commit --code C --text T [--ctx X]\n"
               "  stats                             dump daemon counters\n"
               "  history [--limit N]               dump recent commits\n"
               "  learn [--min-freq N] [--dry-run]  run a learning round\n"
               "  help                              this message\n"
               "\n"
               "Common options:\n"
               "  --addr <path>     daemon socket / pipe (default: built-in)\n"
               "  --timeout <ms>    per-call timeout (default: 1000)\n");
}

std::vector<std::string> Split(const std::string& s, char delim) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == delim) {
      out.push_back(std::move(cur));
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(std::move(cur));
  return out;
}

bool TakeArg(int& i, int argc, char** argv, const char* flag,
             std::string* out) {
  if (std::strcmp(argv[i], flag) == 0 && i + 1 < argc) {
    *out = argv[++i];
    return true;
  }
  return false;
}

bool TakeArgInt(int& i, int argc, char** argv, const char* flag, int* out) {
  std::string s;
  if (!TakeArg(i, argc, argv, flag, &s)) return false;
  *out = std::atoi(s.c_str());
  return true;
}

int CmdPing(const CommonArgs& args) {
  dafeng::DafengClient client(args.addr);
  const auto t0 = std::chrono::steady_clock::now();
  bool ok = client.Ping(std::chrono::milliseconds(args.timeout_ms));
  const auto t1 = std::chrono::steady_clock::now();
  const auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
                      .count();
  if (ok) {
    std::printf("pong in %lld us\n", static_cast<long long>(us));
    return 0;
  }
  std::fprintf(stderr, "ping failed (timeout or daemon unreachable)\n");
  return 1;
}

int CmdRerank(const CommonArgs& args, const std::string& code,
              const std::string& ctx, const std::string& cands_csv,
              const std::string& app) {
  dafeng::DafengClient client(args.addr);
  dafeng::RerankRequest req;
  req.code = code;
  req.context_before = ctx;
  req.candidates = Split(cands_csv, ',');
  req.app_id = app;

  auto resp = client.Rerank(req, std::chrono::milliseconds(args.timeout_ms));
  if (!resp) {
    std::fprintf(stderr, "rerank failed (timeout or daemon unreachable)\n");
    return 1;
  }

  std::printf("order: ");
  for (size_t i = 0; i < resp->reordered_indices.size(); ++i) {
    int idx = resp->reordered_indices[i];
    const std::string& cand =
        (idx >= 0 && idx < static_cast<int>(req.candidates.size()))
            ? req.candidates[idx]
            : std::string("?");
    std::printf("%s%s", i == 0 ? "" : " ", cand.c_str());
  }
  std::printf("\nlatency_us: %u\n", resp->latency_us);
  return 0;
}

int CmdCommit(const CommonArgs& args, const std::string& code,
              const std::string& text, const std::string& ctx) {
  dafeng::DafengClient client(args.addr);
  client.RecordCommit(code, text, ctx);
  std::printf("commit sent (fire-and-forget)\n");
  (void)args;
  return 0;
}

int CmdHistory(int limit) {
  const auto data_dir = dafeng::GetDataDir();
  auto store = dafeng::MakeSqliteHistoryStore(data_dir / "history.db", data_dir);
  if (!store) {
    std::fprintf(stderr, "history: cannot open store under %s\n",
                  data_dir.c_str());
    return 1;
  }
  std::printf("history: %llu rows total\n",
              static_cast<unsigned long long>(store->Count()));
  auto entries = store->RecentEntries(static_cast<uint64_t>(limit));
  for (const auto& e : entries) {
    std::printf("  %llu | code=%-8s text=%s\n",
                static_cast<unsigned long long>(e.id), e.code.c_str(),
                e.committed_text.c_str());
  }
  return 0;
}

int CmdLearn(uint32_t min_freq, bool dry_run) {
  const auto data_dir = dafeng::GetDataDir();
  auto store_unique =
      dafeng::MakeSqliteHistoryStore(data_dir / "history.db", data_dir);
  if (!store_unique) {
    std::fprintf(stderr, "learn: cannot open history store under %s\n",
                  data_dir.c_str());
    return 1;
  }
  std::shared_ptr<dafeng::IHistoryStore> store(std::move(store_unique));
  auto ngram = dafeng::MakeNgramTable();
  // Try to load any existing personal_ngram.bin so increments accumulate.
  ngram->Load(data_dir / "userdata" / "personal_ngram.bin");

  auto discovery = dafeng::MakeFrequencyNewWordDiscovery();
  auto user_dict = dafeng::MakeYamlUserDictGenerator();

  std::unique_ptr<dafeng::IGitSync> git;
  if (!dry_run) {
    dafeng::GitSyncConfig gcfg;
    gcfg.repo_path = (data_dir / "userdata").string();
    git = dafeng::MakeGitSync(gcfg);
  }

  dafeng::LearningRoundConfig cfg;
  cfg.data_dir = data_dir;
  // Mac default for RIME user dir.
  if (const char* home = std::getenv("HOME")) {
    cfg.rime_user_dir = std::filesystem::path(home) / "Library" / "Rime";
    cfg.wubi_dict_path = cfg.rime_user_dir / "wubi86.dict.yaml";
  }
  cfg.min_new_word_freq = min_freq;
  cfg.commit_to_git = !dry_run && git != nullptr;
  cfg.push_to_remote = false;  // CLAUDE.md privacy default

  // For visibility, pull discovered words ourselves before the round so
  // we can print them. The round will do the same plus persist.
  dafeng::DiscoveryConfig dcfg;
  dcfg.min_frequency = min_freq;
  dcfg.scan_max_entries = cfg.scan_max_entries;
  dcfg.include_bigrams = cfg.include_bigrams;
  dcfg.include_trigrams = cfg.include_trigrams;
  auto preview = discovery->Discover(*store, dcfg);

  // Apply wubi codec encoding for the preview too (mirrors what the
  // round will persist).
  if (!cfg.wubi_dict_path.empty()) {
    dafeng::WubiCodec codec;
    if (codec.LoadFromDict(cfg.wubi_dict_path)) {
      for (auto& w : preview) {
        auto canonical = codec.EncodePhrase(w.text);
        if (!canonical.empty()) w.code = std::move(canonical);
      }
    }
  }

  std::printf("history rows scanned: %llu\n",
              static_cast<unsigned long long>(store->Count()));
  std::printf("min_frequency: %u\n", min_freq);
  std::printf("discovered words (top 30):\n");
  std::printf("  freq  text         code  (code blank = code-attribution lost)\n");
  for (size_t i = 0; i < preview.size() && i < 30; ++i) {
    std::printf("  %4u  %-12s %s\n", preview[i].frequency,
                preview[i].text.c_str(),
                preview[i].code.empty() ? "-" : preview[i].code.c_str());
  }
  std::printf("(%zu total)\n", preview.size());

  if (dry_run) {
    std::printf("\n--dry-run: skipping persist + git commit.\n");
    return 0;
  }

  std::printf("\nrunning learning round...\n");
  auto r = dafeng::RunLearningRound(*store, *ngram, *discovery, *user_dict,
                                     git.get(), cfg);
  std::printf("  ok               : %s\n", r.ok ? "yes" : "no");
  std::printf("  bigrams added    : %llu\n",
              static_cast<unsigned long long>(r.bigram_increments));
  std::printf("  words discovered : %llu\n",
              static_cast<unsigned long long>(r.words_discovered));
  std::printf("  wrote user_dict  : %s\n",
              r.wrote_user_dict ? "yes" : "no");
  std::printf("  wrote ngram      : %s\n", r.wrote_ngram ? "yes" : "no");
  std::printf("  redeploy flagged : %s\n",
              r.requested_redeploy ? "yes" : "no");
  std::printf("  git committed    : %s\n", r.committed ? "yes" : "no");
  std::printf("  elapsed          : %lld ms\n",
              static_cast<long long>(r.elapsed.count()));
  return r.ok ? 0 : 1;
}

int CmdStats(const CommonArgs& args) {
  dafeng::DafengClient client(args.addr);
  auto stats = client.GetStats(std::chrono::milliseconds(args.timeout_ms));
  if (!stats) {
    std::fprintf(stderr, "stats failed (timeout or daemon unreachable)\n");
    return 1;
  }
  const auto rc = stats->rerank_count;
  const auto mean_us = rc > 0 ? stats->rerank_latency_sum_us / rc : 0;
  static const char* const kModelNames[] = {"mock", "deterministic", "mlx",
                                             "llama_cpp"};
  const char* model_name =
      stats->rerank_model_version < (sizeof(kModelNames) / sizeof(*kModelNames))
          ? kModelNames[stats->rerank_model_version]
          : "unknown";
  std::printf("daemon uptime    : %llu s\n",
              (unsigned long long)stats->uptime_sec);
  std::printf("rerank model     : %s (v%u)\n", model_name,
              static_cast<unsigned>(stats->rerank_model_version));
  std::printf("rerank requests  : %llu\n",
              (unsigned long long)stats->rerank_count);
  std::printf("ping requests    : %llu\n",
              (unsigned long long)stats->ping_count);
  std::printf("commit events    : %llu\n",
              (unsigned long long)stats->commit_count);
  std::printf("errors           : %llu\n",
              (unsigned long long)stats->error_count);
  std::printf("rerank mean (us) : %llu\n", (unsigned long long)mean_us);
  std::printf("rerank max  (us) : %u\n", stats->rerank_latency_max_us);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 1;
  }

  std::string cmd = argv[1];
  if (cmd == "help" || cmd == "-h" || cmd == "--help") {
    PrintUsage();
    return 0;
  }

  CommonArgs common;
  common.addr = dafeng::GetDaemonAddress();
  std::string code, ctx, cands, app, text;
  int history_limit = 20;
  int min_freq = 2;
  bool dry_run = false;

  for (int i = 2; i < argc; ++i) {
    if (TakeArg(i, argc, argv, "--addr", &common.addr)) continue;
    if (TakeArgInt(i, argc, argv, "--timeout", &common.timeout_ms)) continue;
    if (TakeArg(i, argc, argv, "--code", &code)) continue;
    if (TakeArg(i, argc, argv, "--ctx", &ctx)) continue;
    if (TakeArg(i, argc, argv, "--cands", &cands)) continue;
    if (TakeArg(i, argc, argv, "--app", &app)) continue;
    if (TakeArg(i, argc, argv, "--text", &text)) continue;
    if (TakeArgInt(i, argc, argv, "--limit", &history_limit)) continue;
    if (TakeArgInt(i, argc, argv, "--min-freq", &min_freq)) continue;
    if (std::strcmp(argv[i], "--dry-run") == 0) { dry_run = true; continue; }
    std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
    PrintUsage();
    return 1;
  }

  // Quiet down debug logging unless the user asked.
  dafeng::SetLogLevel(dafeng::LogLevel::kWarn);

  if (cmd == "ping") return CmdPing(common);
  if (cmd == "stats") return CmdStats(common);
  if (cmd == "history") return CmdHistory(history_limit);
  if (cmd == "learn") {
    return CmdLearn(static_cast<uint32_t>(min_freq < 1 ? 1 : min_freq),
                     dry_run);
  }
  if (cmd == "rerank") {
    if (code.empty() || cands.empty()) {
      std::fprintf(stderr, "rerank requires --code and --cands\n");
      return 1;
    }
    return CmdRerank(common, code, ctx, cands, app);
  }
  if (cmd == "commit") {
    if (code.empty() || text.empty()) {
      std::fprintf(stderr, "commit requires --code and --text\n");
      return 1;
    }
    return CmdCommit(common, code, text, ctx);
  }

  std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
  PrintUsage();
  return 1;
}
