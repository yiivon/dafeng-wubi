import Foundation
import SwiftUI

// MenuBarController — drives the menu-bar status icon + dropdown.
//
// "Why a separate menu bar app?" Squirrel's own menu (the keyboard icon)
// is hardcoded in Squirrel.app — third-party schemas can't extend it.
// macOS's status bar (right side of the menu bar) is the equivalent
// surface every other native app uses to expose quick toggles, so we
// drop a tiny icon there. Same UX: hover, click, toggle.
//
// The controller polls `dafeng-cli stats` every few seconds to keep the
// icon in sync (LLM on / deterministic / paused / unreachable) and
// shells out to dafeng-cli for state changes.

enum DaemonBackend: String {
    case llama = "llama_cpp"
    case deterministic = "deterministic"
    case mlx = "mlx"
    case mock = "mock"
    case unknown
}

enum DaemonRunState {
    case running(backend: DaemonBackend)
    case paused
    case unreachable    // tool found, daemon down
    case toolMissing    // dafeng-cli not on $PATH
    case unknown        // initial state, before first poll
}

@MainActor
final class MenuBarController: ObservableObject {
    @Published var state: DaemonRunState = .unknown
    @Published var lastError: String? = nil

    private var pollTask: Task<Void, Never>?

    // ---------- lifecycle ----------

    init() {
        // Start polling as soon as the controller is constructed.
        // We deliberately do NOT bind this to a SwiftUI .task on the
        // window — the user may close the window but expect the menu
        // bar item to keep showing accurate status.
        start()
    }

    deinit {
        pollTask?.cancel()
    }

    private func start() {
        pollTask?.cancel()
        pollTask = Task { [weak self] in
            while !Task.isCancelled {
                await self?.poll()
                try? await Task.sleep(nanoseconds: 4_000_000_000)
            }
        }
    }

    // ---------- view-model helpers ----------

    var statusSymbol: String {
        switch state {
        case .running(let b) where b == .llama: return "bolt.fill"
        case .running:                          return "bolt"
        case .paused:                           return "moon.zzz.fill"
        case .unreachable, .toolMissing:        return "exclamationmark.triangle"
        case .unknown:                          return "circle.dotted"
        }
    }

    var statusTitle: String {
        switch state {
        case .running(let b):
            switch b {
            case .llama:         return "运行中 — LLM 后端"
            case .deterministic: return "运行中 — 快速后端(省电)"
            case .mlx:           return "运行中 — MLX 后端"
            case .mock:          return "运行中 — mock 后端"
            case .unknown:       return "运行中"
            }
        case .paused:        return "已暂停 — IME 走原生 RIME"
        case .unreachable:   return "Daemon 不可达"
        case .toolMissing:   return "找不到 dafeng-cli"
        case .unknown:       return "查询中…"
        }
    }

    var canToggleBackend: Bool {
        if case .running = state { return true }
        return false
    }

    var canPause: Bool {
        if case .running = state { return true }
        return false
    }

    var canResume: Bool {
        if case .paused = state { return true }
        return false
    }

    var currentBackend: DaemonBackend {
        if case .running(let b) = state { return b }
        return .unknown
    }

    // ---------- actions ----------

    func pause() {
        Task.detached { [weak self] in
            _ = DafengCLI.run(arguments: ["pause"], timeout: 5.0)
            await self?.poll()
        }
    }

    func resume() {
        Task.detached { [weak self] in
            _ = DafengCLI.run(arguments: ["resume"], timeout: 10.0)
            await self?.poll()
        }
    }

    /// Re-run `dafeng-cli setup` with or without --backend llama_cpp,
    /// which rewrites the launchd plist + reloads. setup itself takes
    /// a few seconds (deploy + launchctl bootstrap + warmup), so we
    /// poll a couple times after.
    func switchTo(_ backend: DaemonBackend) {
        Task.detached { [weak self] in
            var args = ["setup"]
            if backend == .llama { args.append(contentsOf: ["--backend", "llama_cpp"]) }
            _ = DafengCLI.run(arguments: args, timeout: 60.0)
            // Daemon needs a beat to come back; LLM warmup is ~3-15 s.
            for _ in 0..<10 {
                try? await Task.sleep(nanoseconds: 1_500_000_000)
                if let s = self {
                    await s.poll()
                    if case .running(let b) = await s.state, b == backend { break }
                }
            }
        }
    }

    func openInspectorWindow() {
        NSApp.activate(ignoringOtherApps: true)
        // Find the inspector's main window; SwiftUI gives it our title.
        // If hidden, makeKeyAndOrderFront re-shows it.
        if let w = NSApp.windows.first(where: { $0.title.contains("检查器") }) {
            w.makeKeyAndOrderFront(nil)
        }
    }

    func quit() { NSApp.terminate(nil) }

    // ---------- polling ----------

    private func poll() async {
        let result = await Task.detached(priority: .userInitiated) {
            DafengCLI.run(arguments: ["stats"], timeout: 2.0)
        }.value
        guard let r = result else {
            self.state = .toolMissing
            return
        }
        if r.exit != 0 {
            // Most common failure: "stats failed (timeout or daemon unreachable)".
            // We can't distinguish "paused" (no daemon at all) from "running but
            // hung" via stats alone — but `dafeng-cli pause` makes the launchd
            // service disappear, so we use a `pgrep` follow-up to disambiguate.
            let alive = await Task.detached {
                DafengCLI.run(arguments: ["ping"], timeout: 1.0)?.exit == 0
            }.value
            self.state = alive ? .unreachable : .paused
            return
        }
        // Parse "rerank model     : llama_cpp (v3)" out of stats output.
        var backend = DaemonBackend.unknown
        for line in r.stdout.split(whereSeparator: { $0.isNewline }) {
            let l = String(line)
            guard let colon = l.firstIndex(of: ":") else { continue }
            let key = l[..<colon].trimmingCharacters(in: .whitespaces)
            if key == "rerank model" {
                let rest = l[l.index(after: colon)...]
                    .trimmingCharacters(in: .whitespaces)
                let token = rest.split(separator: " ", maxSplits: 1).first ?? ""
                backend = DaemonBackend(rawValue: String(token)) ?? .unknown
                break
            }
        }
        self.state = .running(backend: backend)
    }
}
