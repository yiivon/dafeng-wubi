# Phase 3.3 — Windows port (scaffold)

CLAUDE.md commits to Mac+Windows from day 1. Phase 2 / 3.x landed
Mac-first; Phase 3.3 fills in the Windows side of the abstractions
that were already in place as stubs.

This phase is **compile-clean and CI-verified**, but **not yet
end-to-end tested on a real Windows box**. The pieces below are designed
so any reasonably-experienced Windows developer can take this branch and
get a working daemon + Weasel integration in an afternoon.

## What's in this scaffold

### 1. Real Named Pipe IPC — [src/common/endpoint_win.cc](../src/common/endpoint_win.cc)

Mirrors the Unix Domain Socket implementation in
[endpoint_unix.cc](../src/common/endpoint_unix.cc). Same public API
([endpoint.h](../include/dafeng/endpoint.h)), same length-prefixed
framing semantics, same per-call timeout contract, same
shutdown-from-another-thread story. Differences are pure plumbing:

- Each `Accept()` creates a fresh pipe instance via `CreateNamedPipeW`
  with `PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED`. We block on
  `WaitForMultipleObjects(connect_event, shutdown_event)` so a peer
  thread can wake the accept loop.
- `ReadFull` / `WriteAll` use `OVERLAPPED` I/O and
  `WaitForSingleObject` for timeout. On `WAIT_TIMEOUT` we
  `CancelIoEx` and drain the pending op so the `OVERLAPPED` is
  re-usable.
- Byte-mode pipes (`PIPE_TYPE_BYTE | PIPE_READMODE_BYTE`) match the
  Mac behavior — the upper layer does its own framing.

The `ListenEndpoint` function does a sanity probe with
`FILE_FLAG_FIRST_PIPE_INSTANCE` so we surface bad pipe names at
construction, then closes it; the actual `Accept()` loop creates per-call
instances.

### 2. Real %APPDATA% paths — [src/common/paths_win.cc](../src/common/paths_win.cc)

- `GetDataDir()` → `%APPDATA%\Dafeng\` via
  `SHGetKnownFolderPath(FOLDERID_RoamingAppData)`. Falls back to the
  `APPDATA` env var, then the cwd. We prefer the API over the env var
  because Scheduled Task launches sometimes have stale env.
- `GetDaemonAddress()` → `\\.\pipe\dafeng-daemon-<sanitized-username>`.
  Per-user naming so two accounts on the same box don't collide. Pipe
  names are restricted to a conservative ASCII subset; non-ASCII bytes
  in the username get mapped to `_`.

### 3. Cross-platform daemon entry — [src/daemon/main.cc](../src/daemon/main.cc)

`InstallSignalHandlers()` is now `#ifdef`'d:

- POSIX: `sigaction(SIGINT/SIGTERM)` + `signal(SIGPIPE, SIG_IGN)`.
- Windows: `SetConsoleCtrlHandler` for `CTRL_C_EVENT`, `CTRL_BREAK_EVENT`,
  `CTRL_CLOSE_EVENT`, `CTRL_LOGOFF_EVENT`, `CTRL_SHUTDOWN_EVENT`. All
  drive the same `g_server->Shutdown()` exit path.

### 4. Cross-platform tests — [tests/unit/endpoint_test.cc](../tests/unit/endpoint_test.cc)

- `MakeTempEndpoint()` returns `/tmp/dafeng-endpoint-test-<rand>.sock` on
  POSIX, `\\.\pipe\dafeng-test-<rand>` on Windows.
- `SetTestEnv` / `UnsetTestEnv` shim around `setenv` ↔ `_putenv_s`.
- `PathsTest.DefaultDaemonAddressIsUnderDataDir` is POSIX-only;
  Windows gets `DefaultDaemonAddressIsPerUserPipe` which checks the
  per-user pipe-name shape.

### 5. Windows CI — [.github/workflows/ci.yml](../.github/workflows/ci.yml)

A new `windows-build-test` job on `windows-latest`:

- Installs `sqlite3:x64-windows` via the pre-installed vcpkg.
- Configures with `cmake -G "Visual Studio 17 2022" -A x64`,
  `DAFENG_BUILD_PLUGIN=OFF`, `DAFENG_ENABLE_GIT_SYNC=OFF`,
  `DAFENG_ENABLE_MLX=OFF`, `DAFENG_ENABLE_LLAMA_CPP=OFF`.
- Builds RelWithDebInfo, runs `ctest`. Catches compile regressions
  before they hit a real Windows machine.

The plugin (`dafeng_lua_bridge.so`) is off here because Weasel ships
its own librime headers we'd have to vendor; that's a follow-up.

## What's still TODO for a working Windows IME

In rough order:

1. **Weasel integration** — Weasel is the Windows port of Squirrel/RIME
   (https://rime.im/download/). Build the Lua bridge as `.dll` against
   Weasel's bundled librime, then drop into `%APPDATA%\Rime\lua\` along
   with `wubi86_dafeng.schema.yaml` etc. The Lua source itself is
   identical to Mac.

2. **`dafeng-cli setup` Windows path** — currently the `setup`
   subcommand looks for `/usr/local/share/dafeng` or `/opt/homebrew/share/dafeng`.
   Add a Windows branch that probes `%PROGRAMFILES%\Dafeng\`.

3. **Service auto-start** — POSIX has `launchd`. Windows equivalents,
   easiest first:
   - **Scheduled Task** triggered "at user logon" — easy, runs in user
     context, no Service Control Manager hoops. Recommended.
   - **Windows Service** — runs as SYSTEM, complex auth dance for the
     per-user pipe. Probably overkill.

4. **Installer** — NSIS or WiX (MSI) to drop `dafeng-daemon.exe`,
   `dafeng-cli.exe`, the Lua bridge `.dll`, schemas, and a Scheduled Task
   `.xml`. NSIS is shorter; WiX is more idiomatic for enterprise.

5. **Enable git_sync on Windows** — libgit2 via vcpkg. Add the
   `find_package(libgit2)` branch to `src/daemon/CMakeLists.txt` once
   we have a Windows test box to actually exercise the userdata sync.

6. **End-to-end smoke** — install Weasel, drop the schema + bridge,
   run daemon as foreground, type and watch the rerank candidates flow.
   Document any Weasel-specific quirks (it diverges from Squirrel in
   some Lua hook semantics; the L3 fixes we hit on Mac may need Windows
   counterparts).

## How to build on a Windows machine right now

```powershell
# One-time deps via vcpkg (or chocolatey, or scoop — vcpkg is easiest
# for the C/C++ side).
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install sqlite3:x64-windows

# Configure + build.
cd C:\path\to\dafeng-wubi
cmake -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake `
      -DVCPKG_TARGET_TRIPLET=x64-windows `
      -DDAFENG_ENABLE_GIT_SYNC=OFF `
      -DDAFENG_BUILD_PLUGIN=OFF
cmake --build build --config RelWithDebInfo -j

# Run daemon in foreground; CLI talks to it via the per-user pipe name.
.\build\src\daemon\RelWithDebInfo\dafeng-daemon.exe `
    --backend deterministic --log-level debug
# In another terminal:
.\build\src\cli\RelWithDebInfo\dafeng-cli.exe ping
```

## Verification on this branch

- `ctest` 146/146 still pass on `macos-14` (Phase 3.3 changes are
  zero-impact on Mac).
- Windows CI job (`windows-build-test`) compiles the daemon, CLI, and
  unit tests against MSVC + vcpkg, runs the cross-platform test set.
  Any compile drift on Windows surfaces in the CI badge before it lands
  on `main`.
