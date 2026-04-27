# Phase 2.2 — Reranker

This phase moves from "any reranker" (Phase 2.1's mock-reverse) to a real,
context-sensitive reranker with a swap point for MLX-backed inference.

## What Phase 2.2 ships

| Component | Status |
|---|---|
| `IRerankService` factory pattern | landed in 2.2.A |
| `MakeDeterministicRerankService()` — scored, ctx-aware, fast | landed in 2.2.A |
| `MakeMLXRerankService()` C++ surface + factory contract | landed in 2.2.B |
| MLX C-API smoke test (load + multiply + readback) | landed in 2.2.B |
| `--backend {mock,deterministic,mlx}` daemon flag | landed in 2.2.C |
| `tests/benchmarks/reranker_latency` + ctest budget gate | landed in 2.2.D |
| `tools/download_model.py` for Qwen 2.5 0.5B 4-bit | landed in 2.2.E |

## What Phase 2.2 does NOT ship

The transformer forward pass. With MLX wired up + a model file present,
the smoke test confirms libmlx is healthy, and the service initializes,
but the *scoring math* is still the deterministic algorithm. This is
called out in `RerankResponse.model_version`:

| version | meaning |
|---|---|
| 0 | mock-reverse (Phase 2.1) |
| 1 | deterministic — bigram + length + hash tie-break |
| 2 | mlx wrapper, scoring delegated to deterministic |

`model_version` lets Phase 2.3 observability tell which backend served
each request, and lets a future Phase 2.2.F drop in real LLM inference
(version 3+) without changing the IRerankService contract.

## Backend selection

```bash
# Default — fast, deterministic, no model file needed.
dafeng-daemon --backend deterministic

# Phase 2.1 compatibility / debugging.
dafeng-daemon --backend mock

# MLX path. Requires DAFENG_ENABLE_MLX=ON at build, mlx + mlx-c installed,
# and a real MLX-format model at the given path.
dafeng-daemon --backend mlx --model-path ~/Library/Application\ Support/Dafeng/models/Qwen2.5-0.5B-Instruct-4bit/model.safetensors
```

## Building with MLX

```bash
brew install mlx-c     # pulls mlx as a dependency
cmake -B build-mlx -G Ninja -DDAFENG_ENABLE_MLX=ON
cmake --build build-mlx -j
```

CMake auto-detects mlx-c at `/opt/homebrew/include/mlx/c/` and links
`libmlxc.dylib` + `libmlx.dylib`. If either is missing you get a clear
warning at configure time and the build falls back to the stub TU.

## Getting the model

```bash
python3 tools/download_model.py
```

This pulls `mlx-community/Qwen2.5-0.5B-Instruct-4bit` (~400 MB) into
`~/Library/Application Support/Dafeng/models/` via `huggingface-cli`.

The script never runs implicitly — it's an opt-in user action so the
daemon can never surprise-download anything (CLAUDE.md privacy rules).

## Reference numbers

```
$ ./build/tests/benchmarks/reranker_latency --iterations 10000 --backend deterministic
reranker_latency: backend=deterministic iterations=10000 candidates=10
  mean :      3 us
  P50  :      3 us
  P95  :      6 us
  P99  :      7 us
  P999 :      9 us
  max  :     37 us
PASS: P99 7 us <= budget 30000 us       <- DESIGN §3 LLM budget
```

The deterministic backend is ~4 orders of magnitude under budget; the
budget exists because real LLM inference is what's expected to consume
most of the 30 ms. When the transformer forward pass lands, this benchmark
becomes the canary that catches regressions on real model inference.

## Tests landed in 2.2

- `mock_reranker_test` (7) — Phase 2.1 audit backfill
- `reranker_deterministic_test` (12) — bigram favor wins, repetition
  avoidance, determinism over 50 calls, latency-under-1ms, scores-sorted,
  MLX-factory-null contract, etc.
- `RerankerLatencyBudgetDeterministic` ctest gate — P99 < 30 ms

## Next: Phase 2.3

Wire the reranker into the daemon's default startup, add observability
(miss rate, latency histogram), implement top-N protection (per
DESIGN §9.2 R5 — high-frequency words skip rerank to keep muscle memory).
