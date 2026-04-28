#!/usr/bin/env bash
# install_launchagent.sh — install dafeng-daemon as a user-level
# LaunchAgent so it auto-starts on login and auto-restarts on crash.
#
# Idempotent: re-running updates the binary path and reloads.
#
# Usage:
#   ./packaging/macos/install_launchagent.sh
#   ./packaging/macos/install_launchagent.sh /path/to/dafeng-daemon
#
# Uninstall:
#   launchctl bootout gui/$(id -u)/com.dafeng.daemon
#   rm ~/Library/LaunchAgents/com.dafeng.daemon.plist

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PLIST_TEMPLATE="$REPO_ROOT/packaging/macos/com.dafeng.daemon.plist"
DAEMON_BIN="${1:-$REPO_ROOT/build/src/daemon/dafeng-daemon}"

if [[ ! -x "$DAEMON_BIN" ]]; then
  echo "[error] daemon binary not executable: $DAEMON_BIN" >&2
  echo "        Build first:  cmake --build build" >&2
  exit 1
fi

PLIST_DEST="$HOME/Library/LaunchAgents/com.dafeng.daemon.plist"
mkdir -p "$(dirname "$PLIST_DEST")"

# Template substitution: replace __BINARY_PATH__ and __HOME__.
sed -e "s|__BINARY_PATH__|$DAEMON_BIN|g" \
    -e "s|__HOME__|$HOME|g" \
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
