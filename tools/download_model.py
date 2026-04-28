#!/usr/bin/env python3
r"""Download a Qwen 2.5 0.5B Instruct model for the dafeng reranker.

Two backends, two formats:

    --backend llama_cpp  (default — Phase 3.2 production path)
        GGUF format. Defaults to Qwen2.5-0.5B-Instruct-Q4_K_M.gguf
        (~390 MB) from Qwen/Qwen2.5-0.5B-Instruct-GGUF.

    --backend mlx
        MLX format (.npz / safetensors). Defaults to
        mlx-community/Qwen2.5-0.5B-Instruct-4bit.

Phase 3.2 ships the daemon's llama.cpp backend wiring; the model
itself is a 4-digit-MB blob that you fetch separately so we never
auto-download without consent (CLAUDE.md privacy/network rules).

Usage examples:

    python3 tools/download_model.py
    python3 tools/download_model.py --backend mlx
    python3 tools/download_model.py --dest "~/Library/Application Support/Dafeng/models"
    python3 tools/download_model.py --dry-run

The path printed at the end is what to pass as `--model-path` to
dafeng-daemon. Why this is a script and not a CMake target:
    - Pulls hundreds of MB. Should be a deliberate user action.
    - HuggingFace tokens may be required if a model goes private.
    - Easy to swap the source URL for a custom-quantized model later.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

DEFAULT_DEST = Path.home() / "Library/Application Support/Dafeng/models"

BACKENDS = {
    "llama_cpp": {
        "repo": "Qwen/Qwen2.5-0.5B-Instruct-GGUF",
        "include_pattern": "qwen2.5-0.5b-instruct-q4_k_m.gguf",
        "expected_file": "qwen2.5-0.5b-instruct-q4_k_m.gguf",
        "size": "~390 MB",
    },
    "mlx": {
        "repo": "mlx-community/Qwen2.5-0.5B-Instruct-4bit",
        "include_pattern": None,  # whole repo
        "expected_file": None,
        "size": "~400 MB",
    },
}


def have_hf_cli() -> str | None:
    """Return the path to the `hf` CLI, or None if missing.

    The recent huggingface-cli rewrite replaced the legacy
    `huggingface-cli download` command with `hf download`. We use the
    new one and fall back to `huggingface-cli` only if `hf` is missing.
    """
    return shutil.which("hf") or shutil.which("huggingface-cli")


def ensure_hf_cli() -> str:
    cli = have_hf_cli()
    if cli is not None:
        return cli
    print(
        "Hugging Face CLI is required.\n"
        "Install with one of:\n"
        "    brew install huggingface-cli\n"
        "    pipx install --include-deps 'huggingface_hub[cli]'\n"
        "    pip install --user 'huggingface_hub[cli]'\n",
        file=sys.stderr,
    )
    sys.exit(1)


def download(cli: str, repo: str, include_pattern: str | None, dest: Path,
             dry_run: bool) -> int:
    dest.mkdir(parents=True, exist_ok=True)
    target_dir = dest / repo.split("/")[-1]
    cli_name = Path(cli).name
    if cli_name == "hf":
        # `hf download REPO_ID [FILENAMES]... --local-dir DIR`
        cmd = [cli, "download", repo, "--local-dir", str(target_dir)]
        if include_pattern:
            cmd += ["--include", include_pattern]
    else:
        # legacy `huggingface-cli download REPO_ID --local-dir DIR`
        cmd = [cli, "download", repo, "--local-dir", str(target_dir)]
        if include_pattern:
            cmd += ["--include", include_pattern]
    print("+", " ".join(cmd))
    if dry_run:
        return 0
    return subprocess.call(cmd)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--backend", choices=sorted(BACKENDS.keys()),
                   default="llama_cpp",
                   help="Which model format to fetch (default: llama_cpp/GGUF)")
    p.add_argument("--repo", default=None,
                   help="Override the HF repo id")
    p.add_argument("--dest", default=str(DEFAULT_DEST),
                   help="Destination directory")
    p.add_argument("--dry-run", action="store_true",
                   help="Print the command, don't run it")
    args = p.parse_args(argv)

    cfg = BACKENDS[args.backend]
    repo = args.repo or cfg["repo"]
    print(f"backend     : {args.backend}")
    print(f"repo        : {repo}")
    print(f"size (est.) : {cfg['size']}")

    cli = ensure_hf_cli() if not args.dry_run else (have_hf_cli() or "hf")

    dest = Path(args.dest).expanduser()
    rc = download(cli, repo, cfg["include_pattern"], dest, args.dry_run)
    if rc != 0:
        print(f"download failed (rc={rc})", file=sys.stderr)
        return rc

    final_dir = dest / repo.split("/")[-1]
    if cfg["expected_file"]:
        model_path = final_dir / cfg["expected_file"]
    else:
        model_path = final_dir  # MLX models are directories

    print()
    print(f"Done. Model is at:\n    {model_path}")
    print()
    print("Run the daemon with:")
    if args.backend == "llama_cpp":
        print("    ./build-llama/src/daemon/dafeng-daemon \\")
        print(f"        --backend llama_cpp --model-path {model_path}")
    else:
        print("    ./build-mlx/src/daemon/dafeng-daemon \\")
        print(f"        --backend mlx --model-path {model_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
