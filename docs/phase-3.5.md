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

## Self-contained dylib bundling (Phase 3.5+)

The .pkg is now **truly self-contained** — it works on machines that
have *no* Homebrew installed. Mechanics:

1. `dylibbundler --bundle-deps --install-path @executable_path/../lib/dafeng/`
   walks the dep graph from `dafeng-daemon` and `dafeng-cli`, copies
   every non-system dylib (libllama, libggml, libggml-base, libgit2,
   libssl/libcrypto, libssh2, libllhttp) into `$ROOT/usr/local/lib/dafeng/`,
   and rewrites both the binaries' references *and* every transitive
   library's references to `@executable_path/../lib/dafeng/...`.
2. ggml's runtime backend plugins (`libggml-{cpu-*,metal,blas}.so`)
   are loaded via `dlopen`, which `dylibbundler` doesn't see — so we
   also copy them from `$(brew --prefix ggml)/libexec/`, give each
   `@rpath` install-name, and `add_rpath @loader_path` so they find
   their bundled `libggml-base` sibling.
3. The ggml plugins reference `@rpath/libggml-base.0.dylib`
   (major-version soname), but brew ships
   `libggml-base.0.10.0.dylib`. We add a relative symlink
   `libggml-base.0.dylib → libggml-base.0.10.0.dylib` (and the same
   for `libggml.0.dylib`) so the plugin's `@rpath` lookup succeeds.
4. ggml-cpu plugins also link `/opt/homebrew/opt/libomp/lib/libomp.dylib`
   absolute — we bundle a copy of `libomp.dylib`, set its install-name
   to `@rpath/libomp.dylib`, and `install_name_tool -change` the
   absolute reference inside each plugin to `@rpath/libomp.dylib`.
5. **Re-sign every modified Mach-O** with `codesign --force --sign -`
   (ad-hoc). On Apple Silicon, dyld sends SIGKILL to anything whose
   ad-hoc signature became invalid after `install_name_tool` — a
   silent crash that looks like a startup failure with empty stderr.
   This step is mandatory.
6. The daemon's `InitBackendOnce` probes
   `<exe-dir>/../lib/dafeng/` (computed via `_NSGetExecutablePath`)
   *first*, before brew's canonical paths. So the bundle "wins" the
   ggml registration race even when brew is also present, and the
   daemon works whether installed at `/`, under `/opt/homebrew`, or
   extracted to `/tmp/foo` for inspection.

Verified end-to-end: extract the .pkg to `/tmp/dafeng-test-install/`,
run `dafeng-daemon --backend llama_cpp --model-path …` from there, and
the log shows the bundled plugins being loaded from `/private/tmp/…`,
the Metal kernels compile against Apple M4 Max, and rerank requests
return real candidate orderings in ~12-19 ms (cold start).

Final `.pkg` size: ~5.2 MB compressed.

## What 3.5 does NOT include

- **Apple Developer ID signing + notarization.** The .pkg works
  ad-hoc-signed (with the Gatekeeper right-click → Open bypass) but
  distributing through the App Store or to security-strict users
  needs Developer ID signing. Adds an ~$99/year cost + secret
  management in CI.
- **Intel (x86_64) `.pkg`.** v0.1.0 ships arm64 only. The release
  matrix used to include `macos-13` for Intel, but GH Actions free
  tier's `macos-13` queue is unreliable enough that the
  `publish-release` step routinely starved waiting for a runner.
  Apple Silicon coverage handles the practical majority of installs
  today; we can re-enable Intel when we have paid runners or a
  self-hosted Intel mac.
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
