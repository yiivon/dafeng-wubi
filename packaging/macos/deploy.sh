#!/usr/bin/env bash
# deploy.sh — copy schema, Lua filter, and Lua C bridge into the RIME user
# directory on macOS so Squirrel picks them up on its next deploy.
#
# Usage:
#   ./packaging/macos/deploy.sh           # uses build/ directory
#   ./packaging/macos/deploy.sh build-rel # uses an alternate build dir
#
# Squirrel must be installed and have run at least once so ~/Library/Rime
# exists. After this script completes, click "Squirrel -> Deploy" in the
# input source menu (or run `~/Library/Rime/...` deployer) to apply.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-build}"
RIME_DIR="${RIME_DIR:-$HOME/Library/Rime}"

if [[ ! -d "$RIME_DIR" ]]; then
  echo "[error] RIME user directory not found: $RIME_DIR" >&2
  echo "        Install Squirrel from https://rime.im and run it once." >&2
  exit 1
fi

PLUGIN_BIN="$REPO_ROOT/$BUILD_DIR/src/plugin/dafeng_lua_bridge.so"
SCHEMA_SRC="$REPO_ROOT/schemas/wubi86_dafeng.schema.yaml"
FILTER_SRC="$REPO_ROOT/src/plugin/dafeng_filter.lua"
RERANK_SRC="$REPO_ROOT/src/plugin/dafeng_rerank.lua"

for f in "$PLUGIN_BIN" "$SCHEMA_SRC" "$FILTER_SRC" "$RERANK_SRC"; do
  if [[ ! -f "$f" ]]; then
    echo "[error] missing artifact: $f" >&2
    echo "        Did you build first?  cmake --build $BUILD_DIR" >&2
    exit 1
  fi
done

mkdir -p "$RIME_DIR/lua"

# librime-lua looks for Lua scripts under <user>/lua and resolves require()
# via package.cpath which (in librime-lua's defaults) includes <user>/lua.
cp -v "$FILTER_SRC" "$RIME_DIR/lua/dafeng_filter.lua"
cp -v "$RERANK_SRC" "$RIME_DIR/lua/dafeng_rerank.lua"
cp -v "$PLUGIN_BIN" "$RIME_DIR/lua/dafeng_lua_bridge.so"

# The schema goes at the top of the user dir. Filename MUST equal
# schema_id per RIME convention, otherwise deploy errors with
# "missing input schema".
cp -v "$SCHEMA_SRC" "$RIME_DIR/wubi86_dafeng.schema.yaml"

cat <<EOF

Deployed to $RIME_DIR.

Next steps:
  1. Make sure dafeng-daemon is running:
       $REPO_ROOT/$BUILD_DIR/src/daemon/dafeng-daemon --foreground
  2. In the macOS input-source menu, choose "Squirrel -> Deploy".
  3. Switch the input method to 大风五笔 86.
  4. Type "ggll" — candidates should appear in REVERSED order (the Phase 2.1
     mock reranker simply reverses the list).

Check the daemon log for inbound rerank requests. If you see no rerank
traffic, the Lua filter likely failed to load — check Squirrel's log
under ~/Library/Logs/Rime/.
EOF
