# 大风五笔 (Dafeng Wubi)

A cross-platform, learning, predictive Wubi input method.

> **Status: Phase 2.1 — IPC skeleton.** Daemon ↔ IME plumbing is being built.
> No LLM yet. No production-ready release.

## Targets

- macOS (Apple Silicon and Intel) — primary
- Windows 10/11 — secondary, after Mac stabilizes

## Architecture (one-paragraph version)

Two processes. The IME process (Squirrel/Weasel + librime + a thin Lua filter)
handles every keystroke and is allowed zero LLM-induced latency. The
`dafeng-daemon` process owns the model, the input history, and the learning
pipeline. They talk over a length-prefixed MessagePack channel — Unix domain
socket on macOS, named pipe on Windows. If the daemon is slow or absent, the
filter falls back to plain RIME within a 30 ms hard timeout.

Read [DESIGN.md](DESIGN.md) for the full design and [CLAUDE.md](CLAUDE.md) for
the non-negotiable engineering rules.

## Build (macOS, Apple Silicon)

```bash
git submodule update --init --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Toolchain: `cmake ≥ 3.20`, `ninja`, Apple clang 15+.

```bash
brew install cmake ninja
```

## Build (Windows)

Not yet wired up — Phase 2.1 is Mac-first. The CMake skeleton is already
cross-platform; the Windows-specific Endpoint backend lands in a later phase.

## Privacy

`history.db` (raw input history) **never** leaves the machine and is never
committed. Only the derived, sanitized user dictionary in `userdata/` is
synced via a separate Git repository. See `DESIGN.md` §9.2 R3.

## License

TBD.
