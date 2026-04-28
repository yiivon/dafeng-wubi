import Foundation

// Thin wrapper around the `dafeng-cli` executable. We shell out instead of
// re-implementing the IPC client in Swift — the CLI is already maintained,
// covers the full protocol, and changes ship in lockstep with the daemon.
//
// Resolution order (the GUI app inherits a minimal PATH so several of
// these matter):
//   1. $DAFENG_CLI env var (if set + executable)
//   2. canonical install locations: /usr/local/bin, /opt/homebrew/bin
//   3. derived from the running daemon's launchd plist — replace the
//      `/src/daemon/dafeng-daemon` segment with `/src/cli/dafeng-cli`
//      (covers dev-tree builds at $REPO/build-llama/...)
//   4. `which dafeng-cli` via /usr/bin/which (works only if PATH has it)
enum DafengCLI {
    /// Resolve the dafeng-cli binary path. Returns nil if it can't be found.
    static func locate() -> URL? {
        if let env = ProcessInfo.processInfo.environment["DAFENG_CLI"],
           !env.isEmpty,
           FileManager.default.isExecutableFile(atPath: env) {
            return URL(fileURLWithPath: env)
        }
        let canonical = [
            "/usr/local/bin/dafeng-cli",
            "/opt/homebrew/bin/dafeng-cli",
        ]
        for path in canonical {
            if FileManager.default.isExecutableFile(atPath: path) {
                return URL(fileURLWithPath: path)
            }
        }
        if let derived = deriveFromLaunchAgent() {
            return derived
        }
        if let viaWhich = locateViaWhich() {
            return viaWhich
        }
        return nil
    }

    /// Read the LaunchAgent plist for com.dafeng.daemon, find the
    /// daemon binary path in ProgramArguments[0], substitute the
    /// `daemon` path component for `cli`. Handles the dev-tree case
    /// where the user installed the LaunchAgent pointing at a build
    /// dir that isn't on PATH.
    private static func deriveFromLaunchAgent() -> URL? {
        let home = FileManager.default.homeDirectoryForCurrentUser
        let plistURL = home.appendingPathComponent(
            "Library/LaunchAgents/com.dafeng.daemon.plist")
        guard let data = try? Data(contentsOf: plistURL),
              let plist = try? PropertyListSerialization.propertyList(
                from: data, options: [], format: nil) as? [String: Any],
              let args = plist["ProgramArguments"] as? [String],
              let daemonPath = args.first else {
            return nil
        }
        // Map */src/daemon/dafeng-daemon → */src/cli/dafeng-cli.
        // We use `replacingOccurrences` rather than path arithmetic so
        // either canonical layout works (build/, build-llama/, etc).
        let cliPath = daemonPath.replacingOccurrences(
            of: "/src/daemon/dafeng-daemon",
            with: "/src/cli/dafeng-cli")
        if cliPath != daemonPath,
           FileManager.default.isExecutableFile(atPath: cliPath) {
            return URL(fileURLWithPath: cliPath)
        }
        // Same-directory fallback: install_launchagent.sh sometimes
        // points at a directly-installed `bin/dafeng-daemon` and the
        // sibling cli is in the same `bin/`.
        let dir = (daemonPath as NSString).deletingLastPathComponent
        let sibling = dir + "/dafeng-cli"
        if FileManager.default.isExecutableFile(atPath: sibling) {
            return URL(fileURLWithPath: sibling)
        }
        return nil
    }

    private static func locateViaWhich() -> URL? {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/which")
        task.arguments = ["dafeng-cli"]
        let pipe = Pipe()
        task.standardOutput = pipe
        task.standardError = Pipe()
        do {
            try task.run()
            task.waitUntilExit()
        } catch {
            return nil
        }
        guard task.terminationStatus == 0 else { return nil }
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        let trimmed = String(data: data, encoding: .utf8)?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        return trimmed.isEmpty ? nil : URL(fileURLWithPath: trimmed)
    }

    /// Run `dafeng-cli <args>` and return stdout (or nil on launch failure).
    /// Stderr is captured separately and returned alongside.
    @discardableResult
    static func run(arguments: [String], timeout: TimeInterval = 2.0)
        -> (stdout: String, stderr: String, exit: Int32)?
    {
        guard let bin = locate() else { return nil }
        let task = Process()
        task.executableURL = bin
        task.arguments = arguments

        let outPipe = Pipe()
        let errPipe = Pipe()
        task.standardOutput = outPipe
        task.standardError = errPipe

        do {
            try task.run()
        } catch {
            return nil
        }

        // Soft timeout: spin a one-shot watchdog that terminates the task
        // if it overshoots. Pure-CLI calls usually finish in tens of ms.
        let deadline = DispatchTime.now() + timeout
        DispatchQueue.global().asyncAfter(deadline: deadline) {
            if task.isRunning { task.terminate() }
        }
        task.waitUntilExit()

        let outData = outPipe.fileHandleForReading.readDataToEndOfFile()
        let errData = errPipe.fileHandleForReading.readDataToEndOfFile()
        return (
            String(data: outData, encoding: .utf8) ?? "",
            String(data: errData, encoding: .utf8) ?? "",
            task.terminationStatus
        )
    }
}
