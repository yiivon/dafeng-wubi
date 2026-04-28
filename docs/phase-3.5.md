# Phase 3.5 — Distribution

Phase 3.2 made real LLM rerank work. Phase 3.5 makes the project
**installable by people who haven't read the source tree**. Three
parallel install paths:

| Channel | For | What it gives |
|---|---|---|
| **`.pkg` installer** (this phase) | non-developers / "double-click to install" | Native macOS GUI, customize panel with LLM toggle |
| **Homebrew Cask** (this phase) | brew-friendly Mac users | `brew install dafeng-wubi` builds + installs |
| **`./install.sh`** (Phase 3.1) | source-tree users / contributors | Builds from local checkout |

All three end in the same state: schemas + Lua bridge in
`~/Library/Rime/`, daemon launchd-managed, `dafeng-cli` on `PATH`.

## The .pkg installer

`packaging/macos/build-pkg.sh` produces a distribution-style
`.pkg` with **two components**:

```
+-- dafeng-base.pkg     (always installed, ~600 KB)
|     • dafeng-daemon, dafeng-cli  → /usr/local/bin
|     • Lua bridge .so, schemas, scripts → /usr/local/share/dafeng
|     • postinstall: deploys to ~/Library/Rime + installs launchd
|       (deterministic backend, no model file needed)
|
+-- dafeng-llm.pkg      (optional, off by default, marker payload)
      • postinstall: downloads Qwen 2.5 0.5B Q4_K_M GGUF (~390 MB)
        + reconfigures launchd LaunchAgent to use --backend llama_cpp
```

The user sees a "Customize" panel during install with a checkbox:

> ☐ LLM rerank backend (~390 MB download)

If unchecked → fast deterministic backend (still has all the learning
machinery — the Phase 2.4 / 3.2 wubi-aware split-typed phrase pipeline
works fine without LLM).

If checked → postinstall pulls the model from Hugging Face and reloads
the launchd plist with `--backend llama_cpp --model-path <gguf>`. A
desktop notification confirms when the model is ready.

### Building locally

```bash
brew install cmake ninja libgit2 llama.cpp
cmake -B build-llama -G Ninja -DDAFENG_ENABLE_LLAMA_CPP=ON
cmake --build build-llama -j
bash packaging/macos/build-pkg.sh
# → dist/dafeng-wubi-<version>-<arch>.pkg
```

### Install on test machine

GUI: double-click the `.pkg` → Continue → Customize → tick or untick
"LLM rerank backend" → Install.

CLI:

```bash
# base only
sudo installer -pkg dist/*.pkg -target /

# base + LLM (override default choice)
sudo installer -pkg dist/*.pkg \
               -applyChoiceChangesXML choices-with-llm.xml \
               -target /
```

`choices-with-llm.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><array>
  <dict>
    <key>choiceIdentifier</key><string>dafeng-llm</string>
    <key>choiceAttribute</key><string>selected</string>
    <key>attributeSetting</key><integer>1</integer>
  </dict>
</array></plist>
```

### Gatekeeper / signing

The .pkg is **unsigned**. On first install macOS shows
"unidentified developer." Workarounds:

- Right-click → Open in Finder (one-time bypass)
- `xattr -d com.apple.quarantine path/to.pkg` (CLI)

Apple Developer ID signing + notarization is Phase 3.5.+ — needs a paid
developer account secret added to GitHub Actions.

## The Homebrew formula

`packaging/homebrew/dafeng-wubi.rb` is a brew formula that:

- Declares deps: `cmake`, `ninja`, `libgit2`, `llama.cpp`, `sqlite`,
  optional `mlx-c`, recommended `squirrel` (cask)
- Builds with `DAFENG_ENABLE_LLAMA_CPP=ON` from source
- Installs binaries to `bin/`, share files to `pkgshare/`
- Registers a `brew services` integration as an alternative to the
  launchd LaunchAgent
- Caveats string tells the user to run `dafeng-cli setup`

To consume it (when the tap exists at `github.com/<owner>/homebrew-dafeng`):

```bash
brew install <owner>/dafeng/dafeng-wubi
dafeng-cli setup                            # base, deterministic
# OR
dafeng-cli setup --backend llama_cpp        # with LLM (model must exist)
```

The formula's `url` and `sha256` are placeholder values. The release
GitHub Action overwrites them during a tagged release; until then,
users build from `--HEAD` (`brew install --HEAD <tap>/dafeng-wubi`).

## The `dafeng-cli setup` subcommand

A new CLI subcommand that exists for the brew + .pkg post-install
case where the binaries are placed but ~/Library/Rime/ deployment is
the user's call:

```bash
dafeng-cli setup
dafeng-cli setup --backend llama_cpp [--model-path PATH]
```

It probes `/usr/local/share/dafeng/` and `/opt/homebrew/share/dafeng/`
for the helper scripts (`deploy.sh`, `install_launchagent.sh`) we
shipped at install time, then runs them. With `--backend llama_cpp`
without `--model-path`, it looks at the canonical model location and
errors with a download instruction if the model is missing.

## GitHub Actions release pipeline

`.github/workflows/release.yml` triggers on `git push origin v0.X.Y`:

1. **build-macos** runs on `macos-14` (arm64) + `macos-13` (x86_64).
   Each builds with `DAFENG_ENABLE_LLAMA_CPP=ON`, runs `build-pkg.sh`,
   and produces:
   - `dafeng-wubi-<version>-<arch>.pkg`
   - `dafeng-wubi-<version>-<arch>.tar.gz`
2. **publish-release** flattens the artifacts, generates
   `SHA256SUMS.txt`, extracts the matching CHANGELOG section as release
   body, and creates / updates the GitHub release.

Manual re-trigger via the `workflow_dispatch` event in the Actions tab.

## What 3.5 does NOT include

- **Apple Developer ID signing + notarization.** The .pkg works
  unsigned (with the Gatekeeper bypass) but distributing through the
  App Store or to security-strict users needs signing. Adds an
  ~$99/year cost + secret management in CI.
- **Bundled dylibs.** The .pkg currently relies on the user having
  brew packages (libllama, libgit2) installed. A truly self-contained
  .pkg would `install_name_tool` the binary's references and bundle
  the .dylibs. Possible but tedious; defer until first user complains.
- **Auto-update.** `Sparkle.framework` integration would let the
  installed daemon check for new versions. Phase 3.5.+ if usage grows.
- **Windows port.** Phase 3.3.

## Verification on this machine

```bash
$ ./packaging/macos/build-pkg.sh
✓ built: dist/dafeng-wubi-0.1.0-arm64.pkg

$ pkgutil --expand-full dist/dafeng-wubi-0.1.0-arm64.pkg /tmp/inspect
$ ls /tmp/inspect/
Distribution
dafeng-base.pkg
dafeng-llm.pkg

$ # Distribution.xml has the customize=always GUI flag and the
$ # llm choice with start_selected="false" — meaning it presents
$ # a checkbox UNchecked by default.
```

The .pkg's logical structure has been verified; an end-to-end install
on a fresh macOS box should be the first item under "next-week
testing" once a release tag is cut.
