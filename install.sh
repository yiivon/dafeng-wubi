#!/usr/bin/env bash
# install.sh — one-command bootstrap for dafeng-wubi on a fresh macOS.
#
# Usage:
#   ./install.sh                    # full install
#   ./install.sh --skip-build       # use existing build/ if present
#   ./install.sh --skip-launchagent # don't auto-start the daemon
#   ./install.sh --uninstall        # remove launchagent + RIME files
#
# Idempotent: safe to re-run after upgrading.
#
# What it does:
#   1. Sanity-check macOS version + arch
#   2. Install brew packages (cmake, ninja, libgit2, mlx-c)
#   3. Install Squirrel (input method) if missing
#   4. Install the wubi86 dictionary (clones rime-wubi)
#   5. Build dafeng (cmake + ninja)
#   6. Deploy schema + Lua + .so into ~/Library/Rime/
#   7. Install dafeng-daemon as a launchd LaunchAgent
#   8. Print next-step instructions for the manual Squirrel deploy

set -euo pipefail

# Colors so progress is scannable.
if [[ -t 1 ]]; then
  C_BLUE=$'\e[34m'; C_GREEN=$'\e[32m'; C_YELLOW=$'\e[33m';
  C_RED=$'\e[31m'; C_DIM=$'\e[2m'; C_RESET=$'\e[0m';
else
  C_BLUE=''; C_GREEN=''; C_YELLOW=''; C_RED=''; C_DIM=''; C_RESET='';
fi
log()    { printf '%s» %s%s\n' "$C_BLUE" "$1" "$C_RESET"; }
ok()     { printf '%s✓ %s%s\n' "$C_GREEN" "$1" "$C_RESET"; }
warn()   { printf '%s! %s%s\n' "$C_YELLOW" "$1" "$C_RESET"; }
fatal()  { printf '%s✗ %s%s\n' "$C_RED" "$1" "$C_RESET" >&2; exit 1; }

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKIP_BUILD=0
SKIP_LAUNCHAGENT=0
UNINSTALL=0

for arg in "$@"; do
  case "$arg" in
    --skip-build) SKIP_BUILD=1 ;;
    --skip-launchagent) SKIP_LAUNCHAGENT=1 ;;
    --uninstall) UNINSTALL=1 ;;
    -h|--help)
      sed -n '3,16p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
    *) fatal "unknown arg: $arg (try --help)" ;;
  esac
done

# ---- uninstall path ---------------------------------------------------------

if [[ $UNINSTALL -eq 1 ]]; then
  log "uninstalling launchagent"
  launchctl bootout "gui/$(id -u)/com.dafeng.daemon" 2>/dev/null || true
  rm -f "$HOME/Library/LaunchAgents/com.dafeng.daemon.plist"
  log "removing dafeng files from RIME user dir"
  rm -f "$HOME/Library/Rime/wubi86_dafeng.schema.yaml" \
        "$HOME/Library/Rime/wubi86_dafeng.dict.yaml" \
        "$HOME/Library/Rime/dafeng_learned.dict.yaml" \
        "$HOME/Library/Rime/rime.lua" \
        "$HOME/Library/Rime/lua/dafeng_filter.lua" \
        "$HOME/Library/Rime/lua/dafeng_rerank.lua" \
        "$HOME/Library/Rime/lua/dafeng_lua_bridge.so"
  ok "uninstalled. Your data dir at ~/Library/Application Support/Dafeng/"
  ok "is intact (history.db, ngram, learned_words.yaml). Remove that"
  ok "manually if you want a clean slate."
  exit 0
fi

# ---- pre-flight -------------------------------------------------------------

log "macOS version + arch check"
OS_VERSION=$(sw_vers -productVersion)
OS_MAJOR=$(echo "$OS_VERSION" | cut -d. -f1)
if [[ $OS_MAJOR -lt 14 ]]; then
  fatal "macOS >= 14 required (you have $OS_VERSION). Required for MLX."
fi
ARCH=$(uname -m)
if [[ $ARCH != arm64 && $ARCH != x86_64 ]]; then
  fatal "unsupported arch: $ARCH (need arm64 or x86_64)"
fi
ok "macOS $OS_VERSION on $ARCH"

log "Xcode Command Line Tools"
if ! xcode-select -p >/dev/null 2>&1; then
  warn "Command Line Tools not installed. Triggering install (a system"
  warn "dialog will appear). After it finishes, re-run this script."
  xcode-select --install
  exit 1
fi
ok "CLT at $(xcode-select -p)"

# ---- brew packages ----------------------------------------------------------

log "Homebrew + required packages"
if ! command -v brew >/dev/null 2>&1; then
  fatal "Homebrew not installed. See https://brew.sh"
fi

want_pkgs=(cmake ninja libgit2 mlx-c llama.cpp)
need_install=()
for p in "${want_pkgs[@]}"; do
  if ! brew list --formula "$p" >/dev/null 2>&1; then
    need_install+=("$p")
  fi
done
if [[ ${#need_install[@]} -gt 0 ]]; then
  log "installing: ${need_install[*]}"
  brew install "${need_install[@]}"
fi
ok "brew packages: ${want_pkgs[*]}"

# ---- Squirrel input method --------------------------------------------------

log "Squirrel (RIME input method)"
if [[ ! -d "/Library/Input Methods/Squirrel.app" ]]; then
  log "installing Squirrel via brew --cask"
  brew install --cask squirrel
  warn "After Squirrel installs, you'll need to *enable* it once via"
  warn "  System Settings → Keyboard → Input Sources → +"
  warn "  Pick any 鼠须管 variant. RIME will create ~/Library/Rime/."
  warn "Re-run this script after that step."
  if [[ ! -d "$HOME/Library/Rime" ]]; then
    fatal "$HOME/Library/Rime/ not found yet — finish the Input Source step first."
  fi
fi
ok "Squirrel installed"

if [[ ! -d "$HOME/Library/Rime" ]]; then
  fatal "$HOME/Library/Rime/ doesn't exist. Enable Squirrel as an Input Source first (System Settings → Keyboard)."
fi
ok "RIME user dir exists at ~/Library/Rime/"

# ---- wubi86 dictionary ------------------------------------------------------

log "wubi86 dictionary"
if [[ ! -f "$HOME/Library/Rime/wubi86.dict.yaml" ]]; then
  TMP_RW=$(mktemp -d /tmp/dafeng-rime-wubi-XXXXXX)
  log "  cloning rime/rime-wubi into $TMP_RW"
  git clone --depth 1 https://github.com/rime/rime-wubi.git "$TMP_RW" >/dev/null 2>&1
  cp "$TMP_RW"/wubi86.{schema,dict}.yaml "$HOME/Library/Rime/"
  cp "$TMP_RW"/wubi_pinyin.schema.yaml "$HOME/Library/Rime/" 2>/dev/null || true
  cp "$TMP_RW"/wubi_trad.schema.yaml "$HOME/Library/Rime/" 2>/dev/null || true
  rm -rf "$TMP_RW"
fi
ok "wubi86.dict.yaml in place"

# ---- build dafeng -----------------------------------------------------------

if [[ $SKIP_BUILD -eq 0 ]]; then
  log "building dafeng (cmake + ninja, RelWithDebInfo)"
  cd "$REPO_ROOT"
  if [[ -f .gitmodules ]]; then
    git submodule update --init --recursive
  fi
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DDAFENG_ENABLE_GIT_SYNC=ON >/dev/null
  cmake --build build -j --target dafeng-daemon dafeng-cli dafeng_lua_bridge
  ok "build/src/daemon/dafeng-daemon ready"
fi

DAEMON_BIN="$REPO_ROOT/build/src/daemon/dafeng-daemon"
if [[ ! -x "$DAEMON_BIN" ]]; then
  fatal "daemon binary missing: $DAEMON_BIN"
fi

# ---- schema_list patch (only add wubi86_dafeng if not already there) -------

CUSTOM="$HOME/Library/Rime/default.custom.yaml"
if [[ ! -f "$CUSTOM" ]] || ! grep -q 'wubi86_dafeng' "$CUSTOM"; then
  log "writing default.custom.yaml — making 大风五笔 the only schema"
  cat > "$CUSTOM" <<'YAML'
# Custom patch — managed by dafeng install.sh.
# Keeps only 大风五笔 in the schema list so Ctrl+\` doesn't need a pick.
# Add `- schema: wubi86` below if you want a vanilla fallback.

patch:
  schema_list:
    - schema: wubi86_dafeng
YAML
fi
ok "default.custom.yaml configured"

# ---- deploy schema + Lua + .so ---------------------------------------------

log "deploying schema, dict, Lua bridge to ~/Library/Rime/"
"$REPO_ROOT/packaging/macos/deploy.sh" build >/dev/null
ok "deploy artifacts in place"

# ---- launchd LaunchAgent ---------------------------------------------------

if [[ $SKIP_LAUNCHAGENT -eq 0 ]]; then
  log "installing dafeng-daemon as a launchd LaunchAgent"
  "$REPO_ROOT/packaging/macos/install_launchagent.sh" "$DAEMON_BIN" >/dev/null
  ok "daemon will auto-start on login"
fi

# ---- next steps -------------------------------------------------------------

cat <<EOF

${C_GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${C_RESET}
${C_GREEN}  install complete${C_RESET}
${C_GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${C_RESET}

  1.  Click ${C_BLUE}menu bar 鼠须管 → 重新部署${C_RESET}
      (one-time deploy so RIME picks up our schema + dict)

  2.  In any text app, press ${C_BLUE}Ctrl + \`${C_RESET} and pick
      "${C_BLUE}大风五笔 86${C_RESET}" (the only option, after step 1).

  3.  Type. As you commit characters, the daemon learns. Once a week:
        ${C_DIM}\$ $REPO_ROOT/build/src/cli/dafeng-cli learn --min-freq 2${C_RESET}
      then 重新部署 again to pick up the learned phrases.

  Cheat sheet: ${C_DIM}docs/cheat-sheet.md${C_RESET}
  Logs:        ${C_DIM}~/Library/Logs/dafeng-daemon.err.log${C_RESET}
  Stats:       ${C_DIM}\$ $REPO_ROOT/build/src/cli/dafeng-cli stats${C_RESET}

  ${C_YELLOW}Privacy: history.db never leaves your machine. ~/Library/Application Support/Dafeng/${C_RESET}
  ${C_YELLOW}is local-only by default. Cross-device sync requires explicit opt-in.${C_RESET}

EOF
