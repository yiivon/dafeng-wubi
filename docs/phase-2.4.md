# Phase 2.4 — Learning Subsystem

History capture, n-gram learning, frequency-based new-word discovery,
user-dict YAML generation, and Git sync — all the cold-path machinery
that turns "raw input events" into "the IME knows me."

## What Phase 2.4 ships

| Component | Source | Tests |
|---|---|---|
| `IHistoryStore` (SQLite, WAL, mode 0600, privacy guard) | `src/daemon/history_store.{h,cc}` | 11 unit |
| `INgramTable` + persistence + history-driven update | `src/daemon/ngram.{h,cc}` | 11 unit |
| `INewWordDiscovery` (frequency-statistical, 2/3-grams) | `src/daemon/new_word.{h,cc}` | 9 unit |
| `IUserDictGenerator` + RIME redeploy hook | `src/daemon/user_dict.{h,cc}` | 7 unit |
| `IGitSync` (libgit2, push opt-in) | `src/daemon/git_sync.{h,cc}` | 8 unit |
| `RunLearningRound` orchestrator | `src/daemon/learning_round.{h,cc}` | 4 unit |
| Helper: `tools/rime_redeploy.sh` | | manual |

## Privacy boundaries — load-bearing tests

CLAUDE.md is firm:
> `history.db` 含原始输入历史,**永远不能进 Git 仓库**,不能上传任何远程

Three guarantees, each backed by a test that fails the build on regression:

1. **history.db never escapes the data directory.**
   `MakeSqliteHistoryStore(db_path, data_root)` refuses any `db_path` not
   under `data_root`. Tested by `PrivacyGuardRejectsEscape` —
   `/tmp/dafeng-escape-test.db` against `data_root=/tmp/dafeng-history-XXX`
   returns `nullptr` and the file is never created.

2. **history.db is owner-read-only on disk.**
   `chmod 0600` immediately after `sqlite3_open_v2`, regardless of umask.
   Tested by `FileMode0600AfterOpen`.

3. **Git push never happens without explicit opt-in.**
   `GitSyncConfig::push_enabled` defaults to `false`. Tested by
   `PushDisabledByDefaultIsNoop` with a deliberately bogus URL — the
   call must return success in <100 ms (no DNS, no network attempt).

## Data flow (per `RunLearningRound`)

```
history.db (SQLite, local only)
    │
    ├─► ngram update                  ──►  userdata/personal_ngram.bin
    │       (UTF-8 bigrams)
    │
    └─► new-word discovery            ──►  userdata/learned_words.yaml
            (freq ≥ N, 2/3-grams,
             ASCII / repeats filtered)

userdata/                             ──►  git commit (always)
                                      ──►  git push   (only if BOTH
                                                push_enabled AND push_url
                                                are set)
```

The userdata directory is the boundary between "private" (history.db,
stays put) and "syncable" (everything in `userdata/`, all aggregates / no
raw input).

## What Phase 2.4 does NOT ship (deferred to Phase 3 / 4)

- **LLM-validated discovery.** DESIGN §4.4 calls for a Qwen 1.5B
  reasonableness gate on top of frequency statistics. The discovery
  layer ships without that hook so it's testable on machines without MLX.
  Phase 3 adds an `ILlmValidator` plugged into `RunLearningRound`.
- **Real RIME userdb compilation.** `learned_words.yaml` is a
  human-readable dump, not a binary `*.userdb`. The format conversion
  needs `installation_id` from the user's RIME setup and computed wubi
  codes — a Phase 3 packaging tool.
- **Periodic scheduler.** The learning round is invokable via direct API
  but the daemon doesn't run it on a timer yet — Phase 2 stays demand-
  driven so it never surprises a user mid-typing.
- **Vendored SQLite + libgit2.** Both are linked from the system
  toolchain (CLT for SQLite, brew for libgit2). Phase 3 packaging will
  vendor the amalgamation per DESIGN appendix A.

## Numbers from the in-tree tests

```
$ ctest --test-dir build
100% tests passed, 0 tests failed out of 132
```

Breakdown:
- protocol + endpoint + logging + paths     : 27
- mock + deterministic reranker + MLX        : 19
- DafengClient ↔ daemon integration          : 10
- Lua bridge + filter helper (compute_order) : 14
- history.db / ngram / new-word / user-dict / git-sync / learning-round
                                             : 50
- Lua bridge ↔ daemon (real round-trip)      : 7
- Benchmarks (IPC P99 < 5 ms, reranker P99 < 30 ms)
                                             : 2 ctest gates

## Next: Phase 3 (packaging)

- Auto-start: launchd plist on macOS, scheduled task on Windows.
- Vendored deps as submodules (sqlite-amalgamation, libgit2 source).
- Real userdb compiler.
- Windows port: Named Pipes endpoint, paths_win.cc real impl, MSVC
  build matrix.
- LLM validator hook for new-word discovery.
- Scheduled learning rounds (idle-detect + nightly).
