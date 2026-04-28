# Phase 3.2 — Real LLM via llama.cpp

Phase 3.1 shipped a v0.1.0 you could install. Phase 3.2 plugs in the
actual LLM that makes the rerank intelligent. This is the
differentiating feature in [DESIGN.md §1](../DESIGN.md) — context-aware
candidate ordering — and the missing payload that turns the deterministic
backend's "good engineering" into "good IME."

## Architectural choice: llama.cpp over custom MLX

Phase 2.2 wired an MLX backend skeleton; the smoke test passes (Metal
device + mlx-c integration), but the transformer forward pass was never
implemented. Doing it from scratch in C++ is weeks of work.

llama.cpp is already in DESIGN's tech stack as the Windows backend, and
its Qwen2 architecture support is mature. So Phase 3.2 makes llama.cpp
the **first-class** real-LLM path, with MLX kept as a future-Apple-native
backend for someone with deep MLX experience to fill in.

| | MLX (deferred) | llama.cpp (now) |
|---|---|---|
| Qwen2 forward pass implemented | no — would need to write | yes — built in |
| GGUF / safetensors loader | no | yes |
| Metal acceleration on Mac | yes (native) | yes (via Metal backend) |
| Cross-platform (Windows / Linux) | no | yes |
| Code we ship | huge | thin wrapper |
| Latency on Qwen 2.5 0.5B Q4 | (not measured) | < 30 ms achievable on M-series |

## What this phase ships

- `src/daemon/reranker_llamacpp.{cc,stub}` — full backend with model
  lifecycle, tokenizer, forward pass, logit-based scoring.
- `--backend llama_cpp` flag on `dafeng-daemon`.
- `RerankResponse.model_version=3` distinguishes this path from the
  deterministic (1) and MLX-wrapper (2) backends — observability via
  `dafeng-cli stats` shows which path served.
- Updated `tools/download_model.py` to fetch GGUF format by default.
- Updated `tests/benchmarks/reranker_latency` so you can measure
  on your hardware: `--backend llama_cpp --model-path <path>.gguf`.

## How scoring works

For each rerank request, **one forward pass** is run:

```
prompt = recent committed_text (≤ 256 tokens)
tokens = tokenize(prompt, add_bos=true)
llama_decode(tokens)
logits = llama_get_logits_ith(last_position)

for each candidate:
  candidate_first_token = tokenize(candidate)[0]
  score = logits[candidate_first_token]

sort candidates by score (stable)
```

This is single-token-prefix scoring. For multi-character candidates
(rare in wubi after the first char), only the first token's logit is
used. A future iteration could run per-candidate forwards with KV-cache
sharing for full-sequence scoring; the trade-off is latency vs. accuracy
on multi-char phrases. Single-token scoring is the right starting point
because (a) most wubi candidates are single characters, and (b) it stays
well within the 30 ms budget.

The KV cache is reset per request (`llama_memory_clear` after each
rerank). For typical short prompts (< 80 tokens), the prefill is fast
enough that incremental cache management isn't worth its bug surface.

## Build with llama.cpp on

```bash
brew install llama.cpp                    # provides libllama + headers
cmake -B build-llama -G Ninja \
      -DDAFENG_ENABLE_LLAMA_CPP=ON
cmake --build build-llama -j
ctest --test-dir build-llama --output-on-failure
# 146/146 tests pass; factory-contract tests assert nullptr behavior on
# missing model path / DAFENG_ENABLE_LLAMA_CPP=OFF.
```

## Get the model

```bash
python3 tools/download_model.py --backend llama_cpp
# Pulls Qwen/Qwen2.5-0.5B-Instruct-GGUF · qwen2.5-0.5b-instruct-q4_k_m.gguf
# (~390 MB) into ~/Library/Application Support/Dafeng/models/
```

The script never auto-runs — explicit user action per CLAUDE.md privacy.

## Run

```bash
./build-llama/src/daemon/dafeng-daemon \
    --backend llama_cpp \
    --model-path "$HOME/Library/Application Support/Dafeng/models/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q4_k_m.gguf"
```

In another terminal:

```bash
./build-llama/src/cli/dafeng-cli stats
# Should report: rerank model: llama_cpp (v3)
```

## Benchmark on your machine

```bash
./build-llama/tests/benchmarks/reranker_latency \
    --backend llama_cpp \
    --model-path /path/to/qwen2.5-0.5b-instruct-q4_k_m.gguf \
    --iterations 200 --warmup 20 \
    --p99-budget-us 30000
```

Expected on M-series (M2/M3/M4): P99 in the 15-25 ms range. The 30 ms
budget gate exits non-zero if breached so this is wired-up the same way
as `IpcPingpongBudget` and `RerankerLatencyBudgetDeterministic` — CI
catches model regressions when we eventually pin a release model.

## What this phase does NOT include

- Pre-built `.pkg` installer with bundled model — Phase 3.5.
- Quality regression suite (P@1 ≥ 70 % vs. wubi86 baseline) — needs
  a labeled test set, separate piece of work.
- Per-candidate forward passes for accurate multi-token candidate
  scoring — likely Phase 3.2.+ if first-token scoring proves limiting.
- Online learning that updates the LLM itself — out of scope.
- KV cache reuse across requests for the same context — micro-optim
  for when the same context comes back-to-back. Profile first.

## Failure modes & graceful degradation

`MakeLlamaCppRerankService` returns `nullptr` for any of:
- DAFENG_ENABLE_LLAMA_CPP=OFF at build time
- Empty `model_path`
- Model file missing on disk
- Model load failed (bad GGUF, OOM, etc.)
- Context init failed (Metal device unavailable)

`dafeng-daemon`'s `BuildReranker` catches this and falls back to
`MakeDeterministicRerankService()` with a warning log. The IME
**never** sees an error; degraded performance is a `dafeng-cli stats`
look away.

If the model loads but inference is slow (P99 > 30 ms), the IME-side
filter still respects the 30 ms hard cap — it'll time out the IPC and
the user gets librime's stock ordering. So even a misconfigured GGUF
can't hang typing.
