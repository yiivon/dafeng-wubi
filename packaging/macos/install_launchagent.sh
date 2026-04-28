#!/usr/bin/env bash
# install_launchagent.sh — install dafeng-daemon as a user-level
# LaunchAgent so it auto-starts on login and auto-restarts on crash.
#
# Idempotent: re-running updates the binary path and reloads.
#
# Usage:
#   ./install_launchagent.sh
#       Default: deterministic backend (no model file needed)
#
#   ./install_launchagent.sh /path/to/dafeng-daemon
#       Override the daemon binary path
#
#   DAFENG_BACKEND=llama_cpp \
#   DAFENG_MODEL_PATH=/path/to.gguf \
#   ./install_launchagent.sh /path/to/dafeng-daemon-llama
#       Use the llama.cpp backend with a specific GGUF model
#
# Uninstall:
#   launchctl bootout gui/$(id -u)/com.dafeng.daemon
#   rm ~/Library/LaunchAgents/com.dafeng.daemon.plist

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PLIST_TEMPLATE="$REPO_ROOT/packaging/macos/com.dafeng.daemon.plist"
DAEMON_BIN="${1:-$REPO_ROOT/build/src/daemon/dafeng-daemon}"
BACKEND="${DAFENG_BACKEND:-}"
MODEL_PATH="${DAFENG_MODEL_PATH:-}"

if [[ ! -x "$DAEMON_BIN" ]]; then
  echo "[error] daemon binary not executable: $DAEMON_BIN" >&2
  echo "        Build first:  cmake --build build" >&2
  exit 1
fi

PLIST_DEST="$HOME/Library/LaunchAgents/com.dafeng.daemon.plist"
mkdir -p "$(dirname "$PLIST_DEST")"

# Build the optional <string>...</string> entries for backend / model.
EXTRA_ARGS=""
if [[ -n "$BACKEND" ]]; then
  EXTRA_ARGS="<string>--backend</string><string>$BACKEND</string>"
fi
if [[ -n "$MODEL_PATH" ]]; then
  EXTRA_ARGS="$EXTRA_ARGS<string>--model-path</string><string>$MODEL_PATH</string>"
fi

# Template substitution.
sed -e "s|__BINARY_PATH__|$DAEMON_BIN|g" \
    -e "s|__HOME__|$HOME|g" \
    -e "s|__EXTRA_ARGS__|$EXTRA_ARGS|g" \
    "$PLIST_TEMPLATE" > "$PLIST_DEST"

LABEL="com.dafeng.daemon"

# bootout the previous instance if any (ignore errors — first install).
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true

# bootstrap the new one.
launchctl bootstrap "gui/$(id -u)" "$PLIST_DEST"

# Verify it's running.
sleep 0.5
if launchctl print "gui/$(id -u)/$LABEL" 2>/dev/null | grep -q 'state = running'; then
  echo "✓ dafeng-daemon launched (label: $LABEL)"
  echo "  plist:   $PLIST_DEST"
  echo "  binary:  $DAEMON_BIN"
  echo "  stdout:  ~/Library/Logs/dafeng-daemon.out.log"
  echo "  stderr:  ~/Library/Logs/dafeng-daemon.err.log"
else
  echo "[warn] daemon not in 'running' state after install — check logs."
  launchctl print "gui/$(id -u)/$LABEL" 2>&1 | head -20
fi
