# Changelog

All notable changes to dafeng-wubi follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- (placeholder for next release)

## [0.1.0] — 2026-04-28

First public-facing release. End-to-end working five笔 IME on macOS:
type characters → daemon learns combinations → re-deploy → learned
phrases appear as candidates next to stock wubi86 entries. Verified
live on macOS 15.6 / Squirrel 1.1.2.

### Added (Phase 2 — engine)

- **IPC backbone**: length-prefixed MessagePack over Unix domain socket;
  P99 14 µs (budget 5 ms). Cross-platform `Endpoint` abstraction
  (Windows backend stubbed, Mac UDS implemented).
- **Reranker abstraction**: `IRerankService` with three pluggable
  backends (`mock`, `deterministic`, `mlx`). DeterministicReranker
  uses a curated bigram table + length / repetition heuristics; P99
  3 µs (budget 30 ms).
- **MLX backend wiring**: builds against `mlx-c` (brew). Smoke-tests
  the framework on init; scoring currently delegates to deterministic
  pending real transformer forward pass (Phase 3.2).
- **Stats endpoint**: monotonic counters for rerank / ping / commit /
  errors + latency mean+max + uptime, exposed via `dafeng-cli stats`.
- **Top-N protection**: schema-configurable `protect_top_n` (default 3)
  pins librime's leading candidates against muscle-memory drift.
- **SQLite history store** (`history.db`): WAL mode, `0600` perms,
  privacy-guarded path validator that refuses to open outside the
  user's data directory.
- **Personal n-gram counter**: time-aware chained extraction (wubi
  commits one char per event — chain consecutive commits within 2 s
  into a phrase, then emit bigrams).
- **Frequency-based new-word discovery**: configurable threshold,
  ASCII-only / repeat-noise filters.
- **Wubi 86 phrase encoder** (`WubiCodec`): parses `wubi86.dict.yaml`,
  applies the standard rules (2-char `Aa+Ab+Ba+Bb`, 3-char
  `Aa+Ba+Ca+Cb`, 4+ char `Aa+Ba+Ca+Za`).
- **`learned_words.yaml`** (human-readable) **+** `dafeng_learned.dict.yaml`
  (RIME-loadable) generators.
- **Combined dict** `wubi86_dafeng.dict.yaml` that imports `wubi86` +
  `dafeng_learned` so learned phrases appear as standard wubi candidates.
- **libgit2 sync** for `userdata/` cross-device, with push **defaulting
  to off** (CLAUDE.md privacy rule).
- **`LearningRound` orchestrator** stitching all of the above; runs
  in ~30 ms on a 60-commit history.

### Added (Phase 3.1 — packaging + CI)

- **`./install.sh`** — single-command bootstrap from a fresh macOS
  (brew packages, Squirrel, wubi86 dict, build, deploy, launchd).
- **launchd LaunchAgent** (`packaging/macos/com.dafeng.daemon.plist`
  + `install_launchagent.sh`) for daemon auto-start + auto-restart.
- **GitHub Actions CI**: build + test on macos-14, MLX-on smoke build,
  shellcheck for shell scripts, pyflakes for tools/.
- **`dafeng-cli`**: `ping`, `stats`, `rerank`, `commit`, `history`,
  `learn` subcommands.
- **License + project hygiene**: Apache-2.0 LICENSE + NOTICE,
  CONTRIBUTING.md, docs/cheat-sheet.md.

### Tools

- `tools/download_model.py` — opt-in HuggingFace fetcher for Qwen 2.5
  0.5B 4-bit (Phase 3.2 will consume).
- `tools/rime_redeploy.sh` — manual RIME redeploy trigger.

### Tests + benchmarks

144 tests across:
- **Unit**: protocol codec, endpoint UDS, logging, all reranker
  backends, history store (privacy guard test is load-bearing),
  ngram persistence, new-word discovery, user dict YAML, libgit2
  sync, learning round, wubi codec.
- **Integration**: real-UDS DafengClient ↔ Server, Lua bridge ↔
  daemon end-to-end (with vendored Lua 5.4), top-N protection in pure
  Lua against fake clients.
- **Benchmarks**: `IpcPingpongBudget` (P99 < 5 ms), `RerankerLatencyBudgetDeterministic` (P99 < 30 ms) — wired into ctest as gates.

### Privacy guarantees (test-locked)

- `history.db` cannot be opened outside the user's data directory
  (`PrivacyGuardRejectsEscape` test fails the build on regression).
- `history.db` is `0600` on disk (`FileMode0600AfterOpen` test).
- Git push refuses without explicit opt-in
  (`PushDisabledByDefaultIsNoop` exercises bogus URL + asserts < 100 ms
  return — proves no DNS / network attempt).

### Verified live (Kevin's machine, macOS 15.6 + Squirrel 1.1.2)

- Type `xu` → 弹, `pw` → 窗 four times within a few seconds. Run
  `dafeng-cli learn`. Re-deploy Squirrel. Type `xupw`. → 弹窗 appears
  as a candidate.
- Same flow validated for `ypwh` → 这个, `trwu` → 我们, `khlg` → 中国,
  `wygd` → 今天.

### Known gaps (deferred)

- `reranker_mlx.cc` runs the MLX smoke test but transformer inference
  is not yet implemented; `model_version=2` distinguishes the path.
  → Phase 3.2.
- Learning round is on-demand via `dafeng-cli learn`, no scheduler.
  → Phase 3.x.
- Windows backend (`endpoint_win.cc`, `paths_win.cc`) is a build-time
  stub; the rest of the source tree compiles cross-platform.
  → Phase 3.3.
- `learned_words.yaml` editing is by hand; no GUI. → Phase 3.4.
