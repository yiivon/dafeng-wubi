# Contributing

Thanks for considering a contribution. Quick read first:

## Before you change code

1. **Read [DESIGN.md](DESIGN.md) and [CLAUDE.md](CLAUDE.md).** The latter is
   the project's "constitution" — a few rules are non-negotiable
   (30 ms IME hard timeout, history.db never leaves the machine, no
   unauthorized network calls, double-process boundary).

2. **One PR per logical change.** Small commits with clear "what + why"
   messages. We don't squash on merge — your local history matters.

3. **Tests must accompany behavior changes.** Anything in the hot path
   (rerank / commit / IPC) needs both unit + integration coverage. We
   don't accept "tested locally, can't add a test" for protocol or
   performance code.

## Build + test

```bash
brew install cmake ninja libgit2 mlx-c
git submodule update --init --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The test suite (currently 144 tests) runs in ~2 seconds. Performance
gates (`IpcPingpongBudget`, `RerankerLatencyBudgetDeterministic`) fail
the build if a regression breaks the DESIGN §3 budget.

## Style

- C++17 (not C++20 — librime isn't ready yet)
- `clang-format` per `.clang-format` (Google style + project tweaks)
- File / class names: `snake_case` for files, `PascalCase` for classes,
  `snake_case` for functions and locals, `kFoo` for constants
- No unnecessary comments — write the code clearly first; comment only
  the WHY of non-obvious decisions
- Header files use `#pragma once`

## Areas where help is wanted

In rough order of impact:

1. **Real Qwen 2.5 0.5B inference in `reranker_mlx.cc`** — the transformer
   forward pass + tokenizer + logit extraction over candidates. Right
   now MLX is wired but scoring delegates to the deterministic backend.
   See `docs/phase-2.2.md`.

2. **Windows port.** `endpoint_win.cc` and `paths_win.cc` are stubs
   today. The IPC abstraction is already in place — Named Pipe backend
   + correct user-data dir resolution + a Windows version of the
   launchd plist (scheduled task / service) are the missing pieces.

3. **Pinyin or other-layout schemas riding the same daemon.** The
   reranker, history, and learning pipeline are layout-agnostic; each
   schema just needs its own `*.schema.yaml` + WubiCodec equivalent
   (or skip phrase-encoding for layouts that already have natural
   multi-char codes).

4. **GUI for inspecting / editing learned phrases.** Currently CLI
   only. A SwiftUI sidebar that shows recent commits + lets the user
   delete entries from `dafeng_learned.dict.yaml` would lower the bar
   for non-developers.

## What we won't take

- Code that uploads any of `history.db` to a remote (CLAUDE.md §2)
- Telemetry / analytics / crash reporting that calls home
- Dependencies on cloud-only services in the IME hot path
- Force-pushes to main, signed-off-by waivers, or skip-CI commits

## Reporting bugs

- Reproducer is the best opening — a sequence of keys + the candidate
  window result.
- Attach `dafeng-cli stats`, `dafeng-cli history --limit 30`, and the
  daemon's `~/Library/Logs/dafeng-daemon.err.log` tail.
- macOS version, Squirrel version, brew package versions are useful.

## Conduct

Treat each other well. There's no formal CoC document yet — start with
"don't be a jerk" and we'll add the longer version when we need to.
