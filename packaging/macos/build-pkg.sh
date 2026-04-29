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

# The Inspector .app is built separately via apps/inspector/build-app.sh.
# Build it on demand if missing — keeps the .pkg always-fresh.
INSPECTOR_APP="$DIST_DIR/Dafeng Inspector.app"
if [[ ! -d "$INSPECTOR_APP" ]]; then
  echo "» (re)building Dafeng Inspector.app"
  bash "$REPO_ROOT/apps/inspector/build-app.sh" >/dev/null
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

# Phase 3.4: ship the SwiftUI inspector under /Applications.
# productbuild's BOM walks the staging tree, so the .app's bundle
# structure is preserved as-is on installation.
APPLICATIONS_DIR="$BASE_ROOT/Applications"
mkdir -p "$APPLICATIONS_DIR"
cp -R "$INSPECTOR_APP" "$APPLICATIONS_DIR/"

# ---- bundle non-system dylibs (libllama, libggml, libgit2, ssh, openssl)
# We use dylibbundler to walk the dep graph from the daemon, copy
# everything to /usr/local/lib/dafeng/, and fix install names. Then we
# also copy ggml's runtime backend plugins (libggml-cpu-*.so,
# libggml-metal.so, etc.) which dlopen at runtime — dylibbundler
# doesn't see those. The daemon's reranker_llamacpp.cc probes
# /usr/local/lib/dafeng/ as a candidate for ggml_backend_load_all_from_path,
# so plugins land in the same dir as the dylibs they depend on.
echo "» bundling dylibs via dylibbundler"
DAFENG_LIBDIR="$BASE_ROOT/usr/local/lib/dafeng"
mkdir -p "$DAFENG_LIBDIR"

if ! command -v dylibbundler >/dev/null 2>&1; then
  # We used to fall back to a "warn and continue" mode here. That
  # silently shipped a 400 KB .pkg in v0.2.0 release CI that couldn't
  # run on stock macOS — every dylib reference still pointed at
  # /opt/homebrew. Hard-fail so this can't happen again. If you
  # genuinely want a quick local test build without bundling, comment
  # this block out by hand.
  echo "[error] dylibbundler not installed; refusing to ship an" >&2
  echo "        unbundled .pkg that won't run without Homebrew." >&2
  echo "        Install:  brew install dylibbundler" >&2
  exit 2
fi

# `--no-codesign` because we don't have an Apple Developer ID yet;
# the .pkg itself will run unsigned. The cli + daemon binaries are
# both fixed; --bundle-deps walks the transitive graph.
dylibbundler \
      --fix-file "$BASE_ROOT/usr/local/bin/dafeng-daemon" \
      --fix-file "$BASE_ROOT/usr/local/bin/dafeng-cli" \
      --bundle-deps \
      --dest-dir "$DAFENG_LIBDIR" \
      --install-path "@executable_path/../lib/dafeng/" \
      --create-dir \
      --overwrite-files \
      --no-codesign 2>&1 | tail -12

  # ggml runtime plugins (loaded via dlopen, not linked).
  GGML_LIBEXEC="$(brew --prefix ggml)/libexec"
  if [[ -d "$GGML_LIBEXEC" ]]; then
    for plugin in "$GGML_LIBEXEC"/libggml-*.so; do
      [[ -f "$plugin" ]] || continue
      cp "$plugin" "$DAFENG_LIBDIR/"
      base="$(basename "$plugin")"
      chmod u+w "$DAFENG_LIBDIR/$base"
      install_name_tool -id "@rpath/$base" "$DAFENG_LIBDIR/$base"
      # Plugins reference @rpath/libggml-base.0.dylib already. We want
      # @rpath to resolve to the plugin's own directory so it finds the
      # bundled libggml-base sibling.
      install_name_tool -add_rpath "@loader_path" "$DAFENG_LIBDIR/$base" 2>/dev/null || true
      # ggml-cpu-*.so links absolute /opt/homebrew/opt/libomp/lib/libomp.dylib.
      # Rewrite to @rpath/libomp.dylib so the bundled copy is used.
      install_name_tool -change /opt/homebrew/opt/libomp/lib/libomp.dylib \
                                "@rpath/libomp.dylib" "$DAFENG_LIBDIR/$base" 2>/dev/null || true
      install_name_tool -change /usr/local/opt/libomp/lib/libomp.dylib \
                                "@rpath/libomp.dylib" "$DAFENG_LIBDIR/$base" 2>/dev/null || true
    done
    echo "  ggml plugins bundled: $(ls "$DAFENG_LIBDIR"/libggml-*.so 2>/dev/null | wc -l | tr -d ' ')"
  fi

  # Bundle libomp.dylib (used by ggml-cpu plugins via OpenMP). Brew installs
  # it under libomp/lib; we copy the dylib only and rewrite its install_name.
  for omp_prefix in /opt/homebrew/opt/libomp /usr/local/opt/libomp; do
    if [[ -f "$omp_prefix/lib/libomp.dylib" ]]; then
      cp "$omp_prefix/lib/libomp.dylib" "$DAFENG_LIBDIR/libomp.dylib"
      chmod u+w "$DAFENG_LIBDIR/libomp.dylib"
      install_name_tool -id "@rpath/libomp.dylib" "$DAFENG_LIBDIR/libomp.dylib"
      echo "  bundled libomp.dylib from $omp_prefix"
      break
    fi
  done

  # ggml plugins reference @rpath/libggml-base.0.dylib (major-soname),
  # but brew ships libggml-base.0.10.0.dylib. dylibbundler copied the
  # versioned name; create the major-version alias the plugins look for.
  # Same for libggml.0.dylib in case anything wants the unversioned soname.
  (
    cd "$DAFENG_LIBDIR"
    for full in libggml-base.0.*.dylib libggml.0.*.dylib; do
      [[ -f "$full" ]] || continue
      # major version = "0"
      short="${full%%.*}.0.dylib"   # libggml-base.0.dylib / libggml.0.dylib
      [[ -e "$short" ]] || ln -s "$full" "$short"
    done
  )

  # Re-sign every Mach-O we modified. install_name_tool invalidates the
  # original ad-hoc signature, and on Apple Silicon dyld will SIGKILL
  # (exit 137) anything that fails signature validation. Use ad-hoc
  # signing (`-`); a real Developer ID would replace this in CI.
  echo "» re-signing modified Mach-O binaries (ad-hoc)"
  codesign --force --sign - "$BASE_ROOT/usr/local/bin/dafeng-daemon"
  codesign --force --sign - "$BASE_ROOT/usr/local/bin/dafeng-cli"
  for lib in "$DAFENG_LIBDIR"/*.dylib "$DAFENG_LIBDIR"/*.so; do
    [[ -f "$lib" && ! -L "$lib" ]] || continue
    codesign --force --sign - "$lib" 2>/dev/null || true
  done

  # Quick sanity: print our binary's deps after rewriting — should now
  # all be @rpath/... or system libs.
  echo "  daemon deps after bundling:"
  otool -L "$BASE_ROOT/usr/local/bin/dafeng-daemon" \
      | awk 'NR>1 && $1 !~ /^\/(usr\/lib|System)/' \
      | sed 's/^/    /'

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
