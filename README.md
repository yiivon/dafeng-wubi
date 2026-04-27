# 大风五笔 (Dafeng Wubi)

A cross-platform, learning, predictive Wubi input method.

> **Status: Phase 2.1 — IPC skeleton complete.** Daemon ↔ IME plumbing
> ships and is benchmarked. No LLM yet; the reranker is a deterministic
> mock that reverses the candidate list end-to-end.

## Targets

- **macOS** (Apple Silicon and Intel) — primary, Phase 2.1 fully working.
- **Windows** 10/11 — secondary. The C++ source tree is cross-platform-clean
  (Endpoint / paths abstractions, no platform headers in `include/dafeng/`,
  CMake `if(WIN32)` branches in place), but the Windows backends are stubs
  pending the dedicated Windows phase.

## Architecture, in one paragraph

Two processes. The IME process (Squirrel/Weasel + librime + a thin Lua
filter) handles every keystroke and is allowed zero LLM-induced latency.
The `dafeng-daemon` process owns the model, the input history, and the
learning pipeline. They talk over a length-prefixed MessagePack channel —
Unix domain socket on macOS, named pipe on Windows. If the daemon is slow
or absent, the filter falls back to plain RIME within a hard 30 ms timeout.

Read [DESIGN.md](DESIGN.md) for the full design and [CLAUDE.md](CLAUDE.md)
for the non-negotiable engineering rules.

## Build (macOS)

Toolchain: `cmake ≥ 3.20`, `ninja`, Apple clang 15+. The rest is fetched
via submodules and `FetchContent` (msgpack-c, GoogleTest, Lua 5.4).

```bash
brew install cmake ninja
git submodule update --init --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

Phase 2.1 ships **38 tests** across 4 suites:
- `protocol_test` — wire-format codec round-trip + framing edge cases (14)
- `endpoint_test` — UDS listen / connect / timeout / shutdown (8)
- `client_daemon_test` — DafengClient ↔ Server end-to-end (8)
- `lua_bridge_test` — Lua C bridge against an in-process daemon (7)
- `IpcPingpongBudget` — P99 < 5 ms gate from DESIGN §3 (1)

## Run the daemon

```bash
./build/src/daemon/dafeng-daemon --foreground --log-level info
```

It listens on `~/Library/Application Support/Dafeng/daemon.sock` by
default. Override with `--addr <path>` or `DAFENG_DAEMON_ADDR=<path>`.

## Smoke-test from the command line

In another terminal, with the daemon running:

```bash
./build/src/cli/dafeng-cli ping
# pong in 167 us

./build/src/cli/dafeng-cli rerank --code ggll --cands 感,工,一,国
# order: 国 一 工 感         <- mock reverse correctly applied

./build/src/cli/dafeng-cli commit --code wo --text 我 --ctx 今天
# commit sent (fire-and-forget)
```

## Benchmark

```bash
./build/tests/benchmarks/ipc_pingpong --iterations 10000 --p99-budget-us 5000
```

Reference numbers on M-series, in-process UDS:

```
mean :      9 us
P50  :      8 us
P95  :     12 us
P99  :     25 us       <- DESIGN §3 budget is 5000 us
```

The P99 budget gate is wired into `ctest` as `IpcPingpongBudget`.

## Live verification in Squirrel (manual)

See [docs/phase-2.1-verification.md](docs/phase-2.1-verification.md) for a
step-by-step Squirrel deploy + manual keystroke check.

## Privacy

`history.db` (raw input history) **never** leaves the machine and is never
committed. Only the derived, sanitized user dictionary in `userdata/` is
synced via a separate Git repository. See `DESIGN.md` §9.2 R3.

## Repository layout

```
include/dafeng/      — public headers (no platform code)
src/common/          — shared codec, IPC, paths, logging
src/client/          — DafengClient (C++ + C ABI)
src/daemon/          — dafeng-daemon executable + dafeng_daemon_core lib
src/cli/             — dafeng-cli debugging tool
src/plugin/          — librime Lua bridge + filter
schemas/             — RIME schema YAML
tests/{unit,integration,benchmarks}/
third_party/         — submodules (msgpack-c)
packaging/macos/     — deploy script for Squirrel
docs/                — long-form documentation
```

## License

TBD.
