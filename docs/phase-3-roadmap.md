# Phase 3 — From "works on Kevin's machine" to "works for everyone"

Phase 2 made the engine real (132+ tests, end-to-end learning loop).
Phase 3 is the *product* phase — packaging, distribution, real LLM
quality, cross-platform, and the experience for someone who doesn't
have the source tree open.

## 3.1 — Packaging + project hygiene · ✅ shipped (this commit-set)

- LICENSE (Apache-2.0) + NOTICE for third-party deps
- `install.sh` one-command bootstrap from a fresh Mac
- launchd LaunchAgent (auto-start, auto-restart)
- GitHub Actions CI (macos-14 build/test/MLX-on smoke + shellcheck +
  pyflakes)
- User-facing README rewrite + cheat sheet
- `dafeng-cli` rounded out: `ping`, `stats`, `rerank`, `commit`,
  `history`, `learn`
- VERSION + CHANGELOG + release-process doc

### What 3.1 does NOT include

- Pre-built binaries / `.pkg` installer / Homebrew tap → 3.2 once
  there's a real LLM payload to bundle.
- Code-signing + notarization → 3.2.

## 3.2 — Real LLM in the reranker · 🚧 next

This is the differentiating piece. Right now `--backend mlx` is wired
end-to-end but scoring delegates to the deterministic backend. The
production reranker should:

- Tokenize the prompt + each candidate as a continuation
- Run a single forward pass over the joint sequence
- Extract per-candidate logits (not generate — extract)
- Score each candidate by the sum of its token logprobs
- Stay under the 30 ms P99 budget (current Qwen 2.5 0.5B 4-bit
  numbers from MLX literature put a 20-token forward pass at ~25 ms
  on M2 — tight but doable)

Concrete subtasks:

- 3.2.A — Tokenizer integration (sentencepiece or Qwen2's BPE).
- 3.2.B — MLX transformer load (`safetensors` reader using
  `mlx::core::load_safetensors`).
- 3.2.C — Forward-pass shape: prefill prompt KV-cache once per request,
  then score each candidate's first-token logit (or sum-of-logprobs for
  multi-token candidates).
- 3.2.D — Latency tuning: candidate batch shape, attention masks,
  4-bit quant kernels.
- 3.2.E — Quality gate: a small held-out set of (prompt, expected-rank)
  pairs that asserts P@1 ≥ 70 % vs. wubi86 baseline (DESIGN §8 target).
- 3.2.F — Pre-built `.pkg` distributable: bundles the binary +
  pre-quantized model + an opt-in download fallback.

### Why this is the key differentiator

Without real LLM scoring, dafeng's value over vanilla rime-wubi is:
- Daemon-driven user-dict learning (the wubi-aware split-typed phrase
  detection from Phase 2.4)
- Top-N protection
- Plumbing for Phase 3.3+ (Windows, GUI, sync)

That's worth shipping (and we are shipping it as 0.1.0). But the
*"intelligently aware of context"* claim in DESIGN §1 isn't true until
3.2 lands. The reranker calls `client:rerank()` for every keystroke
already; today the daemon runs deterministic logic; Phase 3.2 swaps
that one function for real model inference.

## 3.3 — Windows port

Source tree is cross-platform-ready (no `#include <sys/un.h>` in
public headers, `if(WIN32)` branches in CMake, stub `endpoint_win.cc`
and `paths_win.cc`). Concrete missing pieces:

- 3.3.A — `endpoint_win.cc` real implementation: Named Pipes
  (`CreateNamedPipe` server side, `CreateFile` client side, plus
  IOCP for Accept-loop interruption).
- 3.3.B — `paths_win.cc`: `SHGetFolderPath` for `%APPDATA%`,
  named-pipe address (`\\.\pipe\dafeng-daemon`).
- 3.3.C — Auto-start: scheduled task (`schtasks /create /tn dafeng …`)
  via PowerShell installer.
- 3.3.D — Weasel (Windows librime frontend) integration: same Lua
  bridge approach as Squirrel but with `.dll` + dllexport already
  applied (existing `__declspec(dllexport)` in
  `dafeng_lua_bridge.c`'s `luaopen_*` symbol).
- 3.3.E — CI matrix: add a Windows runner.

The Lua filter, dafeng_filter.lua, and the daemon binary itself need
**no changes** — they're already platform-portable.

## 3.4 — GUI inspector / editor

Currently `dafeng-cli history` and `dafeng_learned.dict.yaml` are the
only ways to inspect / curate learned phrases. A small SwiftUI
panel on macOS that:

- Lists recent commits (with timestamps + code)
- Lists current learned phrases with frequency
- Lets the user delete a phrase (auto-rebuilds dict + redeploys RIME)
- Shows daemon stats live

…would lower the bar significantly for non-technical users.

This is a separable, optional piece. The CLI keeps full power.

## 3.5 — Pre-built installer + Homebrew tap

Once 3.2 lands and the value prop is real:

- `pkgbuild` a `.pkg` installer that bundles dafeng-daemon, the Lua
  bridge `.so`, schemas, and the Qwen 2.5 0.5B model.
- Code-sign and notarize for macOS 14+ (avoid "unidentified developer"
  blocking).
- `homebrew-dafeng` tap with a cask. `brew install --cask dafeng-wubi`
  runs the installer.

## 3.6 — Telemetry-free crash reporting

When something breaks for a user, they shouldn't have to dig through
logs. Local-first model:

- Daemon writes a structured crash report to
  `~/Library/Logs/dafeng-daemon.crash.json` on `SIGABRT` / `terminate_handler`
- `dafeng-cli diagnose` bundles the crash report + recent logs +
  versions into a single file the user can attach to a GitHub issue
- **No** automatic upload anywhere.

---

## Out-of-scope reminders

These are deliberately not on the roadmap:

- Cloud sync of `history.db` (CLAUDE.md §2 — never)
- Account / auth / activation
- Built-in OCR / handwriting input
- Mobile (iOS — different IME architecture)
- LLM-as-everything (CLAUDE.md §1 — must always degrade to wubi
  when LLM is slow/down)
