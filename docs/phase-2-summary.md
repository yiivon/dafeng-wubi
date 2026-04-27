# Phase 2 — Wrap-Up

Phase 2 is the from-scratch build of the dafeng daemon, IPC protocol,
librime integration, reranker abstraction, and learning subsystem. This
file is the index — each sub-phase has its own deeper-dive doc.

## What shipped

| Sub-phase | Theme | Doc | Status |
|---|---|---|---|
| 2.1 | IPC skeleton (no LLM) | [phase-2.1-verification.md](phase-2.1-verification.md) | ✅ all green |
| 2.2 | Reranker abstraction + MLX | [phase-2.2.md](phase-2.2.md) | ✅ all green |
| 2.3 | Reranker integration + observability + top-N protection | (this doc) | ✅ all green |
| 2.4 | Learning subsystem | [phase-2.4.md](phase-2.4.md) | ✅ all green |

## Test inventory (132 / 132 passing)

| Suite | Count | What it covers |
|---|---|---|
| `protocol_test` | 18 | wire-format codec, framing edges, large payloads, unknown-key forward-compat |
| `endpoint_test` | 8 | UDS listen/connect, ReadFull timeout, Shutdown unblocks Accept |
| `logging_test` | 6 | level filter, mute, format args, file:line in output |
| `mock_reranker_test` | 7 | Phase 2.1 mock-reverse contract |
| `reranker_deterministic_test` | 12 | bigram favor wins, repetition penalty, stability, latency |
| `history_store_test` | 11 | privacy guard escape rejection, 0600 mode, durability |
| `ngram_test` | 11 | UTF-8 walks, persistence atomicity, concurrent inserts |
| `new_word_test` | 9 | freq threshold, ASCII filter, repeat-noise filter |
| `user_dict_test` | 7 | YAML escape, atomic rename, redeploy sentinel |
| `git_sync_test` | 8 | init+commit+push-disabled-default load-bearing test |
| `learning_round_test` | 4 | end-to-end orchestrator |
| `IntegrationTest` (`client_daemon_test`) | 10 | DafengClient ↔ Server real UDS round-trip |
| `LuaBridgeFixture` | 14 | Lua C bridge + pure-Lua compute_order |
| `IpcPingpongBudget` (ctest) | 1 | gate: P99 IPC < 5 ms |
| `RerankerLatencyBudgetDeterministic` (ctest) | 1 | gate: P99 rerank < 30 ms |

## Performance (M-series, RelWithDebInfo, in-process UDS)

```
ipc_pingpong         : P99   13 us  (budget   5000 us)
reranker_latency     : P99    7 us  (budget  30000 us)
```

Two orders of magnitude under each budget — the headroom is for the
real LLM forward pass that lands when the deterministic backend is
swapped for full MLX inference.

## Privacy posture (CLAUDE.md §不可妥协的核心原则 #2)

| Claim | Where it's enforced | Test that fails on regression |
|---|---|---|
| `history.db` never leaves data dir | `MakeSqliteHistoryStore` privacy guard | `PrivacyGuardRejectsEscape` |
| `history.db` is owner-only | `chmod 0600` after open | `FileMode0600AfterOpen` |
| No git push without explicit opt-in | `GitSyncConfig::push_enabled = false` default | `PushDisabledByDefaultIsNoop` |
| No model auto-download | `tools/download_model.py` is opt-in | (manual, by design) |
| `userdata/` only carries aggregates | discovery output is freq + ts only | (review-only, doc'd) |

## Backend matrix

```
$ dafeng-daemon --backend mock           # Phase 2.1 reverse-permutation
$ dafeng-daemon --backend deterministic  # default — bigram + length + hash
$ dafeng-daemon --backend mlx --model-path PATH
                                          # MLX wrapper; scoring still
                                          # delegates to deterministic
                                          # until transformer fwd lands
```

`RerankResponse.model_version` distinguishes the path:
- `0` mock-reverse, `1` deterministic, `2` mlx-wrapped.

## What Phase 3 needs to start

- Vendored deps (sqlite amalgamation, libgit2 source) as submodules.
- Auto-start (launchd plist Mac, scheduled task Windows).
- Windows port: implement `endpoint_win.cc`, `paths_win.cc` properly.
- Real RIME userdb compiler from `learned_words.yaml`.
- LLM validator hook on new-word discovery.
- Scheduled learning rounds (idle / nightly).

## Repo layout (after Phase 2)

```
include/dafeng/
  api.h            — DAFENG_API export macro
  client.h         — DafengClient (C++)
  client_c.h       — C ABI for Lua bridge
  endpoint.h       — Connection / EndpointServer abstraction
  logging.h        — leveled stderr logger
  paths.h          — GetDataDir, GetDaemonAddress
  protocol.h       — encoders, decoders, framing
  types.h          — RerankRequest / Response / CommitEvent / Stats / ...

src/common/
  protocol.cc + endpoint_unix.cc + endpoint_win.cc
  paths_mac.cc + paths_win.cc + logging.cc

src/client/
  client.cc + client_c.cc

src/daemon/
  main.cc + server.{h,cc}
  services.h
  mock_reranker.cc + reranker_deterministic.cc
  reranker_mlx.cc + reranker_mlx_stub.cc
  history_store.{h,cc}
  ngram.{h,cc}
  new_word.{h,cc}
  user_dict.{h,cc}
  git_sync.{h,cc} + git_sync_stub.cc
  learning_round.{h,cc}

src/cli/
  main.cc                            — dafeng-cli {ping,rerank,commit,stats}

src/plugin/
  dafeng_lua_bridge.c                — librime Lua C extension
  dafeng_filter.lua                  — librime filter
  dafeng_rerank.lua                  — pure-Lua reordering helper

schemas/wubi.schema.yaml             — librime schema

tests/{unit,integration,benchmarks}/

tools/
  download_model.py                  — opt-in HF model fetcher
  rime_redeploy.sh                   — manual redeploy trigger

packaging/macos/deploy.sh            — Squirrel deploy

docs/
  phase-2.1-verification.md
  phase-2.2.md
  phase-2.4.md
  phase-2-summary.md   (this file)
```
