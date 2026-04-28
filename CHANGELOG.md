# Changelog

All notable changes to dafeng-wubi follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

(empty)

## [0.1.0] — 2026-04-28

First public-facing release. End-to-end working 五笔 IME on macOS with
LLM-backed reranking, learning, and a SwiftUI inspector. Phase 2 (engine)
+ Phase 3.1 (packaging) + 3.2 (real LLM) + 3.3 (Windows scaffold) + 3.4
(GUI) + 3.5+ (self-contained pkg) all in one tag.

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
  customize-panel UX, Gatekeeper bypass.
- README rewritten with three-channel install matrix.

### Added (Phase 3.5+ — self-contained .pkg)

- **dylib bundling via `dylibbundler`** in `build-pkg.sh`. Walks the
  dep graph from `dafeng-daemon` and `dafeng-cli`, copies every
  non-system dylib into `/usr/local/lib/dafeng/` and rewrites refs
  to `@executable_path/../lib/dafeng/...`. The `.pkg` now installs
  cleanly on machines with no Homebrew.
- **ggml runtime plugins** (`libggml-{cpu-*,metal,blas}.so`) bundled
  with `@rpath` install-names, soname symlinks (`libggml-base.0.dylib
  → libggml-base.0.10.0.dylib`), and rewritten `libomp.dylib`
  references.
- **Ad-hoc re-signing** (`codesign --force --sign -`) of every Mach-O
  after `install_name_tool`, fixing the silent SIGKILL (exit 137) on
  Apple Silicon when the original signature is invalidated.
- **Sibling-first plugin probe**: daemon resolves `<exe-dir>/../lib/dafeng/`
  via `_NSGetExecutablePath` and probes it before brew paths, so the
  bundle wins ggml registration regardless of install prefix.

### Added (Phase 3.3 — Windows port scaffold)

- **Real Named Pipe IPC** (`src/common/endpoint_win.cc`). Mirrors the
  Unix Domain Socket backend's contract: length-prefixed framing,
  per-call millisecond timeouts via OVERLAPPED + `WaitForSingleObject`
  + `CancelIoEx`, cross-thread `Shutdown()` via
  `WaitForMultipleObjects(connect_event, shutdown_event)`.
- **Real `%APPDATA%` paths** (`src/common/paths_win.cc`).
  `SHGetKnownFolderPath(FOLDERID_RoamingAppData)` for the data dir,
  per-user pipe name `\\.\pipe\dafeng-daemon-<sanitized-username>` to
  avoid cross-user collisions.
- **Cross-platform daemon main**: `SetConsoleCtrlHandler` for
  `CTRL_C/BREAK/CLOSE/LOGOFF/SHUTDOWN` events, drives the same
  `g_server->Shutdown()` exit path as POSIX `sigaction`.
- **Cross-platform `endpoint_test`**: `MakeTempEndpoint()` picks UDS
  vs pipe naming; `SetTestEnv` shims `setenv` ↔ `_putenv_s`. New
  Windows-specific `DefaultDaemonAddressIsPerUserPipe` test.
- **Windows CI job** (`.github/workflows/ci.yml`): `windows-latest`
  + MSVC + vcpkg sqlite3, builds the daemon + CLI + tests. Plugin /
  git_sync / mlx / llama_cpp deferred — they need Weasel headers and
  vcpkg libgit2 wiring.
- **`docs/phase-3.3.md`**: scaffold rationale, what's still TODO
  (Weasel integration, NSIS installer, Scheduled Task self-start,
  enabling git_sync via vcpkg, end-to-end smoke).

### Added (Phase 3.4 — SwiftUI inspector)

- **`apps/inspector/`** — native macOS app **大风五笔检查器**
  (Dafeng Inspector), SwiftPM, macOS 14+. Three tabs:
  - **状态** — polls `dafeng-cli stats` every 3 s; cards for
    uptime, backend, request counters, errors, mean / max latency.
    Latency card flips orange past 30 ms (DESIGN budget).
  - **输入历史** — opens `~/Library/Application Support/Dafeng/history.db`
    via SQLite C API in `SQLITE_OPEN_READONLY`. Filterable by
    text / context substring; configurable row limit.
  - **已学到的词** — parses `dafeng_learned.dict.yaml`, preserves
    YAML header verbatim, allows per-row delete with confirm
    alert + "请重新部署" banner.
- **`apps/inspector/build-app.sh`** — wraps the SwiftPM executable
  in a minimal `.app` bundle with Info.plist + ad-hoc codesign.
- **`packaging/macos/build-pkg.sh`** auto-builds the inspector and
  stages it into the `.pkg`'s `Applications/` payload, so users
  find **Dafeng Inspector** in `/Applications` after install.
- **`docs/phase-3.4.md`** — architecture notes, what's NOT included
  (menu-bar variant, weight-edit, daemon-control buttons, en.lproj).

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
  is not yet implemented; users go through the llama.cpp backend
  (Phase 3.2) instead. `model_version=2` distinguishes the path.
- Learning round is on-demand via `dafeng-cli learn`, no scheduler.
- **Windows installer not yet shipped.** The Phase 3.3 scaffold
  (Named Pipe IPC, `%APPDATA%` paths, console-ctrl handler, MSVC CI
  build) is in place, but Weasel integration, NSIS / WiX installer,
  and Scheduled Task self-start are still TODO. See `docs/phase-3.3.md`.
- Apple Developer ID signing + notarization for the `.pkg` is deferred
  (paid Apple Developer account + CI secret management).
- `Dafeng Inspector` does not yet ship a menu-bar variant or daemon
  start/stop controls; weight editing is delete-only.
