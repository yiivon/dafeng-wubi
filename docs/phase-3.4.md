# Phase 3.4 — SwiftUI inspector

A small native macOS app — **大风五笔检查器 (Dafeng Inspector)** — that
gives users a window onto the daemon's state and the data it has
learned. Lives at `apps/inspector/` in the repo, ships as
`/Applications/Dafeng Inspector.app` via the `.pkg` installer.

## Three tabs

### 1. 状态 (Stats)

Polls `dafeng-cli stats` every 3 seconds. Shows uptime, backend
(`mock` / `deterministic` / `mlx` / `llama_cpp`), request counts,
errors, and mean / max rerank latency. Off-states have explicit
empty-state cards:

- *Daemon offline* — shown when `dafeng-cli stats` returns
  "daemon unreachable" (likely `launchctl unload` or crash).
- *Tool missing* — shown when `dafeng-cli` itself can't be located
  (`$DAFENG_CLI`, then `/usr/local/bin`, `/opt/homebrew/bin`,
  `which dafeng-cli`).

The latency card flips orange when max latency goes over 30 ms — that's
our `DESIGN.md` budget; the user should pay attention.

### 2. 输入历史 (History)

Reads `~/Library/Application Support/Dafeng/history.db` directly via
the SQLite C API in `SQLITE_OPEN_READONLY` mode — no risk of damaging
the daemon's writer. WAL means we don't block writes while the panel
is open.

- Filterable by substring on either `text` or `ctx` columns.
- Limit picker (100 / 200 / 500 / 1000).
- Total row count shown next to the filter.

### 3. 已学到的词 (Learned dict)

Parses `~/Library/Rime/dafeng_learned.dict.yaml`. The YAML header is
preserved verbatim; only the TSV body (text/code/weight rows) is
edited.

- Filterable by substring on `text` or `code`.
- Each row has a destructive **删除** button that pops a confirm
  alert. After delete, a banner reminds the user to run **菜单栏鼠须管
  → 重新部署** for the change to take effect (RIME re-compiles dicts
  on deploy).

## Build & run locally

```bash
# develop / iterate without leaving the SwiftPM shell:
cd apps/inspector
swift run -c release          # runs the CLI executable directly

# build a .app bundle at dist/Dafeng Inspector.app:
bash apps/inspector/build-app.sh
open "dist/Dafeng Inspector.app"
```

The bundle is ad-hoc-signed (`codesign --force --sign -`) for the
same reason the daemon is — Apple Silicon dyld refuses unsigned or
sig-mismatched Mach-O.

## Architecture choices

- **No bridging headers, no Objective-C.** Pure Swift / SwiftUI. Older
  macOS APIs (`NSStatusItem` for menu bar) are explicitly out of scope
  for this phase; if it's worth a menu bar version later, we revisit.

- **Subprocess to `dafeng-cli stats`** instead of re-implementing the
  IPC client in Swift. The CLI is already maintained, covers the full
  protocol, and any protocol changes will arrive in the binary the user
  has installed at the same version.

- **Direct SQLite read.** History queries are scoped — `WHERE text LIKE
  ?1 OR ctx LIKE ?1 ORDER BY ts_us DESC LIMIT ?2`. We don't add an
  ORM; the schema is small (5 columns, one table) and stable.

- **Header-preserving YAML edit.** No external YAML dependency. We
  split on the `---` / `...` document markers and only rewrite the
  body. If the user has hand-edited the header, we keep their edits.

## Packaging integration

`packaging/macos/build-pkg.sh` now:

1. Calls `apps/inspector/build-app.sh` if `dist/Dafeng Inspector.app`
   is missing.
2. Stages the bundle into the base component's `Applications/`
   payload directory.
3. `productbuild` walks the staging tree and preserves the bundle
   structure verbatim on install.

After install, the user finds **Dafeng Inspector** in
`/Applications`. Spotlight + Launchpad pick it up automatically.

## What 3.4 does NOT include

- **Menu bar / status item version.** A `NSStatusItem` "always
  available" UI is more useful day-to-day but doesn't fit the
  full-screen tabbed design here. Next iteration could ship both: the
  app for inspection, a menu bar item for live latency dot.
- **Editing of weights.** Right now the user can delete entries but
  not retune their weight. Adding an inline editor is straightforward;
  the file format already supports arbitrary weights. Deferred until a
  user asks.
- **Daemon control.** Start / Stop / Restart buttons would call
  `launchctl kickstart` / `unload`. The plumbing is one Swift func; we
  hold off until we have a real use case.
- **Localizations.** UI strings are Chinese-only for now. SwiftUI's
  built-in `String(localized:)` makes adding `en.lproj` later a
  mechanical pass.
