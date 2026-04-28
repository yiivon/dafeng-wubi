#!/usr/bin/env bash
# Build the SwiftPM target and wrap the resulting executable into a
# minimal macOS .app bundle. The build-pkg.sh script copies the bundle
# into /Applications during install.
#
# Output: dist/Dafeng Inspector.app/  (relative to repo root)

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
DIST_DIR="$REPO_ROOT/dist"
APP_NAME="Dafeng Inspector.app"
APP_PATH="$DIST_DIR/$APP_NAME"
VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/VERSION")"

echo "» swift build -c release"
(cd "$HERE" && swift build -c release)

BIN="$HERE/.build/release/DafengInspector"
if [[ ! -x "$BIN" ]]; then
  echo "[error] expected executable missing: $BIN" >&2
  exit 1
fi

echo "» building $APP_NAME"
rm -rf "$APP_PATH"
mkdir -p "$APP_PATH/Contents/MacOS"
mkdir -p "$APP_PATH/Contents/Resources"
cp "$BIN" "$APP_PATH/Contents/MacOS/DafengInspector"

cat > "$APP_PATH/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
                          "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Dafeng Inspector</string>
  <key>CFBundleDisplayName</key><string>大风五笔检查器</string>
  <key>CFBundleIdentifier</key><string>com.dafeng.inspector</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleExecutable</key><string>DafengInspector</string>
  <key>CFBundleVersion</key><string>${VERSION}</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSSupportsAutomaticTermination</key><true/>
  <key>NSSupportsSuddenTermination</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.utilities</string>
</dict>
</plist>
PLIST

# Ad-hoc sign so dyld doesn't refuse to launch the bundle on Apple Silicon.
codesign --force --sign - "$APP_PATH/Contents/MacOS/DafengInspector" 2>/dev/null || true
codesign --force --sign - --deep "$APP_PATH" 2>/dev/null || true

echo
echo "✓ $APP_PATH"
du -sh "$APP_PATH" | sed 's/^/  size: /'
echo
echo "Open from Finder, or:"
echo "  open \"$APP_PATH\""
