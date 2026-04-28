#!/usr/bin/env bash
# build-pkg.sh — assemble an unsigned macOS distribution-style .pkg
# with a CHECKBOX for the user to opt into the LLM model.
#
# Output:
#   dist/dafeng-wubi-<version>-<arch>.pkg
#
# Internal structure (combined via `productbuild --distribution`):
#   dafeng-base.pkg     always installed
#                       — binaries, schemas, Lua bridge, launchd plist
#                       — postinstall: deploy to ~/Library/Rime + launchd
#                         (deterministic backend, no model download)
#   dafeng-llm.pkg      optional, off by default
#                       — payload: small marker
#                       — postinstall: downloads Qwen 2.5 0.5B GGUF
#                         (~390 MB), reconfigures launchd to use llama_cpp
#
# The user sees a "Customize" panel with the LLM checkbox unchecked by
# default. Checking it pulls the model during install. Unchecking
# leaves them with the deterministic backend (still useful — learning,
# rerank rules, all the engine machinery works without LLM).
#
# Prereqs: a built tree (`cmake --build build-llama` first).
# Unsigned: users right-click → Open on first run, OR add an
# Apple Developer ID at distribution time.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build-llama}"
DIST_DIR="$REPO_ROOT/dist"
VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/VERSION")"
ARCH="$(uname -m)"
PKG_NAME="dafeng-wubi-${VERSION}-${ARCH}.pkg"
BUNDLE_ID_BASE="com.dafeng.wubi.base"
BUNDLE_ID_LLM="com.dafeng.wubi.llm"

if [[ ! -x "$BUILD_DIR/src/daemon/dafeng-daemon" ]]; then
  echo "[error] daemon binary missing at $BUILD_DIR/src/daemon/dafeng-daemon" >&2
  echo "        Build first: cmake --build $BUILD_DIR" >&2
  exit 1
fi

WORK="$(mktemp -d /tmp/dafeng-pkg.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$DIST_DIR"

# =============================================================================
# 1. base component
# =============================================================================

echo "» staging base component"
BASE_ROOT="$WORK/base-root"
BASE_SCRIPTS="$WORK/base-scripts"
mkdir -p "$BASE_ROOT/usr/local/bin"
mkdir -p "$BASE_ROOT/usr/local/share/dafeng"
mkdir -p "$BASE_SCRIPTS"

cp "$BUILD_DIR/src/daemon/dafeng-daemon" "$BASE_ROOT/usr/local/bin/"
cp "$BUILD_DIR/src/cli/dafeng-cli" "$BASE_ROOT/usr/local/bin/"

cp "$BUILD_DIR/src/plugin/dafeng_lua_bridge.so" "$BASE_ROOT/usr/local/share/dafeng/"
cp "$REPO_ROOT/src/plugin/rime.lua" "$BASE_ROOT/usr/local/share/dafeng/"
cp "$REPO_ROOT/src/plugin/dafeng_filter.lua" "$BASE_ROOT/usr/local/share/dafeng/"
cp "$REPO_ROOT/src/plugin/dafeng_rerank.lua" "$BASE_ROOT/usr/local/share/dafeng/"
cp "$REPO_ROOT/schemas/wubi86_dafeng.schema.yaml" "$BASE_ROOT/usr/local/share/dafeng/"
cp "$REPO_ROOT/schemas/wubi86_dafeng.dict.yaml" "$BASE_ROOT/usr/local/share/dafeng/"
cp "$REPO_ROOT/schemas/dafeng_learned.dict.yaml" "$BASE_ROOT/usr/local/share/dafeng/"
cp "$REPO_ROOT/packaging/macos/com.dafeng.daemon.plist" "$BASE_ROOT/usr/local/share/dafeng/"
cp "$REPO_ROOT/packaging/macos/install_launchagent.sh" "$BASE_ROOT/usr/local/share/dafeng/"
cp "$REPO_ROOT/packaging/macos/deploy.sh" "$BASE_ROOT/usr/local/share/dafeng/"
chmod +x "$BASE_ROOT/usr/local/share/dafeng/install_launchagent.sh" \
         "$BASE_ROOT/usr/local/share/dafeng/deploy.sh"

cat > "$BASE_SCRIPTS/postinstall" <<'PI_EOF'
#!/bin/bash
# Base component postinstall — runs as root.
# Drops to the invoking user to deploy to ~/Library/Rime.
set -e

ACTUAL_USER="${USER}"
USER_HOME="$HOME"
if [[ "$EUID" -eq 0 ]]; then
  ACTUAL_USER="${SUDO_USER:-${USER}}"
  if [[ -z "$ACTUAL_USER" || "$ACTUAL_USER" == "root" ]]; then
    # Fall back to the GUI session owner when sudo info is unavailable
    # (e.g. installer GUI session).
    ACTUAL_USER="$(stat -f '%Su' /dev/console)"
  fi
  USER_HOME="$(eval echo "~$ACTUAL_USER")"
fi

sudo -u "$ACTUAL_USER" -H bash <<INNER
set -e
export HOME="$USER_HOME"
mkdir -p "\$HOME/Library/Rime/lua"

cp /usr/local/share/dafeng/wubi86_dafeng.schema.yaml "\$HOME/Library/Rime/"
cp /usr/local/share/dafeng/wubi86_dafeng.dict.yaml "\$HOME/Library/Rime/"
[[ -f "\$HOME/Library/Rime/dafeng_learned.dict.yaml" ]] || \
  cp /usr/local/share/dafeng/dafeng_learned.dict.yaml "\$HOME/Library/Rime/"
cp /usr/local/share/dafeng/rime.lua "\$HOME/Library/Rime/"
cp /usr/local/share/dafeng/dafeng_filter.lua "\$HOME/Library/Rime/lua/"
cp /usr/local/share/dafeng/dafeng_rerank.lua "\$HOME/Library/Rime/lua/"
cp /usr/local/share/dafeng/dafeng_lua_bridge.so "\$HOME/Library/Rime/lua/"

# Patch default.custom.yaml so 大风五笔 is the default schema.
if ! grep -q wubi86_dafeng "\$HOME/Library/Rime/default.custom.yaml" 2>/dev/null; then
  cat > "\$HOME/Library/Rime/default.custom.yaml" <<YAML
# Managed by dafeng installer.
patch:
  schema_list:
    - schema: wubi86_dafeng
YAML
fi

# Default backend = deterministic (no model download). The LLM
# component, if installed, will overwrite the launchagent below.
bash /usr/local/share/dafeng/install_launchagent.sh /usr/local/bin/dafeng-daemon
INNER
exit 0
PI_EOF
chmod 755 "$BASE_SCRIPTS/postinstall"

cat > "$BASE_SCRIPTS/preinstall" <<'PRE_EOF'
#!/bin/bash
set -e
if [[ ! -d "/Library/Input Methods/Squirrel.app" ]]; then
  osascript <<OSA 2>/dev/null || true
display dialog "dafeng-wubi requires Squirrel input method (RIME).\n\nInstall Squirrel first via:\n  brew install --cask squirrel\n\nThen re-run this installer." buttons {"Cancel"} default button "Cancel" with icon stop with title "Missing dependency"
OSA
  exit 1
fi
exit 0
PRE_EOF
chmod 755 "$BASE_SCRIPTS/preinstall"

echo "» pkgbuild base"
pkgbuild \
  --root "$BASE_ROOT" \
  --identifier "$BUNDLE_ID_BASE" \
  --version "$VERSION" \
  --install-location "/" \
  --scripts "$BASE_SCRIPTS" \
  "$WORK/dafeng-base.pkg"

# =============================================================================
# 2. LLM component (optional)
# =============================================================================

echo "» staging LLM component"
LLM_ROOT="$WORK/llm-root"
LLM_SCRIPTS="$WORK/llm-scripts"
mkdir -p "$LLM_ROOT/usr/local/share/dafeng"
mkdir -p "$LLM_SCRIPTS"

# Tiny marker so this pkg has a payload — required by pkgbuild.
echo "LLM rerank backend opted-in by user at install time." > "$LLM_ROOT/usr/local/share/dafeng/.llm-opted-in"

cat > "$LLM_SCRIPTS/postinstall" <<'LLM_PI_EOF'
#!/bin/bash
# LLM component postinstall — runs as root only when user checked the
# "Enable LLM rerank backend" box. Downloads the Qwen 2.5 0.5B GGUF
# (~390 MB) and reconfigures the launchd LaunchAgent.
set -e

ACTUAL_USER="${USER}"
USER_HOME="$HOME"
if [[ "$EUID" -eq 0 ]]; then
  ACTUAL_USER="${SUDO_USER:-${USER}}"
  if [[ -z "$ACTUAL_USER" || "$ACTUAL_USER" == "root" ]]; then
    ACTUAL_USER="$(stat -f '%Su' /dev/console)"
  fi
  USER_HOME="$(eval echo "~$ACTUAL_USER")"
fi

MODEL_URL="https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf?download=true"

sudo -u "$ACTUAL_USER" -H bash <<INNER
set -e
export HOME="$USER_HOME"
MODEL_DIR="\$HOME/Library/Application Support/Dafeng/models/Qwen2.5-0.5B-Instruct-GGUF"
MODEL_PATH="\$MODEL_DIR/qwen2.5-0.5b-instruct-q4_k_m.gguf"
mkdir -p "\$MODEL_DIR"

if [[ ! -s "\$MODEL_PATH" ]]; then
  # Show a desktop notification so the user knows the install is in
  # progress on a long network fetch.
  osascript -e 'display notification "Downloading LLM model (~390 MB)…" with title "dafeng-wubi"' || true
  # curl with progress; redirect to file. -L follows the HF redirect.
  curl -L --fail --silent --show-error --output "\$MODEL_PATH" "$MODEL_URL"
fi

# Reconfigure the launchd LaunchAgent to use llama_cpp + the model.
DAFENG_BACKEND=llama_cpp \
DAFENG_MODEL_PATH="\$MODEL_PATH" \
  bash /usr/local/share/dafeng/install_launchagent.sh /usr/local/bin/dafeng-daemon

osascript -e 'display notification "LLM backend ready. Reload Squirrel to use." with title "dafeng-wubi"' || true
INNER
exit 0
LLM_PI_EOF
chmod 755 "$LLM_SCRIPTS/postinstall"

echo "» pkgbuild LLM"
pkgbuild \
  --root "$LLM_ROOT" \
  --identifier "$BUNDLE_ID_LLM" \
  --version "$VERSION" \
  --install-location "/" \
  --scripts "$LLM_SCRIPTS" \
  "$WORK/dafeng-llm.pkg"

# =============================================================================
# 3. Distribution.xml — defines the install GUI flow
# =============================================================================

cat > "$WORK/Distribution.xml" <<XML_EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>大风五笔 (dafeng-wubi)</title>
  <organization>com.dafeng</organization>
  <options customize="always" require-scripts="true" hostArchitectures="${ARCH}"/>
  <volume-check>
    <allowed-os-versions><os-version min="14.0"/></allowed-os-versions>
  </volume-check>

  <welcome  language="en"  mime-type="text/plain">
    <![CDATA[
大风五笔 (dafeng-wubi) installer.

This installs a learning Wubi IME for macOS. Privacy-local; nothing
leaves your machine.

You can optionally enable the LLM rerank backend (~390 MB download)
on the next screen — pick that if you want context-aware candidate
reordering. Skip it for the lighter deterministic backend.
    ]]>
  </welcome>

  <choices-outline>
    <line choice="dafeng-base"/>
    <line choice="dafeng-llm"/>
  </choices-outline>

  <choice id="dafeng-base"
          title="dafeng-wubi (required)"
          description="Daemon, schemas, Lua bridge, launchd LaunchAgent."
          enabled="false" selected="true">
    <pkg-ref id="${BUNDLE_ID_BASE}"/>
  </choice>

  <choice id="dafeng-llm"
          title="LLM rerank backend (~390 MB download)"
          description="Adds Qwen 2.5 0.5B Q4_K_M for context-aware candidate
reordering. Without this, the daemon uses a fast deterministic scorer
that doesn't need a model. You can enable later via:
  dafeng-cli setup --backend llama_cpp"
          start_selected="false">
    <pkg-ref id="${BUNDLE_ID_LLM}"/>
  </choice>

  <pkg-ref id="${BUNDLE_ID_BASE}" version="${VERSION}">dafeng-base.pkg</pkg-ref>
  <pkg-ref id="${BUNDLE_ID_LLM}"  version="${VERSION}">dafeng-llm.pkg</pkg-ref>
</installer-gui-script>
XML_EOF

# =============================================================================
# 4. productbuild — combine into final installer
# =============================================================================

echo "» productbuild final $PKG_NAME"
PKG_OUT="$DIST_DIR/$PKG_NAME"
productbuild \
  --distribution "$WORK/Distribution.xml" \
  --package-path "$WORK" \
  "$PKG_OUT"

echo
echo "✓ built: $PKG_OUT"
echo "  size:  $(du -h "$PKG_OUT" | cut -f1)"
echo
echo "Test install on this machine (requires sudo):"
echo "  sudo installer -pkg \"$PKG_OUT\" -target /"
echo "    base only (no LLM)."
echo "  sudo installer -pkg \"$PKG_OUT\" -applyChoiceChangesXML <(cat <<C
<?xml version='1.0' encoding='UTF-8'?>
<!DOCTYPE plist PUBLIC '-//Apple//DTD PLIST 1.0//EN' 'http://www.apple.com/DTDs/PropertyList-1.0.dtd'>
<plist version='1.0'><array>
  <dict><key>choiceIdentifier</key><string>dafeng-llm</string>
        <key>choiceAttribute</key><string>selected</string>
        <key>attributeSetting</key><integer>1</integer></dict>
</array></plist>
C
) -target /"
echo "    base + LLM (downloads model)."
echo
echo "GUI: double-click the .pkg → Customize panel → check/uncheck the LLM box."
