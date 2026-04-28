# Changelog

All notable changes to dafeng-wubi follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added (Phase 3.2 — real LLM)

- **llama.cpp backend** for the reranker (`reranker_llamacpp.cc` /
  `_stub.cc`). Loads a Qwen 2.5 0.5B GGUF, runs one forward pass per
  request, scores candidates by their first token's logit. KV cache
  reset per request to keep the implementation simple; tight enough
  that a 64-token prompt + scoring of ≤ 10 candidates fits well in
  the 30 ms P99 budget on M-series.
- **`--backend llama_cpp`** flag on `dafeng-daemon`. Sits alongside the
  existing `mock` / `deterministic` / `mlx` options. `RerankResponse.
  model_version=3` distinguishes this path so observability (`dafeng-cli
  stats`) reports it.
- **`tools/download_model.py`** rewritten to support both GGUF
  (`--backend llama_cpp`, default) and MLX (`--backend mlx`) formats.
  The script never auto-runs — explicit user action per CLAUDE.md.
- **Benchmark integration**: `tests/benchmarks/reranker_latency` accepts
  `--backend llama_cpp --model-path <gguf>` for live measurement.
- **`docs/phase-3.2.md`**: architectural rationale (llama.cpp over
  custom-MLX), failure-mode table, build/run/benchmark recipes.
- **`install.sh`**: adds `llama.cpp` to its brew packages list.

### Pivot

The MLX backend (Phase 2.2.B) remains in-tree as a working scaffold —
factory + smoke test + lifecycle — but the real-LLM scoring path is
now llama.cpp. MLX is parked as a future Apple-native backend for
someone with deep MLX expertise to fill in. Both backends produce
distinguishable `model_version` values so live runs are observable.

### Tests

146/146 passing in BOTH the default build (llama OFF, falls back to
deterministic) and the `-DDAFENG_ENABLE_LLAMA_CPP=ON` build. Two new
factory-contract tests assert nullptr returns on missing/empty
model paths regardless of build flag.

### Added (Phase 3.5 — distribution)

- **`.pkg` installer** (`packaging/macos/build-pkg.sh`):
  productbuild-style with TWO selectable components:
  - `dafeng-base.pkg` (required) — daemon, schemas, Lua bridge,
    deterministic backend
  - `dafeng-llm.pkg` (optional, **unchecked by default**) — when
    user ticks the box, postinstall downloads Qwen 2.5 0.5B Q4
    (~390 MB) from Hugging Face and reconfigures launchd to
    `--backend llama_cpp`
  - macOS Installer's "Customize" panel exposes the LLM checkbox
    natively
- **Homebrew formula** (`packaging/homebrew/dafeng-wubi.rb`):
  HEAD + stable, builds from source with LLAMA_CPP=ON, registers a
  `brew services` entry as alternative to LaunchAgent
- **`dafeng-cli setup`** subcommand: probes `/usr/local/share/dafeng`
  and `/opt/homebrew/share/dafeng` for the helper scripts shipped at
  install time, runs deploy + launchagent. `--backend llama_cpp`
  switches to LLM mode (errors actionably if model not present).
- **GitHub Actions release pipeline** (`.github/workflows/release.yml`):
  triggers on `v*` tag push; builds .pkg + .tar.gz on macos-14
  (arm64) and macos-13 (x86_64); generates SHA256SUMS; extracts
  matching CHANGELOG section as GitHub Release body.
- **`docs/phase-3.5.md`**: documents all three install paths, the
  customize-panel UX, Gatekeeper bypass, and what 3.5 does NOT
  include (signing, dylib bundling, auto-update, Windows port).
- README rewritten with three-channel install matrix.

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
