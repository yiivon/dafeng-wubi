#!/usr/bin/env bash
# rime_redeploy.sh — manual trigger to redeploy Squirrel after the daemon
# has updated learned_words.yaml. Phase 2.4.D ships this as a separate
# script (rather than auto-triggered from the daemon) because deploying
# RIME blocks the input briefly and we never want the daemon to surprise
# the user mid-typing.
#
# Run it:
#   $ ./tools/rime_redeploy.sh
#
# The script:
#   1. Touches RIME's user dir to invalidate caches.
#   2. Prints a short message asking the user to click "Deploy" in the
#      Squirrel input-source menu (we don't have a CLI deploy on macOS).
#
# It does NOT wipe userdb or restart Squirrel — those are destructive and
# require explicit user intent.

set -euo pipefail

RIME_DIR="${RIME_DIR:-$HOME/Library/Rime}"
DATA_DIR="${DAFENG_DATA_DIR:-$HOME/Library/Application Support/Dafeng}"

if [[ ! -d "$RIME_DIR" ]]; then
  echo "[error] RIME user directory not found: $RIME_DIR" >&2
  echo "        Install Squirrel and run it once first." >&2
  exit 1
fi

# Refresh the install file's mtime so RIME sees newer source files.
mkdir -p "$RIME_DIR"
touch "$RIME_DIR/installation.yaml"

# Mark the redeploy as serviced (clears the daemon's pending sentinel).
if [[ -f "$DATA_DIR/redeploy_requested" ]]; then
  rm -f "$DATA_DIR/redeploy_requested"
fi

cat <<EOF
Triggered. To finish:
  1. macOS input-source menu -> Squirrel -> Deploy
  2. Or restart Squirrel: Activity Monitor / killall Squirrel

If you were typing when this ran, your candidate window may flicker once
while RIME reloads schemas. That's expected.
EOF
