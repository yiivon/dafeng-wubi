#!/usr/bin/env python3
r"""Download a Qwen 2.5 0.5B Instruct model in MLX format.

Phase 2.2 ships the daemon's MLX backend wiring; the model itself is a
~400 MB blob that Kevin should fetch separately so we never auto-download
without consent (CLAUDE.md privacy/network rules).

Usage:
    python3 tools/download_model.py
    python3 tools/download_model.py --dest "~/Library/Application Support/Dafeng/models"
    python3 tools/download_model.py --dry-run

The output path is what to pass as `--model-path` to dafeng-daemon.

Why this is a script and not a CMake target:
    - Pulls ~400 MB over the network. Should be a deliberate user action.
    - HuggingFace tokens may be required if model goes private. We don't
      want a sudden CI break to surprise anyone.
    - Easy to swap the source URL for a custom-quantized model later.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

DEFAULT_REPO = "mlx-community/Qwen2.5-0.5B-Instruct-4bit"
DEFAULT_DEST = Path.home() / "Library/Application Support/Dafeng/models"


def have_huggingface_cli() -> bool:
    return shutil.which("huggingface-cli") is not None


def ensure_huggingface_cli() -> None:
    if have_huggingface_cli():
        return
    print(
        "huggingface-cli is required.\n"
        "Install with:\n"
        "    pipx install --include-deps huggingface_hub[cli]\n"
        "or:\n"
        "    pip install --user 'huggingface_hub[cli]'\n",
        file=sys.stderr,
    )
    sys.exit(1)


def download(repo: str, dest: Path, dry_run: bool) -> int:
    dest.mkdir(parents=True, exist_ok=True)
    cmd = [
        "huggingface-cli",
        "download",
        repo,
        "--local-dir",
        str(dest / repo.split("/")[-1]),
    ]
    print("+", " ".join(cmd))
    if dry_run:
        return 0
    return subprocess.call(cmd)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--repo", default=DEFAULT_REPO,
                   help=f"HF repo id (default: {DEFAULT_REPO})")
    p.add_argument("--dest", default=str(DEFAULT_DEST),
                   help="destination directory")
    p.add_argument("--dry-run", action="store_true",
                   help="print the command, don't run it")
    args = p.parse_args(argv)

    if not args.dry_run:
        ensure_huggingface_cli()

    dest = Path(args.dest).expanduser()
    rc = download(args.repo, dest, args.dry_run)
    if rc != 0:
        print(f"download failed (rc={rc})", file=sys.stderr)
        return rc

    final = dest / args.repo.split("/")[-1]
    print()
    print(f"Done. Model is at:\n    {final}")
    print()
    print("Run the daemon with:")
    print(f"    ./build-mlx/src/daemon/dafeng-daemon \\")
    print(f"        --backend mlx --model-path {final}/model.safetensors")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
