# Phase 2.1 — Verification Guide

This document is a step-by-step way to convince yourself that Phase 2.1's
completion criteria from `DESIGN.md` §8 are actually met:

> Mac 上能看到 daemon 在影响候选顺序,且 30ms 超时降级正确生效;
> IPC 往返 P99 < 5ms。

## 1. Automated checks (no Squirrel needed)

```bash
# Toolchain.
brew install cmake ninja
git submodule update --init --recursive

# Build.
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j

# Run all 38 tests + the IPC P99 budget gate.
ctest --test-dir build --output-on-failure
```

Expected: `100% tests passed, 0 tests failed out of 38`.

The `IpcPingpongBudget` test fails the build if P99 IPC latency exceeds
5 ms (the contract from DESIGN §3.1).

## 2. Cross-process smoke test (no Squirrel needed)

Start the daemon in one terminal:

```bash
./build/src/daemon/dafeng-daemon --foreground --log-level debug
```

In another terminal:

```bash
# Liveness probe.
./build/src/cli/dafeng-cli ping

# Send a real rerank request.
./build/src/cli/dafeng-cli rerank --code ggll --ctx '你好' --cands '感,工,一,国'
# Expected output: "order: 国 一 工 感"
#   The mock reranker reverses the input list — proof that the daemon
#   is in the loop, not just RIME.

# Commit event (fire-and-forget — no reply).
./build/src/cli/dafeng-cli commit --code wo --text '我' --ctx '感'
```

In the daemon terminal you should see one log line per request.

## 3. Timeout / fallback verification

With the daemon stopped:

```bash
./build/src/cli/dafeng-cli ping --timeout 50
# "ping failed (timeout or daemon unreachable)"
# exit code: 1
```

This is the same path that `dafeng_filter.lua` takes: when the daemon is
absent, `client:rerank()` returns nil within the timeout, and the filter
yields the original RIME ordering. The IME never freezes, never errors.

## 4. Live Squirrel verification (manual, requires Squirrel)

Squirrel needs the standard wubi86 dictionary. If you don't have it:

```bash
# Easiest: install the full Wubi schema set via plum.
bash <(curl -fsSL https://git.io/rime-install) wubi
```

Then deploy our additions on top:

```bash
./build/src/daemon/dafeng-daemon --foreground &
./packaging/macos/deploy.sh
```

In the macOS input source menu:
1. Choose **Squirrel → Deploy** (let it finish — bottom-right notification).
2. Switch to the **大风五笔 86** input method.
3. Type `ggll`. The candidate window should show characters in **reversed**
   order versus stock wubi86 (because the Phase 2.1 reranker is a mock that
   reverses).
4. Stop the daemon (`fg`, then Ctrl-C). The daemon process exits.
5. Type `ggll` again. The candidate order should snap back to stock
   wubi86 — proof that the 30 ms timeout fallback works.

If candidates do not change order with the daemon up, or if the IME hangs
when the daemon is killed, that's a Phase 2.1 regression. Open an issue.

## 5. What's NOT in Phase 2.1

These land in later phases and are not expected to work yet:
- LLM-based reranking (Phase 2.2 — MLX integration)
- Real reranker accuracy (Phase 2.3)
- Commit logging to SQLite (Phase 2.4)
- New-word discovery / personal n-gram learning (Phase 2.4)
- Git sync of user data (Phase 2.4)
- Windows backend (separate phase)
- Auto-start via launchd (Phase 3 packaging)
